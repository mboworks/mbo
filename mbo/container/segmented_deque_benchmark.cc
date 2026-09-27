// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>

#include "mbo/container/internal/segmented_benchmark_context.h"
#include "mbo/container/segmented_deque.h"
#include "mbo/container/segmented_options.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): measured indexed access.

constexpr SegmentedOptions kS64{.segment_size = 64};
constexpr SegmentedOptions kS256{.segment_size = 256};
constexpr SegmentedOptions kS1024{.segment_size = 1'024};
constexpr SegmentedOptions kS256Reserved{.segment_size = 256, .segment_reservation = 128};
constexpr std::size_t kWindow = 16'384;
constexpr std::size_t kBatch = 256;

struct alignas(64) CacheLine final {
  explicit CacheLine(std::uint64_t input = 0) : value(input) {}

  std::uint64_t value = 0;
  std::array<std::byte, 56> padding{};
};

template<typename T>
std::uint64_t Value(const T& value) {
  if constexpr (std::same_as<T, CacheLine>) {
    return value.value;
  } else {
    return value;
  }
}

template<typename Sequence>
void SetStorageCounters(benchmark::State& state, const Sequence& sequence) {
  if constexpr (requires { sequence.segment_count(); }) {
    state.counters["segments"] = static_cast<double>(sequence.segment_count());
    state.counters["capacity"] = static_cast<double>(sequence.capacity());
    state.counters["source_bytes_reserved"] = static_cast<double>(sequence.bytes_reserved());
  }
  state.counters["element_size"] = sizeof(typename Sequence::value_type);
  state.counters["element_alignment"] = alignof(typename Sequence::value_type);
  state.counters["container_bytes"] = sizeof(Sequence);
}

enum class Access { kIndexed, kPermuted, kForward, kReverse, kArithmetic, kSegments };

template<Access Mode, typename Sequence>
std::uint64_t ReadPass(const Sequence& sequence, std::size_t count) {
  std::uint64_t sum = 0;
  if constexpr (Mode == Access::kForward) {
    for (const auto& value : sequence) {
      sum += Value(value);
    }
  } else if constexpr (Mode == Access::kReverse) {
    for (auto iterator = sequence.rbegin(); iterator != sequence.rend(); ++iterator) {
      sum += Value(*iterator);
    }
  } else if constexpr (Mode == Access::kSegments) {
    for (auto segment : sequence.segments()) {
      for (const auto& value : segment) {
        sum += Value(value);
      }
    }
  } else {
    for (std::size_t index = 0; index < count; ++index) {
      // All registered counts are powers of two; the odd multiplier visits every element.
      const std::size_t position = Mode == Access::kIndexed ? index : (index * 8'191) & (count - 1);
      if constexpr (Mode == Access::kArithmetic) {
        sum += Value(*(sequence.begin() + static_cast<std::ptrdiff_t>(position)));
      } else {
        sum += Value(sequence[position]);
      }
    }
  }
  return sum;
}

template<typename Sequence, Access Mode, std::size_t Offset = 0>
void BmRead(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  Sequence sequence;
  for (std::size_t index = 0; index < count + Offset; ++index) {
    sequence.emplace_back(index);
  }
  if constexpr (Offset != 0) {
    for (std::size_t index = 0; index < Offset; ++index) {
      sequence.pop_front();
    }
  }
  const auto expected = (static_cast<std::uint64_t>(count) * (count - 1) / 2) + (Offset * count);
  if (ReadPass<Mode>(sequence, count) != expected) {
    state.SkipWithError("traversal did not consume the expected logical values");
    return;
  }
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = ReadPass<Mode>(sequence, count);
    benchmark::DoNotOptimize(sum);
  }
  SetStorageCounters(state, sequence);
  state.counters["initial_offset"] = Offset;
  state.counters["work_items"] = static_cast<double>(count);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Sequence, bool Front>
void BmFresh(benchmark::State& state) {
  for (auto _ : state) {
    Sequence sequence;
    for (std::size_t index = 0; index < kWindow; ++index) {
      if constexpr (Front) {
        sequence.emplace_front(index);
      } else {
        sequence.emplace_back(index);
      }
    }
    benchmark::DoNotOptimize(sequence);
    benchmark::ClobberMemory();
  }
  state.counters["work_items"] = static_cast<double>(kWindow);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kWindow));
}

