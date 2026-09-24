// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "mbo/container/internal/segmented_sequence_benchmark_context.h"
#include "mbo/container/segmented_sequence.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;

constexpr SegmentedSequenceOptions kUniform64{
    .segment_size = 64,
};
constexpr SegmentedSequenceOptions kUniform256{
    .segment_size = 256,
};
constexpr SegmentedSequenceOptions kUniform1024{
    .segment_size = 1'024,
};
constexpr SegmentedSequenceOptions kFinite256Reservation0{
    .segment_size = 256,
    .segment_capacity = kElementCount / 256,
    .segment_reservation = 0,
};
constexpr SegmentedSequenceOptions kFinite256Reservation1{
    .segment_size = 256,
    .segment_capacity = kElementCount / 256,
    .segment_reservation = 1,
};
constexpr SegmentedSequenceOptions kFinite256Reservation64{
    .segment_size = 256,
    .segment_capacity = kElementCount / 256,
    .segment_reservation = kElementCount / 256,
};

struct AllocationCounters final {
  std::size_t source_allocations = 0;
  std::size_t source_bytes = 0;
  std::size_t directory_allocations = 0;
  std::size_t directory_bytes = 0;
  std::size_t directory_deallocations = 0;
  std::size_t directory_deallocated_bytes = 0;
};

// NOLINTBEGIN(readability-identifier-naming): adapters model BlockSource and Allocator spelling.
struct CountingBlockSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const noexcept {
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      ++counters->source_allocations;
      counters->source_bytes += block->size;
    }
    return block;
  }

  static void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

  AllocationCounters* counters;
};

template<typename T>
struct CountingDirectoryAllocator final {
  using value_type = T;

  template<typename U>
  struct rebind final {
    using other = CountingDirectoryAllocator<U>;
  };

  constexpr CountingDirectoryAllocator() noexcept = default;

  constexpr explicit CountingDirectoryAllocator(AllocationCounters* counters) noexcept : counters(counters) {}

  template<typename U>
  constexpr explicit CountingDirectoryAllocator(const CountingDirectoryAllocator<U>& other) noexcept
      : counters(other.counters) {}

  T* allocate(std::size_t count) {
    ++counters->directory_allocations;
    counters->directory_bytes += count * sizeof(T);
    return std::allocator<T>{}.allocate(count);
  }

  void deallocate(T* data, std::size_t count) noexcept {
    ++counters->directory_deallocations;
    counters->directory_deallocated_bytes += count * sizeof(T);
    std::allocator<T>{}.deallocate(data, count);
  }

  template<typename U>
  friend constexpr bool operator==(
      const CountingDirectoryAllocator& lhs,
      const CountingDirectoryAllocator<U>& rhs) noexcept {
    return lhs.counters == rhs.counters;
  }

  template<typename>
  friend struct CountingDirectoryAllocator;

  AllocationCounters* counters = nullptr;
};

// NOLINTEND(readability-identifier-naming)

