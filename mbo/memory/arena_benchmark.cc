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

#include "benchmark/benchmark.h"
#include "mbo/memory/arena.h"

namespace mbo::memory {
namespace {

constexpr std::size_t kBatch = 1'024;
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
    }
  }
}

[[maybe_unused]] const bool kRegistered = [] {
  RegisterAllocationBenchmarks();
  return true;
}();

BENCHMARK(BmArenaReset);
BENCHMARK(BmFixedArenaExhaustion);

}  // namespace
}  // namespace mbo::memory

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