template<typename Sequence, bool Reverse>
void BmQueue(benchmark::State& state) {
  Sequence sequence;
  sequence.resize(kWindow, 1);
  // Warm one complete traversal of the window before measuring steady reuse.
  for (std::size_t index = 0; index < kWindow; ++index) {
    if constexpr (Reverse) {
      sequence.pop_back();
      sequence.emplace_front(index);
    } else {
      sequence.pop_front();
      sequence.emplace_back(index);
    }
  }
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      if constexpr (Reverse) {
        benchmark::DoNotOptimize(sequence.back());
        sequence.pop_back();
        sequence.emplace_front(index);
      } else {
        benchmark::DoNotOptimize(sequence.front());
        sequence.pop_front();
        sequence.emplace_back(index);
      }
    }
    benchmark::ClobberMemory();
  }
  SetStorageCounters(state, sequence);
  state.counters["work_items"] = static_cast<double>(kBatch);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
}

// Count both segment and directory requests to the same resource. The upstream monotonic
// resource uses caller-owned bytes and has no fallback, so allocation-free reuse is observable.
struct AllocationCounts final {
  std::size_t allocations = 0;
  std::size_t allocated_bytes = 0;
  std::size_t deallocations = 0;
};

class CountingResource final : public std::pmr::memory_resource {
 public:
  explicit CountingResource(std::pmr::memory_resource& upstream) noexcept : upstream_(upstream) {}

  const AllocationCounts& GetCounters() const noexcept { return counts_; }

  void ResetCounters() noexcept { counts_ = {}; }

 private:
  // NOLINTNEXTLINE(readability-identifier-naming): overrides std::pmr.
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    void* const result = upstream_.allocate(bytes, alignment);
    ++counts_.allocations;
    counts_.allocated_bytes += bytes;
    return result;
  }

  // NOLINTNEXTLINE(readability-identifier-naming): overrides std::pmr.
  void do_deallocate(void* data, std::size_t bytes, std::size_t alignment) override {
    ++counts_.deallocations;
    upstream_.deallocate(data, bytes, alignment);
  }

  // NOLINTNEXTLINE(readability-identifier-naming): overrides std::pmr.
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }

  AllocationCounts counts_;
  std::pmr::memory_resource& upstream_;
};

template<SegmentedOptions Options, bool Reverse>
void BmArenaQueue(benchmark::State& state) {
  std::array<std::byte, 512 * 1'024> storage{};
  std::pmr::monotonic_buffer_resource arena(storage.data(), storage.size(), std::pmr::null_memory_resource());
  CountingResource resource(arena);
  using Allocator = std::pmr::polymorphic_allocator<std::byte>;
  using Queue = SegmentedDeque<std::uint64_t, Options, mbo::memory::PmrBlockSource, Allocator>;
  Queue sequence(std::allocator_arg, Allocator(&resource), mbo::memory::PmrBlockSource(&resource));
  sequence.resize(kWindow, 1);
  sequence.reserve_back(Options.segment_size);
  const auto warm_allocations = resource.GetCounters().allocations;
  const auto warm_bytes = resource.GetCounters().allocated_bytes;
  const auto warm_deallocations = resource.GetCounters().deallocations;
  for (auto _ : state) {
    for (std::size_t index = 0; index < kBatch; ++index) {
      if constexpr (Reverse) {
        benchmark::DoNotOptimize(sequence.back());
        sequence.pop_back();
        sequence.emplace_front(index);
      } else {
        benchmark::DoNotOptimize(sequence.front());
        sequence.pop_front();
        sequence.emplace_back(index);
      }
    }
    benchmark::ClobberMemory();
  }
  SetStorageCounters(state, sequence);
  state.counters["warm_allocations"] = static_cast<double>(warm_allocations);
  state.counters["warm_allocated_bytes"] = static_cast<double>(warm_bytes);
  state.counters["timed_allocations"] = static_cast<double>(resource.GetCounters().allocations - warm_allocations);
  state.counters["timed_deallocations"] =
      static_cast<double>(resource.GetCounters().deallocations - warm_deallocations);
  if (resource.GetCounters().allocations != warm_allocations
      || resource.GetCounters().deallocations != warm_deallocations) {
    state.SkipWithError("steady arena queue performed storage operations");
  }
  state.counters["work_items"] = static_cast<double>(kBatch);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
}

