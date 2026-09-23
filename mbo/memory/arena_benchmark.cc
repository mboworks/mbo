// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <span>
#include <string>
#include <vector>
#include <version>

#if defined(__APPLE__)
# include <Availability.h>
#endif

#include "benchmark/benchmark.h"
#include "mbo/memory/arena.h"

namespace mbo::memory {
namespace {

constexpr std::size_t kBatch = 1'024;
static_assert(__cplusplus >= 202'302L, "the Arena benchmark provenance requires C++23");

void AddBuildContext() {
#if defined(__clang__)
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_name", "Apple Clang");
# else
  benchmark::AddCustomContext("compiler_name", "Clang");
# endif
  benchmark::AddCustomContext("compiler", std::string("clang-") + std::to_string(__clang_major__));
  benchmark::AddCustomContext(
      "compiler_version", std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "."
                              + std::to_string(__clang_patchlevel__));
  benchmark::AddCustomContext("compiler_version_extra", __clang_version__);
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_build_version", std::to_string(__apple_build_version__));
# endif
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler_name", "GCC");
  benchmark::AddCustomContext("compiler", std::string("gcc-") + std::to_string(__GNUC__));
  benchmark::AddCustomContext(
      "compiler_version",
      std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__));
  benchmark::AddCustomContext("compiler_version_extra", __VERSION__);
#endif

  benchmark::AddCustomContext("cxx_standard_requested", "c++23");
  benchmark::AddCustomContext("cplusplus", std::to_string(__cplusplus));
#if defined(_LIBCPP_VERSION)
  benchmark::AddCustomContext("standard_library", "libc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(_LIBCPP_VERSION));
#elif defined(__GLIBCXX__)
  benchmark::AddCustomContext("standard_library", "libstdc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(__GLIBCXX__));
# if defined(_GLIBCXX_RELEASE)
  benchmark::AddCustomContext("standard_library_release", std::to_string(_GLIBCXX_RELEASE));
# endif
#endif

#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
  benchmark::AddCustomContext("macos_deployment_target", std::to_string(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__));
#endif
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED)
  benchmark::AddCustomContext("macos_sdk_maximum", std::to_string(__MAC_OS_X_VERSION_MAX_ALLOWED));
#endif
}

constexpr std::array<std::size_t, 16> kStringLikeSizes = {
    1, 3, 5, 7, 8, 11, 15, 16, 19, 23, 31, 32, 47, 64, 127, 511,
};
constexpr ArenaOptions kBenchmarkOptions{
    .initial_block_size = std::size_t{64} * 1'024,
    .maximum_block_size = std::size_t{4} * 1'024 * 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};
constexpr ArenaOptions kFixedBenchmarkOptions{
    .initial_block_size = 4'096,
    .maximum_block_size = 4'096,
    .growth_numerator = 1,
    .growth_denominator = 1,
};

void BmArenaAllocate(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  Arena<NewDeleteBlockSource, kBenchmarkOptions> arena;
  std::size_t used = 0;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      benchmark::DoNotOptimize(arena.Allocate(size, alignment));
    }
    used = arena.bytes_used();
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
  state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
  state.counters["blocks"] = static_cast<double>(arena.block_count());
  state.counters["used"] = static_cast<double>(used);
  state.counters["padding"] = static_cast<double>(used - (kBatch * size));
}

void BmPmrMonotonicAllocate(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  std::pmr::monotonic_buffer_resource resource;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      benchmark::DoNotOptimize(resource.allocate(size, alignment));
    }
    resource.release();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
}

void BmAllocatorArenaAllocate(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  Arena<AllocatorBlockSource<>, kBenchmarkOptions> arena;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      benchmark::DoNotOptimize(arena.Allocate(size, alignment));
    }
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
  state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
  state.counters["blocks"] = static_cast<double>(arena.block_count());
}

void BmPmrArenaAllocate(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  std::pmr::monotonic_buffer_resource resource;
  Arena<PmrBlockSource, kBenchmarkOptions> arena{PmrBlockSource(&resource)};
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      benchmark::DoNotOptimize(arena.Allocate(size, alignment));
    }
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
  state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
  state.counters["blocks"] = static_cast<double>(arena.block_count());
}

