// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index):
// the benchmark deliberately measures unchecked lookup implementations.
// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;
constexpr auto kListedCapacities = std::to_array<std::size_t>({64, 256, 1'024, 4'096});

struct Segment final {
  std::vector<std::uint64_t> owned;
  std::uint64_t* data = nullptr;
  std::size_t begin = 0;
  std::size_t capacity = 0;
};

class ListedStorage final {
 public:
  ListedStorage() {
    std::size_t begin = 0;
    std::size_t segment_index = 0;
    while (begin < kElementCount) {
      const std::size_t capacity =
          segment_index < kListedCapacities.size() ? kListedCapacities[segment_index] : kListedCapacities.back();
      std::vector<std::uint64_t> owned(capacity);
      auto* const data = owned.data();
      for (std::size_t offset = 0; offset < capacity; ++offset) {
        data[offset] = begin + offset;
      }
      segments_.push_back(Segment{
          .owned = std::move(owned),
          .data = data,
          .begin = begin,
          .capacity = capacity,
      });
      begin += capacity;
      ++segment_index;
    }
  }

  const std::vector<Segment>& Segments() const noexcept { return segments_; }

  const std::uint64_t& TailMapped(std::size_t pos) const noexcept {
    constexpr std::size_t kListedCapacity = 64 + 256 + 1'024 + 4'096;
    std::size_t segment_index = 0;
    std::size_t offset = pos;
    if (pos >= kListedCapacity) {
      const std::size_t tail = pos - kListedCapacity;
      segment_index = kListedCapacities.size() + (tail / kListedCapacities.back());
      offset = tail % kListedCapacities.back();
    } else {
      while (offset >= kListedCapacities[segment_index]) {
        offset -= kListedCapacities[segment_index];
        ++segment_index;
      }
    }
    return segments_[segment_index].data[offset];
  }

 private:
  std::vector<Segment> segments_;
};

template<std::size_t PageSize>
requires(PageSize > 0 && (PageSize & (PageSize - 1)) == 0 && 64 % PageSize == 0)
class PointerPageDirectory final {
 public:
  explicit PointerPageDirectory(const ListedStorage& storage) {
    for (const Segment& segment : storage.Segments()) {
      for (std::size_t offset = 0; offset < segment.capacity; offset += PageSize) {
        pages_.push_back(segment.data + offset);
      }
    }
  }

  const std::uint64_t& operator[](std::size_t pos) const noexcept { return pages_[pos / PageSize][pos % PageSize]; }

  std::size_t Bytes() const noexcept { return pages_.capacity() * sizeof(pages_.front()); }

 private:
  std::vector<std::uint64_t*> pages_;
};

template<std::size_t PageSize, typename SegmentIndex = std::uint16_t>
requires(PageSize > 0 && (PageSize & (PageSize - 1)) == 0 && 64 % PageSize == 0)
class CompactPageDirectory final {
 public:
  explicit CompactPageDirectory(const ListedStorage& storage) : segments_(&storage.Segments()) {
    std::size_t segment_index = 0;
    for (const Segment& segment : storage.Segments()) {
      for (std::size_t offset = 0; offset < segment.capacity; offset += PageSize) {
        pages_.push_back(static_cast<SegmentIndex>(segment_index));
      }
      ++segment_index;
    }
  }

  const std::uint64_t& operator[](std::size_t pos) const noexcept {
    const Segment& segment = (*segments_)[pages_[pos / PageSize]];
    return segment.data[pos - segment.begin];
  }

  std::size_t Bytes() const noexcept { return pages_.capacity() * sizeof(pages_.front()); }

 private:
  const std::vector<Segment>* segments_;
  std::vector<SegmentIndex> pages_;
};

class ElementPointerDirectory final {
 public:
  explicit ElementPointerDirectory(const ListedStorage& storage) {
    pointers_.reserve(kElementCount);
    for (const Segment& segment : storage.Segments()) {
      const std::size_t remaining = kElementCount - pointers_.size();
      const std::size_t count = remaining < segment.capacity ? remaining : segment.capacity;
      for (std::size_t offset = 0; offset < count; ++offset) {
        pointers_.push_back(segment.data + offset);
      }
    }
  }

  const std::uint64_t& operator[](std::size_t pos) const noexcept { return *pointers_[pos]; }

  std::size_t Bytes() const noexcept { return pointers_.capacity() * sizeof(pointers_.front()); }

 private:
  std::vector<std::uint64_t*> pointers_;
};

class TailLookup final {
 public:
  explicit TailLookup(const ListedStorage& storage) noexcept : storage_(&storage) {}

  const std::uint64_t& operator[](std::size_t pos) const noexcept { return storage_->TailMapped(pos); }

 private:
  const ListedStorage* storage_;
};

enum class DirectoryGrowth {
  kExact,
  kNatural,
  kOneAndHalf,
  kDouble,
  kPreallocated,
};

template<DirectoryGrowth Growth>
class GrowingPointerDirectory final {
 public:
  explicit GrowingPointerDirectory(std::size_t final_pages) {
    if constexpr (Growth == DirectoryGrowth::kPreallocated) {
      Reserve(final_pages);
    }
  }