template<SegmentedOptions Options, bool Front>
void BmGrowthBoundary(benchmark::State& state) {
  using Allocator = std::pmr::polymorphic_allocator<std::byte>;
  using Sequence = SegmentedDeque<std::uint64_t, Options, mbo::memory::PmrBlockSource, Allocator>;
  for (auto _ : state) {
    state.PauseTiming();
    {
      CountingResource resource(*std::pmr::new_delete_resource());
      Sequence sequence(std::allocator_arg, Allocator(&resource), mbo::memory::PmrBlockSource(&resource));
      for (std::size_t index = 0; index < 2 * Options.segment_size; ++index) {
        if constexpr (Front) {
          sequence.emplace_front(index);
        } else {
          sequence.emplace_back(index);
        }
      }
      resource.ResetCounters();
      state.ResumeTiming();
      if constexpr (Front) {
        benchmark::DoNotOptimize(sequence.emplace_front(42));
      } else {
        benchmark::DoNotOptimize(sequence.emplace_back(42));
      }
      benchmark::ClobberMemory();
      state.PauseTiming();
      SetStorageCounters(state, sequence);
      state.counters["event_allocations"] = static_cast<double>(resource.GetCounters().allocations);
      state.counters["event_allocated_bytes"] = static_cast<double>(resource.GetCounters().allocated_bytes);
      state.counters["event_deallocations"] = static_cast<double>(resource.GetCounters().deallocations);
    }
    state.ResumeTiming();
  }
  state.counters["work_items"] = 1;
  state.SetItemsProcessed(state.iterations());
}

template<typename Sequence, bool Release>
void BmLifecycle(benchmark::State& state) {
  Sequence sequence;
  sequence.resize(kWindow, 1);
  for (auto _ : state) {
    if constexpr (Release) {
      sequence.release();
    } else {
      sequence.clear();
    }
    for (std::size_t index = 0; index < kWindow; ++index) {
      sequence.emplace_back(index);
    }
    benchmark::DoNotOptimize(sequence);
    benchmark::ClobberMemory();
  }
  SetStorageCounters(state, sequence);
  state.counters["work_items"] = static_cast<double>(kWindow);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kWindow));
}

using D64 = SegmentedDeque<std::uint64_t, kS64>;
using D256 = SegmentedDeque<std::uint64_t, kS256>;
using D1024 = SegmentedDeque<std::uint64_t, kS1024>;
using V64 = SegmentedVector<std::uint64_t, kS64>;
using V256 = SegmentedVector<std::uint64_t, kS256>;
using V1024 = SegmentedVector<std::uint64_t, kS1024>;
using StdDeque = std::deque<std::uint64_t>;
using DLine = SegmentedDeque<CacheLine, kS256>;
using VLine = SegmentedVector<CacheLine, kS256>;
using StdLine = std::deque<CacheLine>;

