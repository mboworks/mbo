// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
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
    SetMemoryCounters(state, sequence);
  }
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
  for (auto _ : state) {
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
  for (auto _ : state) {
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
  for (auto _ : state) {
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
    state.counters["capacity"] = static_cast<double>(sequence.capacity());
    state.counters["reserved"] = static_cast<double>(sequence.capacity() * sizeof(std::uint64_t));
  }
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
BENCHMARK(BmDequeAppendFresh)->Name("Deque/AppendFresh");

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

BENCHMARK_MAIN();
