// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Length-bucketed throughput benchmark comparing the hash algorithms (see
// hash_test_util.h). Run with: bazel run -c opt //mbo/hash:hash_benchmark
//
// Two size modes (see mbo/hash/measurements/): the default FAST set is the
// small, README-published set - cheap enough for CI. Setting the environment
// variable MBO_HASH_BENCHMARK_FULL=1 selects the dense FULL set that straddles
// every dispatch-tier boundary and SSO cutoff, for the complete dataset and
// the ns-vs-length graph.

#ifdef __linux__
# include <pthread.h>
# include <sched.h>
#endif  // __linux__

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <version>

#if defined(__APPLE__)
# include <Availability.h>
#endif

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "benchmark/benchmark.h"
#include "mbo/hash/hash_test_util.h"

namespace mbo::hash {
namespace {

// NOLINTBEGIN(*-array-index,*-magic-numbers)

constexpr uint64_t kSeed = 5'381;
static_assert(__cplusplus >= 202'302L, "the hash benchmark provenance requires C++23");

void AddBuildContext() {
#if defined(__clang__)
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_name", "Apple Clang");
# else
  benchmark::AddCustomContext("compiler_name", "Clang");
# endif
  benchmark::AddCustomContext("compiler", absl::StrCat("clang-", __clang_major__));
  benchmark::AddCustomContext(
      "compiler_version", absl::StrCat(__clang_major__, ".", __clang_minor__, ".", __clang_patchlevel__));
  benchmark::AddCustomContext("compiler_version_extra", __clang_version__);
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_build_version", absl::StrCat(__apple_build_version__));
# endif
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler_name", "GCC");
  benchmark::AddCustomContext("compiler", absl::StrCat("gcc-", __GNUC__));
  benchmark::AddCustomContext(
      "compiler_version", absl::StrCat(__GNUC__, ".", __GNUC_MINOR__, ".", __GNUC_PATCHLEVEL__));
  benchmark::AddCustomContext("compiler_version_extra", __VERSION__);
#endif

  benchmark::AddCustomContext("cxx_standard_requested", "c++23");
  benchmark::AddCustomContext("cplusplus", absl::StrCat(__cplusplus));
#if defined(_LIBCPP_VERSION)
  benchmark::AddCustomContext("standard_library", "libc++");
  benchmark::AddCustomContext("standard_library_version", absl::StrCat(_LIBCPP_VERSION));
#elif defined(__GLIBCXX__)
  benchmark::AddCustomContext("standard_library", "libstdc++");
  benchmark::AddCustomContext("standard_library_version", absl::StrCat(__GLIBCXX__));
# if defined(_GLIBCXX_RELEASE)
  benchmark::AddCustomContext("standard_library_release", absl::StrCat(_GLIBCXX_RELEASE));
# endif
#endif

#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
  benchmark::AddCustomContext("macos_deployment_target", absl::StrCat(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__));
#endif
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED)
  benchmark::AddCustomContext("macos_sdk_maximum", absl::StrCat(__MAC_OS_X_VERSION_MAX_ALLOWED));
#endif
}

// FAST set (default, CI, and the README tables): a dense set straddling every
// dispatch-tier boundary and SSO cutoff so the small-key cliffs are visible -
// 7/8 the fully-unrolled <=8 path, 15/16 the <=16 path (15 = libstdc++ SSO
// cap), 22 the libc++ SSO cap, 38/47/48 and 63/64 the short-chain steps, 127/128
// bracketing the chain->bulk 128-byte-window edge, with 3/5/11/19/27 filling the
// small range and 256/1024/4096 the bulk.
constexpr std::array<int, 22> kReadmeSizes = {
    1, 3, 5, 7, 8, 11, 15, 16, 19, 22, 27, 32, 38, 47, 48, 63, 64, 127, 128, 256, 1'024, 4'096,
};

// FULL set (MBO_HASH_BENCHMARK_FULL=1): ~3x denser, a slow exponential (ratio
// ~1.2) from 1..4096 unioned with the boundary set above, so the ns-vs-length
// curve is smooth and the tier edges stay sampled. For the complete dataset /
// graph, not for the README tables.
// Tiny             9
// GCC/MSVC SSO     7
// Clang SSO        6
// Extended/AVX2    9
// Cache            4
// Bins             5
// Medium          14
// Large           10
// TOTAL           64
constexpr std::array<int, 64> kFullSizes = {
    // --- Tiny Keys & Word Alignments ---
    1, 2, 3,
    4,  // 32-bit register (optimized fast-paths for integers)
    5, 6, 7,
    8,  // 64-bit word (standard 64-bit register boundary)
    9,

    // --- Register Alignments & GCC/MSVC SSO limits ---
    11,
    12,  // 3x 4-byte words / 3D float vectors
    13,
    15,  // GCC (libstdc++) & MSVC SSO capacity limit (15 chars + null)
    16,  // 128-bit vector register boundary (SSE/AVX)
    18, 19,

    // --- Clang SSO limit & Object sizes ---
    22,  // Clang (libc++) SSO capacity limit (22 chars + null)
    23,  // Clang heap spill boundary (first byte to trigger dynamic allocation); Folly fbstring SSO capacity
    24,  // LLVM's libc++: sizeof(std::string) 3x 8-byte; Folly fbstring SSO capacity
    25, 27, 29,

    // --- Extended SSO & 256-bit Vector Limits ---
    31,  // jemalloc 32-byte class boundary
    32,  // AVX2: 256-bit vector register boundary; MSVC: sizeof(std::string) 4x 8-byte
    38,
    40,          // Common loop unrolling (5x 8-byte words)
    46, 47, 48,  // Alignments around 48 bytes (tcmalloc size class / MSVC heap spill)
    49,          // Just over 48 bytes (forces allocator to bump to 64-byte class)
    55,

    // --- CPU Cache Line Boundaries (64 Bytes) ---
    63,  // Just under cache line (fits entirely within one line)
    64,  // Exactly one L1 cache line (64 bytes)
    66,  // Just over cache line (spills into second cache line)
    72,

    // --- Allocator Bin / Bucket Transitions (jemalloc & tcmalloc) ---
    79,  // Just under 80-byte allocator bucket
    80,  // 80-byte allocation class boundary
    95,  // Just under 96-byte allocator bucket
    107, 114,

    // --- Dual Cache Line & Medium Keys (Scaling exponentially at ~1.2x) ---
    127,                 // Just under 2 cache lines
    128,                 // Exactly 2 cache lines (AVX-512 register size)
    137, 165, 198, 237,  // Geometric steps mapping allocator classes (144, 176, 208)
    256,                 // 4 cache lines / 256-byte allocator boundary
    285, 342, 410, 492, 591, 709, 851,

    // --- Large Keys & Page Boundaries ---
    1'021,  // Max payload size fitting inside a 1KB allocator block with a null-terminator
    1'024,  // Exactly 1KB (half a page block step for typical modern slab allocators)
    1'225, 1'470, 1'764, 2'116, 2'540, 3'048, 3'657,
    4'096  // Exactly one x86/ARM64 virtual page (often triggering direct mmap)
};

// Table/chart consistency rule: kReadmeSizes (README tables) must be a subset of
// kFullSizes (the throughput chart), so every table row has a matching point on
// the curve. Add a README size to the full set too. Enforced at compile time.
constexpr bool ReadmeSizesAreSubsetOfFull() {
  for (const int want : kReadmeSizes) {
    bool found = false;
    for (const int have : kFullSizes) {
      if (have == want) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

static_assert(ReadmeSizesAreSubsetOfFull(), "every kReadmeSizes entry must also appear in kFullSizes");

// The active throughput size set, selected once by the environment.
std::span<const int> ThroughputSizes() {
  const char* const full = std::getenv("MBO_HASH_BENCHMARK_FULL");  // NOLINT(concurrency-mt-unsafe): startup only
  if (full != nullptr && std::string_view(full) == "1") {
    return kFullSizes;
  }
  return kReadmeSizes;
}

template<typename Algo>
requires HasGetHash64<Algo>
void BmHash64(benchmark::State& state) {
  const auto length = static_cast<std::size_t>(state.range(0));
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): fixed data per length
  std::mt19937_64 rng(0x1234);
  const std::string data = algo::RandomString(rng, length);
  for (auto _ : state) {
    benchmark::DoNotOptimize(Algo::GetHash64(data, kSeed));
  }
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(length));
  state.SetLabel(std::string(Algo::Name()));
}

template<typename Algo>
requires HasGetHash128<Algo>
void BmHash128(benchmark::State& state) {
  const auto length = static_cast<std::size_t>(state.range(0));
  // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): fixed data per length
  std::mt19937_64 rng(0x1234);
  const std::string data = algo::RandomString(rng, length);
  for (auto _ : state) {
    benchmark::DoNotOptimize(Algo::GetHash128(data, kSeed));
  }
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(length));
  state.SetLabel(std::string(Algo::Name()));
}

// --- Throughput over upper-bounded length ranges ----------------------------
//
// BmHash64 / BmHash128 above hash ONE key of an exact length in a hot loop: the
// exact-length -> time curve (the "latency" view). This is the complementary
// throughput view - how a realistic, upper-BOUNDED range of key lengths
// translates to a single bytes/s number.
//
// Two documented length distributions as inverse-CDF control points (cumulative
// percentile -> length in bytes), piecewise-linear between points:
//   - Short: identifiers / DB keys / UUIDs (log-normal), ceiling 128 B.
//   - Web:   paths, URLs, larger text keys (heavy-tailed), ceiling 4096 B.
// Each control-point length doubles as an upper BOUND: we run the mix truncated
// to each bound, with the kept buckets' weights renormalized to 100% (which
// falls straight out of scaling the percentile draw into [0, cdf[bound].pct)).
// A run therefore yields (X = upper-bound length, Y = bytes/s), and sweeping the
// bounds gives the upper-length -> throughput curve. Keys are built once per
// (distribution, bound) with a fixed seed and shared across every algorithm, so
// the set is reproducible and identical for all algorithms (only the LENGTHS
// matter; the bytes are filler). The unpredictable length order defeats the
// size-dispatch branch predictor - the cost a real mixed workload pays.
constexpr std::size_t kLatencyKeys = 1'024;  // power of two for cheap masking
constexpr std::size_t kCdfPoints = 9;        // inverse-CDF control points per distribution

struct LatencyDist {
  std::string_view name;
  std::array<std::pair<double, int>, kCdfPoints> cdf;  // ascending (cumulative pct, length); last = {1.0, Lmax}
};

constexpr std::array<LatencyDist, 2> kLatencyDists = {{
    {.name = "Short",
     .cdf = {{
         {0.10, 8},
         {0.25, 12},
         {0.50, 16},
         {0.75, 23},
         {0.90, 31},
         {0.95, 38},
         {0.99, 53},
         {0.999, 80},
         {1.0, 128},  // 100% ceiling: two L1 cache lines / the SSO & AVX-512 transition
     }}},
    {.name = "Web",
     .cdf = {{
         {0.10, 15},
         {0.25, 28},
         {0.50, 45},
         {0.75, 75},
         {0.90, 120},
         {0.95, 220},
         {0.99, 512},
         {0.999, 2'048},
         {1.0, 4'096},  // 100% ceiling: one x86/ARM64 virtual page
     }}},
}};

// Inverse CDF: percentile p in [0,1) -> length. Piecewise-linear between control
// points; below the first point interpolate from (0, 1 byte). Scaling p into
// [0, cdf[bound].pct) restricts the draw to buckets <= that bound and
// renormalizes their weights to 100% - the truncation the sweep needs.
std::size_t SampleLength(const LatencyDist& dist, double percentile) {
  double prev_p = 0.0;
  double prev_len = 1.0;
  for (const auto& [pct, len] : dist.cdf) {
    if (percentile < pct) {
      const double frac = (percentile - prev_p) / (pct - prev_p);
      return static_cast<std::size_t>(std::lround(prev_len + (frac * (len - prev_len))));
    }
    prev_p = pct;
    prev_len = len;
  }
  return static_cast<std::size_t>(dist.cdf.back().second);
}

// Key sets built once, one per (distribution, bound), shared by every algorithm.
// Bound `b` truncates distribution `d` to lengths <= cdf[b].length: 1023 keys
// drawn from the renormalized truncated distribution, plus one anchor key pinned
// to the bound length so the boundary is always represented.
const std::vector<std::string>& ThroughputKeys(std::size_t dist_index, std::size_t bound_index) {
  static const std::array<std::array<std::vector<std::string>, kCdfPoints>, kLatencyDists.size()> kKeySets = [] {
    std::array<std::array<std::vector<std::string>, kCdfPoints>, kLatencyDists.size()> sets;
    for (std::size_t dist_idx = 0; dist_idx < kLatencyDists.size(); ++dist_idx) {
      const LatencyDist& dist = kLatencyDists.at(dist_idx);
      for (std::size_t point = 0; point < kCdfPoints; ++point) {
        // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): fixed, reproducible set
        std::mt19937_64 rng(0x1a7e9c1);
        const double bound_pct = dist.cdf.at(point).first;
        const auto bound_len = static_cast<std::size_t>(dist.cdf.at(point).second);
        std::vector<std::string>& keys = sets.at(dist_idx).at(point);
        keys.reserve(kLatencyKeys);
        for (std::size_t i = 0; i + 1 < kLatencyKeys; ++i) {
          const double draw = static_cast<double>(rng()) / (static_cast<double>(UINT64_MAX) + 1.0);
          keys.push_back(algo::RandomString(rng, SampleLength(dist, draw * bound_pct)));
        }
        keys.push_back(algo::RandomString(rng, bound_len));  // anchor at the upper bound
      }
    }
    return sets;
  }();
  return kKeySets.at(dist_index).at(bound_index);
}

template<typename Algo>
requires HasGetHash64<Algo>
void BmHash64Throughput(benchmark::State& state, std::size_t dist_index, std::size_t bound_index) {
  const std::vector<std::string>& keys = ThroughputKeys(dist_index, bound_index);
  int64_t total_bytes = 0;
  for (const std::string& key : keys) {
    total_bytes += static_cast<int64_t>(key.size());
  }
  std::size_t counter = 0;
  for (auto _ : state) {
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access): timed loop; the power-of-two mask keeps it in range.
    benchmark::DoNotOptimize(Algo::GetHash64(keys[counter++ & (kLatencyKeys - 1)], kSeed));
  }
  state.SetItemsProcessed(state.iterations());
  // Throughput headline: average key length x iterations = bytes hashed -> bytes/s.
  state.SetBytesProcessed(state.iterations() * (total_bytes / static_cast<int64_t>(kLatencyKeys)));
  state.SetLabel(std::string(Algo::Name()));
}

template<typename Algo>
requires HasGetHash128<Algo>
void BmHash128Throughput(benchmark::State& state, std::size_t dist_index, std::size_t bound_index) {
  const std::vector<std::string>& keys = ThroughputKeys(dist_index, bound_index);
  int64_t total_bytes = 0;
  for (const std::string& key : keys) {
    total_bytes += static_cast<int64_t>(key.size());
  }
  std::size_t counter = 0;
  for (auto _ : state) {
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access): timed loop; the power-of-two mask keeps it in range.
    benchmark::DoNotOptimize(Algo::GetHash128(keys[counter++ & (kLatencyKeys - 1)], kSeed));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * (total_bytes / static_cast<int64_t>(kLatencyKeys)));
  state.SetLabel(std::string(Algo::Name()));
}

// Registers the 64-bit benchmarks (latency + throughput) for an algorithm that
// exposes GetHash64, and the 128-bit ones for an algorithm that exposes
// GetHash128 - so a 64-only or a 128-only algorithm is handled correctly.
template<typename Algo>
void RegisterAlgo() {
  const std::string name(Algo::Name());
  const std::span<const int> sizes = ThroughputSizes();
  // Throughput over upper-bounded length ranges: one benchmark per (distribution,
  // bound), named "BmHash{64,128}Throughput<algo>/<Short|Web>:<bound>", each
  // reporting bytes/s. Sweeping the bounds gives the upper-length -> throughput
  // curve; the exact-length BmHash{64,128} give the latency curve.
  if constexpr (HasGetHash64<Algo>) {
    auto* const hash64 = benchmark::RegisterBenchmark(absl::StrCat("BmHash64<", name, ">"), BmHash64<Algo>);
    for (const int size : sizes) {
      hash64->Arg(size);
    }
    for (std::size_t dist = 0; dist < kLatencyDists.size(); ++dist) {
      for (std::size_t bound = 0; bound < kCdfPoints; ++bound) {
        benchmark::RegisterBenchmark(
            absl::StrCat(
                "BmHash64Throughput<", name, ">/", kLatencyDists.at(dist).name, ":",
                kLatencyDists.at(dist).cdf.at(bound).second),
            [dist, bound](benchmark::State& state) { BmHash64Throughput<Algo>(state, dist, bound); });
      }
    }
  }
  if constexpr (HasGetHash128<Algo>) {
    auto* const hash128 = benchmark::RegisterBenchmark(absl::StrCat("BmHash128<", name, ">"), BmHash128<Algo>);
    for (const int size : sizes) {
      hash128->Arg(size);
    }
    for (std::size_t dist = 0; dist < kLatencyDists.size(); ++dist) {
      for (std::size_t bound = 0; bound < kCdfPoints; ++bound) {
        benchmark::RegisterBenchmark(
            absl::StrCat(
                "BmHash128Throughput<", name, ">/", kLatencyDists.at(dist).name, ":",
                kLatencyDists.at(dist).cdf.at(bound).second),
            [dist, bound](benchmark::State& state) { BmHash128Throughput<Algo>(state, dist, bound); });
      }
    }
  }
}

// Registers benchmarks for every descriptor in the central algo::AllAlgorithms
// list -- an algorithm added there is automatically benchmarked here. The tuple
// (of empty descriptor structs) is passed by value purely to deduce the pack.
template<typename... Algos>
void RegisterAll(std::tuple<Algos...> /*algorithms*/) {
  (RegisterAlgo<Algos>(), ...);
}

// NOLINTEND(*-array-index,*-magic-numbers)

}  // namespace
}  // namespace mbo::hash

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);  // NO ASLR