  void Add(std::uint64_t* first, std::size_t page_count) {
    const std::size_t required = pages_.size() + page_count;
    if constexpr (Growth == DirectoryGrowth::kExact) {
      Reserve(required);
    } else if constexpr (Growth == DirectoryGrowth::kOneAndHalf) {
      ReserveFor(required, 3, 2);
    } else if constexpr (Growth == DirectoryGrowth::kDouble) {
      ReserveFor(required, 2, 1);
    }
    for (std::size_t page = 0; page < page_count; ++page) {
      const std::size_t previous_capacity = pages_.capacity();
      pages_.push_back(first + (page * 64));
      if (pages_.capacity() != previous_capacity) {
        ++reallocations_;
      }
    }
  }

  std::size_t Bytes() const noexcept { return pages_.capacity() * sizeof(pages_.front()); }

  std::size_t Reallocations() const noexcept { return reallocations_; }

  std::size_t Size() const noexcept { return pages_.size(); }

  const std::uint64_t& operator[](std::size_t pos) const noexcept { return pages_[pos / 64][pos % 64]; }

 private:
  void Reserve(std::size_t requested) {
    const std::size_t previous_capacity = pages_.capacity();
    pages_.reserve(requested);
    if (pages_.capacity() != previous_capacity) {
      ++reallocations_;
    }
  }

  void ReserveFor(std::size_t required, std::size_t numerator, std::size_t denominator) {
    if (required <= pages_.capacity()) {
      return;
    }
    const std::size_t grown = pages_.capacity() * numerator / denominator;
    Reserve(std::max(required, grown));
  }

  std::vector<std::uint64_t*> pages_;
  std::size_t reallocations_ = 0;
};

enum class SegmentSchedule {
  kUniform64,
  kUniform256,
  kUniform1024,
  kListed,
};

template<SegmentSchedule Schedule>
std::vector<std::size_t> PageCounts() {
  std::vector<std::size_t> result;
  std::size_t capacity = 0;
  std::size_t segment = 0;
  while (capacity < kElementCount) {
    std::size_t segment_capacity = 0;
    if constexpr (Schedule == SegmentSchedule::kUniform64) {
      segment_capacity = 64;
    } else if constexpr (Schedule == SegmentSchedule::kUniform256) {
      segment_capacity = 256;
    } else if constexpr (Schedule == SegmentSchedule::kUniform1024) {
      segment_capacity = 1'024;
    } else {
      segment_capacity = segment < kListedCapacities.size() ? kListedCapacities[segment] : kListedCapacities.back();
    }
    result.push_back(segment_capacity / 64);
    capacity += segment_capacity;
    ++segment;
  }
  return result;
}

template<DirectoryGrowth Growth, SegmentSchedule Schedule>
void BmDirectoryGrowth(benchmark::State& state) {
  const std::vector<std::size_t> page_counts = PageCounts<Schedule>();
  std::size_t final_pages = 0;
  for (const std::size_t count : page_counts) {
    final_pages += count;
  }
  std::vector<std::uint64_t> storage(final_pages * 64);
  for (std::size_t pos = 0; pos < storage.size(); ++pos) {
    storage[pos] = pos;
  }
  for (auto _ : state) {
    GrowingPointerDirectory<Growth> directory(final_pages);
    std::size_t first_page = 0;
    for (const std::size_t count : page_counts) {
      directory.Add(storage.data() + (first_page * 64), count);
      first_page += count;
    }
    if (directory.Size() != final_pages || directory[storage.size() - 1] != storage.size() - 1) {
      state.SkipWithError("directory growth produced an invalid mapping");
      return;
    }
    benchmark::DoNotOptimize(directory);
    state.counters["directory_bytes"] = static_cast<double>(directory.Bytes());
    state.counters["entries"] = static_cast<double>(directory.Size());
    state.counters["reallocations"] = static_cast<double>(directory.Reallocations());
  }
}

