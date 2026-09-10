// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "benchmark/benchmark.h"
#include "mbo/memory/arena.h"

namespace mbo::memory {
namespace {

constexpr std::size_t kAllocationCount = 16'384;
constexpr std::array<std::size_t, 32> kStringLikeSizes = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,  12,  13,  14,  15,  16,
    17, 19, 23, 27, 31, 37, 47, 63, 79, 95, 127, 191, 255, 383, 511, 767,
};

constexpr ArenaOptions kFixedGrowth{
    .initial_block_size = 4'096,
    .maximum_block_size = 4'096,
    .growth_numerator = 1,
    .growth_denominator = 1,
};
constexpr ArenaOptions kGrowthThreeHalves{
    .initial_block_size = 4'096,
    .maximum_block_size = std::size_t{1'024} * 1'024,
    .growth_numerator = 3,
    .growth_denominator = 2,
};
constexpr ArenaOptions kGrowthTwo{
    .initial_block_size = 4'096,
    .maximum_block_size = std::size_t{1'024} * 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};
constexpr ArenaOptions kGrowthFour{
    .initial_block_size = 4'096,
    .maximum_block_size = std::size_t{1'024} * 1'024,
    .growth_numerator = 4,
    .growth_denominator = 1,
};

// NOLINTBEGIN(readability-identifier-naming): benchmark source models BlockSource spelling.
class ListedNewDeleteSource final {
 public:
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    constexpr std::array<std::size_t, 5> kSizes = {
        4'096, 16'384, 65'536, 262'144, std::size_t{1'024} * 1'024,
    };
    const auto acquisition_size =
        size <= kFixedGrowth.initial_block_size ? kSizes.at(std::min(next_size_index_++, kSizes.size() - 1)) : size;
    return NewDeleteBlockSource::TryAcquire(acquisition_size, alignment);
  }

  static void Release(MemoryBlock block) noexcept { NewDeleteBlockSource::Release(block); }

 private:
  std::size_t next_size_index_ = 0;
};

// NOLINTEND(readability-identifier-naming)

template<ArenaOptions Options>
void AllocateWorkload(Arena<NewDeleteBlockSource, Options>& arena) {
  for (std::size_t index = 0; index < kAllocationCount; ++index) {
    const auto ordinary_size = kStringLikeSizes.at(index % kStringLikeSizes.size());
    const auto size = index % 257 == 256 ? std::size_t{64} * 1'024 : ordinary_size;
    benchmark::DoNotOptimize(arena.Allocate(size, 1));
  }
}

template<ArenaOptions Options>
void SetMemoryCounters(benchmark::State& state, const Arena<NewDeleteBlockSource, Options>& arena) {
  state.counters["blocks"] = static_cast<double>(arena.block_count());
  state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
  state.counters["used"] = static_cast<double>(arena.bytes_used());
  state.counters["waste"] = static_cast<double>(arena.bytes_reserved() - arena.bytes_used());
}

template<ArenaOptions Options>
void BmGrowthRetained(benchmark::State& state) {
  Arena<NewDeleteBlockSource, Options> arena;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    AllocateWorkload(arena);
    SetMemoryCounters(state, arena);
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
}

template<ArenaOptions Options>
void BmGrowthFresh(benchmark::State& state) {
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    Arena<NewDeleteBlockSource, Options> arena;
    AllocateWorkload(arena);
    SetMemoryCounters(state, arena);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
}

void RegisterGrowthBenchmarks() {
  benchmark::RegisterBenchmark("Growth/Retained/Fixed4KiB", BmGrowthRetained<kFixedGrowth>);
  benchmark::RegisterBenchmark("Growth/Retained/ThreeHalves", BmGrowthRetained<kGrowthThreeHalves>);
  benchmark::RegisterBenchmark("Growth/Retained/Two", BmGrowthRetained<kGrowthTwo>);
  benchmark::RegisterBenchmark("Growth/Retained/Four", BmGrowthRetained<kGrowthFour>);
  benchmark::RegisterBenchmark("Growth/Fresh/Fixed4KiB", BmGrowthFresh<kFixedGrowth>);
  benchmark::RegisterBenchmark("Growth/Fresh/ThreeHalves", BmGrowthFresh<kGrowthThreeHalves>);
  benchmark::RegisterBenchmark("Growth/Fresh/Two", BmGrowthFresh<kGrowthTwo>);
  benchmark::RegisterBenchmark("Growth/Fresh/Four", BmGrowthFresh<kGrowthFour>);
}

void BmListedRetained(benchmark::State& state) {
  Arena<ListedNewDeleteSource, kFixedGrowth> arena;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kAllocationCount; ++index) {
      const auto ordinary_size = kStringLikeSizes.at(index % kStringLikeSizes.size());
      const auto size = index % 257 == 256 ? std::size_t{64} * 1'024 : ordinary_size;
      benchmark::DoNotOptimize(arena.Allocate(size, 1));
    }
    state.counters["blocks"] = static_cast<double>(arena.block_count());
    state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
    state.counters["used"] = static_cast<double>(arena.bytes_used());
    state.counters["waste"] = static_cast<double>(arena.bytes_reserved() - arena.bytes_used());
    arena.Reset();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
}

void BmListedFresh(benchmark::State& state) {
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    Arena<ListedNewDeleteSource, kFixedGrowth> arena;
    for (std::size_t index = 0; index < kAllocationCount; ++index) {
      const auto ordinary_size = kStringLikeSizes.at(index % kStringLikeSizes.size());
      const auto size = index % 257 == 256 ? std::size_t{64} * 1'024 : ordinary_size;
      benchmark::DoNotOptimize(arena.Allocate(size, 1));
    }
    state.counters["blocks"] = static_cast<double>(arena.block_count());
    state.counters["reserved"] = static_cast<double>(arena.bytes_reserved());
    state.counters["used"] = static_cast<double>(arena.bytes_used());
    state.counters["waste"] = static_cast<double>(arena.bytes_reserved() - arena.bytes_used());
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
}

[[maybe_unused]] const bool kRegistered = [] {
  RegisterGrowthBenchmarks();
  benchmark::RegisterBenchmark("Growth/Retained/Listed", BmListedRetained);
  benchmark::RegisterBenchmark("Growth/Fresh/Listed", BmListedFresh);
  return true;
}();

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
  benchmark::AddCustomContext("cxx_standard", "c++20");
  benchmark::AddCustomContext("experiment", "arena-block-growth-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
