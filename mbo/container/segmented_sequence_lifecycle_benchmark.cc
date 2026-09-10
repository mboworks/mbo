// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "mbo/container/segmented_sequence.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;

constexpr SegmentedSequenceOptions kUniform64{
    .segment_capacities = {64},
    .listed_capacities = 1,
};
constexpr SegmentedSequenceOptions kListed{
    .segment_capacities = {64, 256, 1'024, 4'096},
    .listed_capacities = 4,
};

consteval SegmentedSequenceOptions WithRetentionLimits(
    SegmentedSequenceOptions options,
    std::size_t segment_limit,
    std::size_t byte_limit = std::numeric_limits<std::size_t>::max()) {
  options.retained_segment_limit = segment_limit;
  options.retained_byte_limit = byte_limit;
  return options;
}

constexpr auto kUniform64Eager = WithRetentionLimits(kUniform64, 0, 0);
constexpr auto kUniform64Count8 = WithRetentionLimits(kUniform64, 8);
constexpr auto kUniform64Bytes32K = WithRetentionLimits(kUniform64, std::numeric_limits<std::size_t>::max(), 32'768);
constexpr auto kUniform64Bytes128K = WithRetentionLimits(kUniform64, std::numeric_limits<std::size_t>::max(), 131'072);
constexpr auto kListedEager = WithRetentionLimits(kListed, 0, 0);
constexpr auto kListedCount1 = WithRetentionLimits(kListed, 1);

template<SegmentedSequenceOptions Options>
void Fill(SegmentedSequence<std::uint64_t, Options>& sequence) {
  sequence.reserve(kElementCount);
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.unchecked_emplace_back(pos);
  }
}

template<SegmentedSequenceOptions Options, bool Trim>
void BmPopRegrow(benchmark::State& state) {
  const auto depth = static_cast<std::size_t>(state.range(0));
  SegmentedSequence<std::uint64_t, Options> sequence;
  Fill(sequence);

  const auto pop = [&] {
    for (std::size_t count = 0; count < depth; ++count) {
      sequence.pop_back();
    }
    if constexpr (Trim) {
      sequence.trim_capacity();
    }
  };
  const auto regrow = [&] {
    benchmark::ClobberMemory();
    const std::size_t first = sequence.size();
    for (std::size_t count = 0; count < depth; ++count) {
      benchmark::DoNotOptimize(sequence.emplace_back(first + count));
    }
  };

  pop();
  const std::size_t low_capacity = sequence.capacity();
  const std::size_t low_reserved = sequence.bytes_reserved();
  const std::size_t low_retained = sequence.retained_bytes();
  const std::size_t low_segments = sequence.segment_count();
  regrow();
  for (auto _ : state) {
    pop();
    regrow();
  }

  if (sequence.size() != kElementCount || sequence.back() != kElementCount - 1) {
    state.SkipWithError("pop/regrow did not restore the sequence");
    return;
  }
  state.counters["depth"] = static_cast<double>(depth);
  state.counters["directory"] = static_cast<double>(sequence.directory_bytes_reserved());
  state.counters["segment_directory"] = static_cast<double>(sequence.segment_directory_bytes_reserved());
  state.counters["low_capacity"] = static_cast<double>(low_capacity);
  state.counters["low_reserved"] = static_cast<double>(low_reserved);
  state.counters["low_retained"] = static_cast<double>(low_retained);
  state.counters["low_segments"] = static_cast<double>(low_segments);
  state.counters["restored_capacity"] = static_cast<double>(sequence.capacity());
  state.counters["restored_reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["restored_segments"] = static_cast<double>(sequence.segment_count());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(depth));
}

#define REGISTER_LIFECYCLE(Label, Options)        \
  BENCHMARK_TEMPLATE(BmPopRegrow, Options, false) \
      ->Name("Lifecycle/Retained/" Label)         \
      ->Arg(64)                                   \
      ->Arg(4'096)                                \
      ->Arg(16'384);                              \
  BENCHMARK_TEMPLATE(BmPopRegrow, Options, true)->Name("Lifecycle/Trimmed/" Label)->Arg(64)->Arg(4'096)->Arg(16'384)

REGISTER_LIFECYCLE("Uniform64", kUniform64);
REGISTER_LIFECYCLE("Listed", kListed);

#define REGISTER_LIMITED_LIFECYCLE(Label, Options) \
  BENCHMARK_TEMPLATE(BmPopRegrow, Options, false)->Name("Lifecycle/Limited/" Label)->Arg(64)->Arg(4'096)->Arg(16'384)

REGISTER_LIMITED_LIFECYCLE("Uniform64Eager", kUniform64Eager);
REGISTER_LIMITED_LIFECYCLE("Uniform64Count8", kUniform64Count8);
REGISTER_LIMITED_LIFECYCLE("Uniform64Bytes32K", kUniform64Bytes32K);
REGISTER_LIMITED_LIFECYCLE("Uniform64Bytes128K", kUniform64Bytes128K);
REGISTER_LIMITED_LIFECYCLE("ListedEager", kListedEager);
REGISTER_LIMITED_LIFECYCLE("ListedCount1", kListedCount1);

#undef REGISTER_LIMITED_LIFECYCLE

#undef REGISTER_LIFECYCLE

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
#if defined(__clang__)
  benchmark::AddCustomContext("compiler", std::string("clang-") + std::to_string(__clang_major__));
  benchmark::AddCustomContext("compiler_version", __clang_version__);
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler", std::string("gcc-") + std::to_string(__GNUC__));
  benchmark::AddCustomContext("compiler_version", __VERSION__);
#endif
#if __cplusplus >= 202'302L
  benchmark::AddCustomContext("cxx_standard", "c++23");
#else
  benchmark::AddCustomContext("cxx_standard", "c++20");
#endif
  benchmark::AddCustomContext("experiment", "segmented-sequence-lifecycle-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
