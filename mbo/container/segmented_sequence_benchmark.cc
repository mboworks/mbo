// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <vector>

#include "mbo/container/segmented_sequence.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;

constexpr SegmentedSequenceOptions kUniform64{
    .segment_capacities = {64},
    .listed_capacities = 1,
};
constexpr SegmentedSequenceOptions kUniform256{
    .segment_capacities = {256},
    .listed_capacities = 1,
};
constexpr SegmentedSequenceOptions kUniform1024{
    .segment_capacities = {1'024},
    .listed_capacities = 1,
};
constexpr SegmentedSequenceOptions kListed{
    .segment_capacities = {64, 256, 1'024, 4'096},
    .listed_capacities = 4,
};

template<SegmentedSequenceOptions Options>
void SetMemoryCounters(benchmark::State& state, const SegmentedSequence<std::uint64_t, Options>& sequence) {
  state.counters["capacity"] = static_cast<double>(sequence.capacity());
  state.counters["directory"] = static_cast<double>(sequence.directory_bytes_reserved());
  state.counters["reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["segments"] = static_cast<double>(sequence.segment_count());
}

template<SegmentedSequenceOptions Options>
void BmAppendFresh(benchmark::State& state) {
  for (auto _ : state) {
    SegmentedSequence<std::uint64_t, Options> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      benchmark::DoNotOptimize(sequence.emplace_back(pos));
    }
    benchmark::DoNotOptimize(sequence);
  }
  // Reproduce growth outside the timed loop for cold memory accounting.
  SegmentedSequence<std::uint64_t, Options> memory_sample;
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    memory_sample.emplace_back(pos);
  }
  SetMemoryCounters(state, memory_sample);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options>
void BmAppendRetained(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.reserve(kElementCount);
  for (auto _ : state) {
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      benchmark::DoNotOptimize(sequence.unchecked_emplace_back(pos));
    }
    benchmark::ClobberMemory();
    sequence.clear();
  }
  SetMemoryCounters(state, sequence);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options, bool Permuted>
void BmIndexedLookup(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.reserve(kElementCount);
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.unchecked_emplace_back(pos);
  }
  benchmark::DoNotOptimize(sequence);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < kElementCount; ++ordinal) {
      const std::size_t pos = Permuted ? (ordinal * 40'503) & (kElementCount - 1) : ordinal;
      sum += sequence[pos];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    benchmark::DoNotOptimize(sum);
  }
  SetMemoryCounters(state, sequence);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options>
void BmIteratorLookup(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.resize(kElementCount, 1);
  benchmark::DoNotOptimize(sequence);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (const std::uint64_t value : sequence) {
      sum += value;
    }
    benchmark::DoNotOptimize(sum);
  }
  SetMemoryCounters(state, sequence);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options>
void BmSegmentLookup(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.resize(kElementCount, 1);
  benchmark::DoNotOptimize(sequence);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (const auto segment : sequence.segments()) {
      for (const std::uint64_t value : segment) {
        sum += value;
      }
    }
    benchmark::DoNotOptimize(sum);
  }
  SetMemoryCounters(state, sequence);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

void BmVectorAppendFresh(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      // Growth is the operation under measurement, so pre-reserving would invalidate the case.
      sequence.push_back(pos);  // NOLINT(performance-inefficient-vector-operation)
    }
    benchmark::DoNotOptimize(sequence);
  }
  std::vector<std::uint64_t> memory_sample;
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    memory_sample.push_back(pos);  // NOLINT(performance-inefficient-vector-operation): Reproduce measured growth.
  }
  state.counters["capacity"] = static_cast<double>(memory_sample.capacity());
  state.counters["reserved"] = static_cast<double>(memory_sample.capacity() * sizeof(std::uint64_t));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