void BmNewDeleteAllocate(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  const auto effective_alignment = std::max(alignment, alignof(std::max_align_t));
  std::vector<void*> allocations;
  allocations.reserve(kBatch);
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      auto* value = ::operator new(size, std::align_val_t{effective_alignment});
      benchmark::DoNotOptimize(value);
      allocations.push_back(value);
    }
    for (auto* value : allocations) {
      ::operator delete(value, std::align_val_t{effective_alignment});
    }
    allocations.clear();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
}

void BmArenaFreshLifecycle(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  const auto alignment = static_cast<std::size_t>(state.range(1));
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    Arena<NewDeleteBlockSource, kBenchmarkOptions> arena;
    for (std::size_t index = 0; index < kBatch; ++index) {
      benchmark::DoNotOptimize(arena.Allocate(size, alignment));
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * size));
}

void BmArenaMixedStringLike(benchmark::State& state) {
  Arena<NewDeleteBlockSource, kBenchmarkOptions> arena;
  std::size_t used = 0;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < kBatch; ++index) {
      const auto size = kStringLikeSizes.at(index % kStringLikeSizes.size());
      benchmark::DoNotOptimize(arena.Allocate(size, 1));
      bytes += size;
    }
    benchmark::DoNotOptimize(bytes);
    used = arena.bytes_used();
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
  state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
  state.counters["blocks"] = static_cast<double>(arena.block_count());
  state.counters["used"] = static_cast<double>(used);
}

void BmArenaReset(benchmark::State& state) {
  Arena<NewDeleteBlockSource, kBenchmarkOptions> arena;
  for (std::size_t index = 0; index < kBatch; ++index) {
    benchmark::DoNotOptimize(arena.Allocate(64, 16));
  }
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    arena.Reset();
    benchmark::DoNotOptimize(arena.Allocate(64, 16));
  }
}

void BmFixedArenaExhaustion(benchmark::State& state) {
  alignas(64) std::array<std::byte, 4'096> storage{};
  Arena<FixedBlockSource, kFixedBenchmarkOptions> arena(FixedBlockSource(std::span<std::byte>(storage), 64));
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    arena.Reset();
    while (arena.TryAllocate(32, 16) != nullptr) {
      benchmark::ClobberMemory();
    }
  }
}

void RegisterAllocationBenchmarks() {
  constexpr std::array<std::int64_t, 7> kSizes = {8, 16, 32, 64, 256, 1'024, 4'096};
  constexpr std::array<std::int64_t, 4> kAlignments = {8, 16, 64, 256};
  for (const auto size : kSizes) {
    for (const auto alignment : kAlignments) {
      benchmark::RegisterBenchmark("Arena/Allocate", BmArenaAllocate)->Args({size, alignment});
      if (std::cmp_less_equal(alignment, alignof(std::max_align_t))) {
        benchmark::RegisterBenchmark("AllocatorArena/Allocate", BmAllocatorArenaAllocate)->Args({size, alignment});
      }
      benchmark::RegisterBenchmark("PmrArena/Allocate", BmPmrArenaAllocate)->Args({size, alignment});
      benchmark::RegisterBenchmark("PmrMonotonic/Allocate", BmPmrMonotonicAllocate)->Args({size, alignment});
      benchmark::RegisterBenchmark("NewDelete/Allocate", BmNewDeleteAllocate)->Args({size, alignment});
      benchmark::RegisterBenchmark("Arena/FreshLifecycle", BmArenaFreshLifecycle)->Args({size, alignment});
    }
  }
}

[[maybe_unused]] const bool kRegistered = [] {
  RegisterAllocationBenchmarks();
  return true;
}();

BENCHMARK(BmArenaReset);
BENCHMARK(BmFixedArenaExhaustion);
BENCHMARK(BmArenaMixedStringLike);

}  // namespace
}  // namespace mbo::memory

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::memory::AddBuildContext();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