BENCHMARK_TEMPLATE(BmRead, D64, Access::kIndexed)
    ->Name("SegmentedDeque/S64/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kPermuted)
    ->Name("SegmentedDeque/S64/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kForward)->Name("SegmentedDeque/S64/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kReverse)->Name("SegmentedDeque/S64/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kArithmetic)->Name("SegmentedDeque/S64/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kSegments)->Name("SegmentedDeque/S64/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D64, Access::kPermuted, 31)->Name("SegmentedDeque/S64/Permuted/Offset31")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, D64, false)->Name("SegmentedDeque/S64/FreshBack");
BENCHMARK_TEMPLATE(BmFresh, D64, true)->Name("SegmentedDeque/S64/FreshFront");
BENCHMARK_TEMPLATE(BmQueue, D64, false)->Name("SegmentedDeque/S64/QueueForward");
BENCHMARK_TEMPLATE(BmQueue, D64, true)->Name("SegmentedDeque/S64/QueueReverse");
BENCHMARK_TEMPLATE(BmLifecycle, D64, false)->Name("SegmentedDeque/S64/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, D64, true)->Name("SegmentedDeque/S64/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, D256, Access::kIndexed)
    ->Name("SegmentedDeque/S256/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kPermuted)
    ->Name("SegmentedDeque/S256/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kForward)->Name("SegmentedDeque/S256/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kReverse)->Name("SegmentedDeque/S256/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kArithmetic)->Name("SegmentedDeque/S256/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kSegments)->Name("SegmentedDeque/S256/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D256, Access::kPermuted, 31)->Name("SegmentedDeque/S256/Permuted/Offset31")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, D256, false)->Name("SegmentedDeque/S256/FreshBack");
BENCHMARK_TEMPLATE(BmFresh, D256, true)->Name("SegmentedDeque/S256/FreshFront");
BENCHMARK_TEMPLATE(BmQueue, D256, false)->Name("SegmentedDeque/S256/QueueForward");
BENCHMARK_TEMPLATE(BmQueue, D256, true)->Name("SegmentedDeque/S256/QueueReverse");
BENCHMARK_TEMPLATE(BmLifecycle, D256, false)->Name("SegmentedDeque/S256/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, D256, true)->Name("SegmentedDeque/S256/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kIndexed)
    ->Name("SegmentedDeque/S1024/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kPermuted)
    ->Name("SegmentedDeque/S1024/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kForward)->Name("SegmentedDeque/S1024/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kReverse)->Name("SegmentedDeque/S1024/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kArithmetic)->Name("SegmentedDeque/S1024/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kSegments)->Name("SegmentedDeque/S1024/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, D1024, Access::kPermuted, 31)->Name("SegmentedDeque/S1024/Permuted/Offset31")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, D1024, false)->Name("SegmentedDeque/S1024/FreshBack");
BENCHMARK_TEMPLATE(BmFresh, D1024, true)->Name("SegmentedDeque/S1024/FreshFront");
BENCHMARK_TEMPLATE(BmQueue, D1024, false)->Name("SegmentedDeque/S1024/QueueForward");
BENCHMARK_TEMPLATE(BmQueue, D1024, true)->Name("SegmentedDeque/S1024/QueueReverse");
BENCHMARK_TEMPLATE(BmLifecycle, D1024, false)->Name("SegmentedDeque/S1024/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, D1024, true)->Name("SegmentedDeque/S1024/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, V64, Access::kIndexed)
    ->Name("SegmentedVector/S64/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V64, Access::kPermuted)
    ->Name("SegmentedVector/S64/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V64, Access::kForward)->Name("SegmentedVector/S64/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V64, Access::kReverse)->Name("SegmentedVector/S64/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V64, Access::kArithmetic)->Name("SegmentedVector/S64/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V64, Access::kSegments)->Name("SegmentedVector/S64/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, V64, false)->Name("SegmentedVector/S64/FreshBack");
BENCHMARK_TEMPLATE(BmLifecycle, V64, false)->Name("SegmentedVector/S64/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, V64, true)->Name("SegmentedVector/S64/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, V256, Access::kIndexed)
    ->Name("SegmentedVector/S256/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V256, Access::kPermuted)
    ->Name("SegmentedVector/S256/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V256, Access::kForward)->Name("SegmentedVector/S256/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V256, Access::kReverse)->Name("SegmentedVector/S256/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V256, Access::kArithmetic)->Name("SegmentedVector/S256/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V256, Access::kSegments)->Name("SegmentedVector/S256/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, V256, false)->Name("SegmentedVector/S256/FreshBack");
BENCHMARK_TEMPLATE(BmLifecycle, V256, false)->Name("SegmentedVector/S256/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, V256, true)->Name("SegmentedVector/S256/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kIndexed)
    ->Name("SegmentedVector/S1024/Indexed/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kPermuted)
    ->Name("SegmentedVector/S1024/Permuted/Offset0")
    ->Arg(16'384)
    ->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kForward)->Name("SegmentedVector/S1024/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kReverse)->Name("SegmentedVector/S1024/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kArithmetic)->Name("SegmentedVector/S1024/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, V1024, Access::kSegments)->Name("SegmentedVector/S1024/Segments/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, V1024, false)->Name("SegmentedVector/S1024/FreshBack");
BENCHMARK_TEMPLATE(BmLifecycle, V1024, false)->Name("SegmentedVector/S1024/ClearReuse");
BENCHMARK_TEMPLATE(BmLifecycle, V1024, true)->Name("SegmentedVector/S1024/ReleaseRebuild");
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kIndexed)->Name("StdDeque/Indexed/Offset0")->Arg(16'384)->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kPermuted)->Name("StdDeque/Permuted/Offset0")->Arg(16'384)->Arg(1'048'576);
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kForward)->Name("StdDeque/Forward/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kReverse)->Name("StdDeque/Reverse/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kArithmetic)->Name("StdDeque/Arithmetic/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdDeque, Access::kPermuted, 31)->Name("StdDeque/Permuted/Offset31")->Arg(16'384);
BENCHMARK_TEMPLATE(BmFresh, StdDeque, false)->Name("StdDeque/FreshBack");
BENCHMARK_TEMPLATE(BmFresh, StdDeque, true)->Name("StdDeque/FreshFront");
BENCHMARK_TEMPLATE(BmQueue, StdDeque, false)->Name("StdDeque/QueueForward");
BENCHMARK_TEMPLATE(BmQueue, StdDeque, true)->Name("StdDeque/QueueReverse");
BENCHMARK_TEMPLATE(BmRead, DLine, Access::kIndexed)->Name("SegmentedDeque/S256/CacheLine/Indexed/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, DLine, Access::kPermuted)
    ->Name("SegmentedDeque/S256/CacheLine/Permuted/Offset0")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, DLine, Access::kSegments)
    ->Name("SegmentedDeque/S256/CacheLine/Segments/Offset0")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, DLine, Access::kPermuted, 31)
    ->Name("SegmentedDeque/S256/CacheLine/Permuted/Offset31")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, VLine, Access::kIndexed)
    ->Name("SegmentedVector/S256/CacheLine/Indexed/Offset0")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, VLine, Access::kPermuted)
    ->Name("SegmentedVector/S256/CacheLine/Permuted/Offset0")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, VLine, Access::kSegments)
    ->Name("SegmentedVector/S256/CacheLine/Segments/Offset0")
    ->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdLine, Access::kIndexed)->Name("StdDeque/CacheLine/Indexed/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdLine, Access::kPermuted)->Name("StdDeque/CacheLine/Permuted/Offset0")->Arg(16'384);
BENCHMARK_TEMPLATE(BmRead, StdLine, Access::kPermuted, 31)->Name("StdDeque/CacheLine/Permuted/Offset31")->Arg(16'384);
BENCHMARK_TEMPLATE(BmArenaQueue, kS64, false)->Name("SegmentedDeque/S64/ArenaQueueForward");
BENCHMARK_TEMPLATE(BmArenaQueue, kS64, true)->Name("SegmentedDeque/S64/ArenaQueueReverse");
BENCHMARK_TEMPLATE(BmArenaQueue, kS256, false)->Name("SegmentedDeque/S256/ArenaQueueForward");
BENCHMARK_TEMPLATE(BmArenaQueue, kS256, true)->Name("SegmentedDeque/S256/ArenaQueueReverse");
BENCHMARK_TEMPLATE(BmArenaQueue, kS1024, false)->Name("SegmentedDeque/S1024/ArenaQueueForward");
BENCHMARK_TEMPLATE(BmArenaQueue, kS1024, true)->Name("SegmentedDeque/S1024/ArenaQueueReverse");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kS256, false)->Name("SegmentedDeque/S256/GrowthBack/GrowDirectory");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kS256, true)->Name("SegmentedDeque/S256/GrowthFront/GrowDirectory");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kS256Reserved, false)->Name("SegmentedDeque/S256/GrowthBack/ReservedDirectory");
BENCHMARK_TEMPLATE(BmGrowthBoundary, kS256Reserved, true)->Name("SegmentedDeque/S256/GrowthFront/ReservedDirectory");

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::container::container_internal::AddSegmentedBenchmarkContext("segmented-deque-circular-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
