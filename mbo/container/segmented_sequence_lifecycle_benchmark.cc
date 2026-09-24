// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "mbo/container/internal/segmented_sequence_benchmark_context.h"
#include "mbo/container/segmented_sequence.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;
constexpr SegmentedSequenceOptions kSegment64{.segment_size = 64};
constexpr SegmentedSequenceOptions kSegment256{.segment_size = 256};

struct AllocationCounters final {
  std::size_t source_allocations = 0;
  std::size_t source_bytes = 0;
  std::size_t source_releases = 0;
  std::size_t source_released_bytes = 0;
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

  void Release(mbo::memory::MemoryBlock block) const noexcept {
    ++counters->source_releases;
    counters->source_released_bytes += block.size;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

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

template<typename Sequence>
void Fill(Sequence& sequence) {
  sequence.reserve(kElementCount);
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.unchecked_emplace_back(pos);
  }
}

void SetAllocationCounters(benchmark::State& state, const AllocationCounters& counters) {
  const auto iterations = static_cast<double>(state.iterations());
  state.counters["directory_allocations"] = static_cast<double>(counters.directory_allocations) / iterations;
  state.counters["directory_bytes"] = static_cast<double>(counters.directory_bytes) / iterations;
  state.counters["directory_deallocated_bytes"] =
      static_cast<double>(counters.directory_deallocated_bytes) / iterations;
  state.counters["directory_deallocations"] = static_cast<double>(counters.directory_deallocations) / iterations;
  state.counters["source_allocations"] = static_cast<double>(counters.source_allocations) / iterations;
  state.counters["source_bytes"] = static_cast<double>(counters.source_bytes) / iterations;
  state.counters["source_released_bytes"] = static_cast<double>(counters.source_released_bytes) / iterations;
  state.counters["source_releases"] = static_cast<double>(counters.source_releases) / iterations;
}

template<SegmentedSequenceOptions Options, bool Trim>
void BmPopRegrow(benchmark::State& state) {
  using Allocator = CountingDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<std::uint64_t, Options, CountingBlockSource, Allocator>;
  const auto depth = static_cast<std::size_t>(state.range(0));
  AllocationCounters counters;
  Sequence sequence(std::allocator_arg, Allocator(&counters), CountingBlockSource{.counters = &counters});
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
  const std::size_t low_segments = sequence.segment_count();
  regrow();
  counters = {};
  for (auto _ : state) {
    pop();
    regrow();
  }

  if (sequence.size() != kElementCount || sequence.back() != kElementCount - 1) {
    state.SkipWithError("pop/regrow did not restore the sequence");
    return;
  }
  state.counters["depth"] = static_cast<double>(depth);
  SetAllocationCounters(state, counters);
  state.counters["low_capacity"] = static_cast<double>(low_capacity);
  state.counters["low_reserved"] = static_cast<double>(low_reserved);
  state.counters["low_segments"] = static_cast<double>(low_segments);
  state.counters["restored_capacity"] = static_cast<double>(sequence.capacity());
  state.counters["restored_reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["restored_segments"] = static_cast<double>(sequence.segment_count());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(depth));
}

template<SegmentedSequenceOptions Options, bool Release>
void BmClearRegrow(benchmark::State& state) {
  using Allocator = CountingDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<std::uint64_t, Options, CountingBlockSource, Allocator>;
  AllocationCounters counters;
  Sequence sequence(std::allocator_arg, Allocator(&counters), CountingBlockSource{.counters = &counters});
  Fill(sequence);
  counters = {};

  for (auto _ : state) {
    if constexpr (Release) {
      sequence.release();
    } else {
      sequence.clear();
    }
    Fill(sequence);
  }
  SetAllocationCounters(state, counters);

  if (sequence.size() != kElementCount || sequence.back() != kElementCount - 1) {
    state.SkipWithError("clear/regrow did not restore the sequence");
    return;
  }
  state.counters["restored_capacity"] = static_cast<double>(sequence.capacity());
  state.counters["restored_reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["restored_segments"] = static_cast<double>(sequence.segment_count());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

#define REGISTER_LIFECYCLE(Label, Options)                                                 \
  BENCHMARK_TEMPLATE(BmPopRegrow, Options, false)                                          \
      ->Name("Lifecycle/Retained/" Label)                                                  \
      ->Arg((Options).segment_size)                                                        \
      ->Arg(4'096)                                                                         \
      ->Arg(16'384);                                                                       \
  BENCHMARK_TEMPLATE(BmPopRegrow, Options, true)                                           \
      ->Name("Lifecycle/Trimmed/" Label)                                                   \
      ->Arg((Options).segment_size)                                                        \
      ->Arg(4'096)                                                                         \
      ->Arg(16'384);                                                                       \
  BENCHMARK_TEMPLATE(BmClearRegrow, Options, false)->Name("Lifecycle/ClearRegrow/" Label); \
  BENCHMARK_TEMPLATE(BmClearRegrow, Options, true)->Name("Lifecycle/ReleaseRegrow/" Label)

REGISTER_LIFECYCLE("S64", kSegment64);
REGISTER_LIFECYCLE("S256", kSegment256);

#undef REGISTER_LIFECYCLE

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::container::container_internal::AddSegmentedSequenceBenchmarkContext("segmented-sequence-lifecycle-v2");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