void BmDequeAppendFresh(benchmark::State& state) {
  for (auto _ : state) {
    std::deque<std::uint64_t> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.push_back(pos);
    }
    benchmark::DoNotOptimize(sequence);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

void BmVectorAppendRetained(benchmark::State& state) {
  std::vector<std::uint64_t> sequence;
  sequence.reserve(kElementCount);
  for (auto _ : state) {
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.push_back(pos);
    }
    benchmark::ClobberMemory();
    sequence.clear();
  }
  state.counters["capacity"] = static_cast<double>(sequence.capacity());
  state.counters["reserved"] = static_cast<double>(sequence.capacity() * sizeof(std::uint64_t));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

void BmListAppendFresh(benchmark::State& state) {
  for (auto _ : state) {
    std::list<std::uint64_t> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.push_back(pos);
    }
    benchmark::DoNotOptimize(sequence);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<typename Sequence>
void BmStandardIteratorLookup(benchmark::State& state) {
  Sequence sequence(kElementCount, 1);
  benchmark::DoNotOptimize(sequence);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (const std::uint64_t value : sequence) {
      sum += value;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<typename Sequence, bool Permuted>
void BmStandardIndexedLookup(benchmark::State& state) {
  Sequence sequence;
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.push_back(pos);  // NOLINT(performance-inefficient-vector-operation): Untimed fixture setup.
  }
  benchmark::DoNotOptimize(sequence);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < kElementCount; ++ordinal) {
      const std::size_t pos = Permuted ? (ordinal * 40'503) & (kElementCount - 1) : ordinal;
      sum += sequence[pos];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Measured indexed
                             // access.
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<typename Sequence>
void BmPushPopCycle(benchmark::State& state) {
  Sequence sequence;
  if constexpr (requires { sequence.reserve(kElementCount); }) {
    sequence.reserve(kElementCount);
  }
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.emplace_back(pos);
  }
  for (std::size_t remaining = kElementCount; remaining > 0; --remaining) {
    if (sequence.size() != remaining || sequence.back() != remaining - 1) {
      state.SkipWithError("push/pop preflight disagrees with reverse element order");
      return;
    }
    sequence.pop_back();
  }
  for (auto _ : state) {
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      benchmark::DoNotOptimize(sequence.emplace_back(pos));
    }
    benchmark::ClobberMemory();
    while (!sequence.empty()) {
      auto value = sequence.back();
      benchmark::DoNotOptimize(value);
      sequence.pop_back();
    }
  }
  if constexpr (requires { sequence.bytes_reserved(); }) {
    SetMemoryCounters(state, sequence);
  } else if constexpr (requires { sequence.capacity(); }) {
    state.counters["capacity"] = static_cast<double>(sequence.capacity());
    state.counters["reserved"] = static_cast<double>(sequence.capacity() * sizeof(std::uint64_t));
  }
  state.counters["operations_per_iteration"] = static_cast<double>(2 * kElementCount);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(2 * kElementCount));
}

#define REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS(Label, Options)                                          \
  BENCHMARK_TEMPLATE(BmAppendFresh, Options)->Name("SegmentedSequence/AppendFresh/" Label);             \
  BENCHMARK_TEMPLATE(BmAppendRetained, Options)->Name("SegmentedSequence/AppendRetained/" Label);       \
  BENCHMARK_TEMPLATE(BmIndexedLookup, Options, false)->Name("SegmentedSequence/Indexed/" Label);        \
  BENCHMARK_TEMPLATE(BmIndexedLookup, Options, true)->Name("SegmentedSequence/IndexedPermuted/" Label); \
  BENCHMARK_TEMPLATE(BmIteratorLookup, Options)->Name("SegmentedSequence/Iterator/" Label);             \
  BENCHMARK_TEMPLATE(BmSegmentLookup, Options)->Name("SegmentedSequence/Segments/" Label)

REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform64", kUniform64);
REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform256", kUniform256);
REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform1024", kUniform1024);
REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Listed", kListed);

#undef REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS

BENCHMARK(BmVectorAppendFresh)->Name("Vector/AppendFresh");
BENCHMARK(BmVectorAppendRetained)->Name("Vector/AppendRetained");
BENCHMARK(BmDequeAppendFresh)->Name("Deque/AppendFresh");
BENCHMARK(BmListAppendFresh)->Name("List/AppendFresh");
BENCHMARK_TEMPLATE(BmStandardIteratorLookup, std::vector<std::uint64_t>)->Name("Vector/Iterator");
BENCHMARK_TEMPLATE(BmStandardIteratorLookup, std::deque<std::uint64_t>)->Name("Deque/Iterator");
BENCHMARK_TEMPLATE(BmStandardIteratorLookup, std::list<std::uint64_t>)->Name("List/Iterator");
BENCHMARK_TEMPLATE(BmStandardIndexedLookup, std::vector<std::uint64_t>, false)->Name("Vector/Indexed");
BENCHMARK_TEMPLATE(BmStandardIndexedLookup, std::vector<std::uint64_t>, true)->Name("Vector/IndexedPermuted");
BENCHMARK_TEMPLATE(BmStandardIndexedLookup, std::deque<std::uint64_t>, false)->Name("Deque/Indexed");
BENCHMARK_TEMPLATE(BmStandardIndexedLookup, std::deque<std::uint64_t>, true)->Name("Deque/IndexedPermuted");
BENCHMARK_TEMPLATE(BmPushPopCycle, SegmentedSequence<std::uint64_t, kUniform64>)
    ->Name("SegmentedSequence/PushPopCycle/Uniform64");
BENCHMARK_TEMPLATE(BmPushPopCycle, SegmentedSequence<std::uint64_t, kUniform256>)
    ->Name("SegmentedSequence/PushPopCycle/Uniform256");
BENCHMARK_TEMPLATE(BmPushPopCycle, SegmentedSequence<std::uint64_t, kUniform1024>)
    ->Name("SegmentedSequence/PushPopCycle/Uniform1024");
BENCHMARK_TEMPLATE(BmPushPopCycle, SegmentedSequence<std::uint64_t, kListed>)
    ->Name("SegmentedSequence/PushPopCycle/Listed");
BENCHMARK_TEMPLATE(BmPushPopCycle, std::vector<std::uint64_t>)->Name("Vector/PushPopCycle");
BENCHMARK_TEMPLATE(BmPushPopCycle, std::deque<std::uint64_t>)->Name("Deque/PushPopCycle");
BENCHMARK_TEMPLATE(BmPushPopCycle, std::list<std::uint64_t>)->Name("List/PushPopCycle");

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
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