#ifdef __linux__
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);  // Pin to the first available core
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
#endif  // __linux__

  mbo::hash::RegisterAll(mbo::hash::algo::AllAlgorithms{});
  benchmark::Initialize(&argc, argv);
  benchmark::AddCustomContext("algos", absl::StrJoin(mbo::hash::algo::GetAlgoNames(), ", "));
  // Record the toolchain that built THIS executable. Compiler choice affects
  // performance, while the language, standard library, and Apple SDK fields
  // make the resulting artifact independently auditable.
  mbo::hash::AddBuildContext();
  // Emit the curated README size subset (kReadmeSizes) so the report tool extracts
  // the small table straight from a FULL dataset - no separate fast run, and no
  // second size list to drift (this C++ list is the single source of truth).
  benchmark::AddCustomContext("readme_sizes", absl::StrJoin(mbo::hash::kReadmeSizes, ","));
  // Export the throughput length distributions in use as "name=pct:len,...;..."
  // (the full inverse-CDF, not just the bound labels), so a dataset records
  // exactly which mix produced its BmHash64Throughput<algo>/<name>:<bound> numbers.
  for (const auto& throughput_dists : mbo::hash::kLatencyDists) {
    benchmark::AddCustomContext(
        absl::StrCat("throughput_dists:", throughput_dists.name),
        absl::StrJoin(throughput_dists.cdf, ",", [](std::string* cdf_out, const std::pair<double, int>& point) {
          absl::StrAppend(cdf_out, point.first, ":", point.second);
        }));
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