template<SegmentedSequenceOptions Options, mbo::memory::BlockSource Source, typename DirectoryAllocator>
void SetMemoryCounters(
    benchmark::State& state,
    const SegmentedSequence<std::uint64_t, Options, Source, DirectoryAllocator>& sequence) {
  state.counters["capacity"] = static_cast<double>(sequence.capacity());
  state.counters["reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["segments"] = static_cast<double>(sequence.segment_count());
}

template<SegmentedSequenceOptions Options>
void BmGrowthBoundary(benchmark::State& state) {
  using Allocator = CountingDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<std::uint64_t, Options, CountingBlockSource, Allocator>;
  constexpr std::size_t kPreparedElements = 2 * Options.segment_size;
  for (auto _ : state) {
    state.PauseTiming();
    {
      AllocationCounters counters;
      Sequence sequence(std::allocator_arg, Allocator(&counters), CountingBlockSource{.counters = &counters});
      sequence.resize(kPreparedElements, 1);
      counters = {};
      state.ResumeTiming();
      benchmark::DoNotOptimize(sequence.emplace_back(1));
      benchmark::ClobberMemory();
      state.PauseTiming();
      SetMemoryCounters(state, sequence);
      state.counters["growth_directory_allocations"] = static_cast<double>(counters.directory_allocations);
      state.counters["growth_directory_bytes"] = static_cast<double>(counters.directory_bytes);
      state.counters["growth_directory_deallocated_bytes"] = static_cast<double>(counters.directory_deallocated_bytes);
      state.counters["growth_directory_deallocations"] = static_cast<double>(counters.directory_deallocations);
      state.counters["growth_source_allocations"] = static_cast<double>(counters.source_allocations);
      state.counters["growth_source_bytes"] = static_cast<double>(counters.source_bytes);
    }
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations());
}

template<SegmentedSequenceOptions Options>
void BmFreshConstructAppendDestroy(benchmark::State& state) {
  std::size_t capacity = 0;
  std::size_t reserved = 0;
  std::size_t segments = 0;
  for (auto _ : state) {
    SegmentedSequence<std::uint64_t, Options> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.emplace_back(pos);
    }
    benchmark::DoNotOptimize(sequence);
    benchmark::ClobberMemory();
    capacity = sequence.capacity();
    reserved = sequence.bytes_reserved();
    segments = sequence.segment_count();
  }
  state.counters["capacity"] = static_cast<double>(capacity);
  state.counters["reserved"] = static_cast<double>(reserved);
  state.counters["segments"] = static_cast<double>(segments);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options>
void BmRetainedAppendClear(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.reserve(kElementCount);
  for (auto _ : state) {
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.unchecked_emplace_back(pos);
    }
    benchmark::DoNotOptimize(sequence);
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
  benchmark::ClobberMemory();
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
void BmIteratorForward(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.resize(kElementCount, 1);
  benchmark::ClobberMemory();
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
void BmIteratorArithmetic(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.resize(kElementCount, 1);
  benchmark::ClobberMemory();
  for (auto _ : state) {
    std::uint64_t sum = 0;
    const auto begin = sequence.cbegin();
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sum += *(begin + static_cast<std::ptrdiff_t>(pos));
    }
    benchmark::DoNotOptimize(sum);
  }
  SetMemoryCounters(state, sequence);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<SegmentedSequenceOptions Options>
void BmIteratorReverse(benchmark::State& state) {
  SegmentedSequence<std::uint64_t, Options> sequence;
  sequence.resize(kElementCount, 1);
  const auto& values = sequence;
  benchmark::ClobberMemory();
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator) {
      sum += *iterator;
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
  benchmark::ClobberMemory();
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

void BmVectorFreshConstructAppendDestroy(benchmark::State& state) {
  std::size_t capacity = 0;
  for (auto _ : state) {
    std::vector<std::uint64_t> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      // Growth is the operation under measurement, so pre-reserving would invalidate the case.
      sequence.push_back(pos);  // NOLINT(performance-inefficient-vector-operation)
    }
    benchmark::DoNotOptimize(sequence);
    benchmark::ClobberMemory();
    capacity = sequence.capacity();
  }
  state.counters["capacity"] = static_cast<double>(capacity);
  state.counters["reserved"] = static_cast<double>(capacity * sizeof(std::uint64_t));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

void BmDequeFreshConstructAppendDestroy(benchmark::State& state) {
  for (auto _ : state) {
    std::deque<std::uint64_t> sequence;
    for (std::size_t pos = 0; pos < kElementCount; ++pos) {
      sequence.push_back(pos);
    }
    benchmark::DoNotOptimize(sequence);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

#define REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS(Label, Options)                                              \
  BENCHMARK_TEMPLATE(BmFreshConstructAppendDestroy, Options)                                                \
      ->Name("SegmentedSequence/FreshConstructAppendDestroy/" Label);                                       \
  BENCHMARK_TEMPLATE(BmRetainedAppendClear, Options)->Name("SegmentedSequence/RetainedAppendClear/" Label); \
  BENCHMARK_TEMPLATE(BmIndexedLookup, Options, false)->Name("SegmentedSequence/Indexed/" Label);            \
  BENCHMARK_TEMPLATE(BmIndexedLookup, Options, true)->Name("SegmentedSequence/IndexedPermuted/" Label);     \
  BENCHMARK_TEMPLATE(BmIteratorForward, Options)->Name("SegmentedSequence/IteratorForward/" Label);         \
  BENCHMARK_TEMPLATE(BmIteratorArithmetic, Options)->Name("SegmentedSequence/IteratorArithmetic/" Label);   \
  BENCHMARK_TEMPLATE(BmIteratorReverse, Options)->Name("SegmentedSequence/IteratorReverse/" Label);         \
  BENCHMARK_TEMPLATE(BmSegmentLookup, Options)->Name("SegmentedSequence/Segments/" Label)

REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform64", kUniform64);
REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform256", kUniform256);
REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS("Uniform1024", kUniform1024);

#undef REGISTER_SEGMENTED_SEQUENCE_BENCHMARKS

BENCHMARK_TEMPLATE(BmFreshConstructAppendDestroy, kFinite256Reservation0)
    ->Name("SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation0");
BENCHMARK_TEMPLATE(BmFreshConstructAppendDestroy, kFinite256Reservation1)
    ->Name("SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation1");
BENCHMARK_TEMPLATE(BmFreshConstructAppendDestroy, kFinite256Reservation64)
    ->Name("SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation64");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kFinite256Reservation0)
    ->Name("SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation0/Cross2To4");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kFinite256Reservation1)
    ->Name("SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation1/Cross2To4");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kFinite256Reservation64)
    ->Name("SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation64/Preallocated");
BENCHMARK(BmVectorFreshConstructAppendDestroy)->Name("Vector/FreshConstructAppendDestroy");
BENCHMARK(BmDequeFreshConstructAppendDestroy)->Name("Deque/FreshConstructAppendDestroy");

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::container::container_internal::AddSegmentedSequenceBenchmarkContext("segmented-sequence-v2");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