template<bool Permuted, typename Lookup>
void MeasureLookup(benchmark::State& state, const Lookup& lookup) {
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    if (lookup[pos] != pos) {
      state.SkipWithError("lookup returned the wrong element");
      return;
    }
  }
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < kElementCount; ++ordinal) {
      const std::size_t pos = Permuted ? (ordinal * 40'503) & (kElementCount - 1) : ordinal;
      sum += lookup[pos];
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<bool Permuted>
void BmTailMapped(benchmark::State& state) {
  const ListedStorage storage;
  const TailLookup lookup(storage);
  MeasureLookup<Permuted>(state, lookup);
  state.counters["directory_bytes"] = 0;
}

template<std::size_t PageSize, bool Permuted>
void BmPointerPages(benchmark::State& state) {
  const ListedStorage storage;
  const PointerPageDirectory<PageSize> lookup(storage);
  MeasureLookup<Permuted>(state, lookup);
  state.counters["directory_bytes"] = static_cast<double>(lookup.Bytes());
}

template<std::size_t PageSize, bool Permuted>
void BmCompactPages(benchmark::State& state) {
  const ListedStorage storage;
  const CompactPageDirectory<PageSize> lookup(storage);
  MeasureLookup<Permuted>(state, lookup);
  state.counters["directory_bytes"] = static_cast<double>(lookup.Bytes());
}

template<bool Permuted>
void BmCompact32Page64(benchmark::State& state) {
  const ListedStorage storage;
  const CompactPageDirectory<64, std::uint32_t> lookup(storage);
  MeasureLookup<Permuted>(state, lookup);
  state.counters["directory_bytes"] = static_cast<double>(lookup.Bytes());
}

template<bool Permuted>
void BmElementPointers(benchmark::State& state) {
  const ListedStorage storage;
  const ElementPointerDirectory lookup(storage);
  MeasureLookup<Permuted>(state, lookup);
  state.counters["directory_bytes"] = static_cast<double>(lookup.Bytes());
}

template<typename Directory>
void BmBuildDirectory(benchmark::State& state) {
  const ListedStorage storage;
  for (auto _ : state) {
    const Directory directory(storage);
    benchmark::DoNotOptimize(directory.Bytes());
    state.counters["directory_bytes"] = static_cast<double>(directory.Bytes());
  }
}

#define REGISTER_PAGE_BENCHMARKS(Label, PageSize)                                                                \
  BENCHMARK_TEMPLATE(BmPointerPages, PageSize, false)->Name("Mapping/Sequential/PointerPage" Label);             \
  BENCHMARK_TEMPLATE(BmPointerPages, PageSize, true)->Name("Mapping/Permuted/PointerPage" Label);                \
  BENCHMARK_TEMPLATE(BmCompactPages, PageSize, false)->Name("Mapping/Sequential/CompactPage" Label);             \
  BENCHMARK_TEMPLATE(BmCompactPages, PageSize, true)->Name("Mapping/Permuted/CompactPage" Label);                \
  BENCHMARK_TEMPLATE(BmBuildDirectory, PointerPageDirectory<PageSize>)->Name("Mapping/Build/PointerPage" Label); \
  BENCHMARK_TEMPLATE(BmBuildDirectory, CompactPageDirectory<PageSize>)->Name("Mapping/Build/CompactPage" Label)

BENCHMARK_TEMPLATE(BmTailMapped, false)->Name("Mapping/Sequential/TailMapped");
BENCHMARK_TEMPLATE(BmTailMapped, true)->Name("Mapping/Permuted/TailMapped");
BENCHMARK_TEMPLATE(BmElementPointers, false)->Name("Mapping/Sequential/ElementPointers");
BENCHMARK_TEMPLATE(BmElementPointers, true)->Name("Mapping/Permuted/ElementPointers");
BENCHMARK_TEMPLATE(BmCompact32Page64, false)->Name("Mapping/Sequential/Compact32Page64");
BENCHMARK_TEMPLATE(BmCompact32Page64, true)->Name("Mapping/Permuted/Compact32Page64");
BENCHMARK_TEMPLATE(BmBuildDirectory, CompactPageDirectory<64, std::uint32_t>)->Name("Mapping/Build/Compact32Page64");
REGISTER_PAGE_BENCHMARKS("4", 4);
REGISTER_PAGE_BENCHMARKS("8", 8);
REGISTER_PAGE_BENCHMARKS("16", 16);
REGISTER_PAGE_BENCHMARKS("32", 32);
REGISTER_PAGE_BENCHMARKS("64", 64);

#undef REGISTER_PAGE_BENCHMARKS

#define REGISTER_DIRECTORY_GROWTH(Label, Growth, ScheduleLabel, Schedule)                   \
  BENCHMARK_TEMPLATE(BmDirectoryGrowth, DirectoryGrowth::Growth, SegmentSchedule::Schedule) \
      ->Name("DirectoryGrowth/" Label "/" ScheduleLabel)

#define REGISTER_DIRECTORY_GROWTH_SCHEDULES(Label, Growth)               \
  REGISTER_DIRECTORY_GROWTH(Label, Growth, "Uniform64", kUniform64);     \
  REGISTER_DIRECTORY_GROWTH(Label, Growth, "Uniform256", kUniform256);   \
  REGISTER_DIRECTORY_GROWTH(Label, Growth, "Uniform1024", kUniform1024); \
  REGISTER_DIRECTORY_GROWTH(Label, Growth, "Listed", kListed)

REGISTER_DIRECTORY_GROWTH_SCHEDULES("Exact", kExact);
REGISTER_DIRECTORY_GROWTH_SCHEDULES("Natural", kNatural);
REGISTER_DIRECTORY_GROWTH_SCHEDULES("OneAndHalf", kOneAndHalf);
REGISTER_DIRECTORY_GROWTH_SCHEDULES("Double", kDouble);
REGISTER_DIRECTORY_GROWTH_SCHEDULES("Preallocated", kPreallocated);

#undef REGISTER_DIRECTORY_GROWTH_SCHEDULES
#undef REGISTER_DIRECTORY_GROWTH

// NOLINTEND(clang-analyzer-deadcode.DeadStores)
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

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
  benchmark::AddCustomContext("experiment", "segmented-sequence-mapping-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
