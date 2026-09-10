// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
constexpr ArenaOptions kRecordArenaOptions{
    .initial_block_size = std::size_t{64} * 1'024,
    .maximum_block_size = std::size_t{1'024} * 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};

struct PointerRecord final {
  const char* data;
  std::uint32_t size;
};

struct OffsetRecord final {
  std::uint32_t offset;
  std::uint32_t size;
};

struct SegmentOffsetRecord final {
  std::uint32_t segment;
  std::uint32_t offset;
  std::uint32_t size;
};

// NOLINTBEGIN(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays):
// Experimental stores model container spelling; unique_ptr<T[]> is intentional dynamic storage.
class PointerRecords final {
 public:
  PointerRecords(std::size_t count, std::size_t /*bytes*/) { records_.reserve(count); }

  void Add(std::string_view value) {
    auto* const data = reinterpret_cast<char*>(  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        arena_.Allocate(value.size(), 1));
    std::memcpy(data, value.data(), value.size());
    records_.push_back(PointerRecord{.data = data, .size = static_cast<std::uint32_t>(value.size())});
  }

  std::string_view Get(std::size_t index) const {
    const auto& record = records_.at(index);
    return {record.data, record.size};
  }

  std::size_t bytes_reserved() const noexcept {
    return arena_.bytes_reserved() + (records_.capacity() * sizeof(PointerRecord));
  }

 private:
  Arena<NewDeleteBlockSource, kRecordArenaOptions> arena_;
  std::vector<PointerRecord> records_;
};

class PointerSoARecords final {
 public:
  PointerSoARecords(std::size_t count, std::size_t /*bytes*/) {
    pointers_.reserve(count);
    sizes_.reserve(count);
  }

  void Add(std::string_view value) {
    auto* const data = reinterpret_cast<char*>(  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        arena_.Allocate(value.size(), 1));
    std::memcpy(data, value.data(), value.size());
    pointers_.push_back(data);
    sizes_.push_back(static_cast<std::uint32_t>(value.size()));
  }

  std::string_view Get(std::size_t index) const { return {pointers_.at(index), sizes_.at(index)}; }

  std::size_t bytes_reserved() const noexcept {
    return arena_.bytes_reserved() + (pointers_.capacity() * sizeof(const char*))
           + (sizes_.capacity() * sizeof(std::uint32_t));
  }

 private:
  Arena<NewDeleteBlockSource, kRecordArenaOptions> arena_;
  std::vector<const char*> pointers_;
  std::vector<std::uint32_t> sizes_;
};

class OffsetRecords final {
 public:
  OffsetRecords(std::size_t count, std::size_t bytes) {
    records_.reserve(count);
    bytes_.reserve(bytes);
  }

  void Add(std::string_view value) {
    const auto offset = bytes_.size();
    bytes_.resize(offset + value.size());
    std::memcpy(bytes_.data() + offset, value.data(), value.size());
    records_.push_back(
        OffsetRecord{.offset = static_cast<std::uint32_t>(offset), .size = static_cast<std::uint32_t>(value.size())});
  }

  std::string_view Get(std::size_t index) const {
    const auto& record = records_.at(index);
    return {bytes_.data() + record.offset, record.size};
  }

  std::size_t bytes_reserved() const noexcept {
    return bytes_.capacity() + (records_.capacity() * sizeof(OffsetRecord));
  }

 private:
  std::vector<char> bytes_;
  std::vector<OffsetRecord> records_;
};

class SegmentOffsetRecords final {
 public:
  SegmentOffsetRecords(std::size_t count, std::size_t /*bytes*/) { records_.reserve(count); }

  void Add(std::string_view value) {
    if (segments_.empty() || segment_used_ + value.size() > segment_capacity_) {
      AddSegment(std::max(kSegmentSize, value.size()));
    }
    std::memcpy(segments_.back().get() + segment_used_, value.data(), value.size());
    records_.push_back(SegmentOffsetRecord{
        .segment = static_cast<std::uint32_t>(segments_.size() - 1),
        .offset = static_cast<std::uint32_t>(segment_used_),
        .size = static_cast<std::uint32_t>(value.size()),
    });
    segment_used_ += value.size();
  }

  std::string_view Get(std::size_t index) const {
    const auto& record = records_.at(index);
    return {segments_.at(record.segment).get() + record.offset, record.size};
  }

  std::size_t bytes_reserved() const noexcept {
    return segment_bytes_ + (segments_.capacity() * sizeof(std::unique_ptr<char[]>))
           + (records_.capacity() * sizeof(SegmentOffsetRecord));
  }

 private:
  static constexpr std::size_t kSegmentSize = std::size_t{64} * 1'024;

  void AddSegment(std::size_t size) {
    segments_.push_back(std::make_unique_for_overwrite<char[]>(size));
    segment_bytes_ += size;
    segment_capacity_ = size;
    segment_used_ = 0;
  }

  std::vector<std::unique_ptr<char[]>> segments_;
  std::vector<SegmentOffsetRecord> records_;
  std::size_t segment_bytes_ = 0;
  std::size_t segment_capacity_ = 0;
  std::size_t segment_used_ = 0;
};

class InlineRecords final {
 public:
  InlineRecords(std::size_t count, std::size_t bytes) {
    offsets_.reserve(count);
    bytes_.reserve(bytes + (count * sizeof(std::uint32_t)));
  }

  void Add(std::string_view value) {
    const auto offset = bytes_.size();
    offsets_.push_back(static_cast<std::uint32_t>(offset));
    bytes_.resize(offset + sizeof(std::uint32_t) + value.size());
    const auto size = static_cast<std::uint32_t>(value.size());
    std::memcpy(bytes_.data() + offset, &size, sizeof(size));
    std::memcpy(bytes_.data() + offset + sizeof(size), value.data(), value.size());
  }

  std::string_view Get(std::size_t index) const {
    const auto offset = offsets_.at(index);
    std::uint32_t size = 0;
    std::memcpy(&size, bytes_.data() + offset, sizeof(size));
    return {bytes_.data() + offset + sizeof(size), size};
  }

  std::size_t bytes_reserved() const noexcept {
    return bytes_.capacity() + (offsets_.capacity() * sizeof(std::uint32_t));
  }

 private:
  std::vector<char> bytes_;
  std::vector<std::uint32_t> offsets_;
};

// NOLINTEND(readability-identifier-naming,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

const std::vector<std::string>& RecordCorpus() {
  static const auto kCorpus = [] {
    std::vector<std::string> values;
    values.reserve(kAllocationCount);
    for (std::size_t index = 0; index < kAllocationCount; ++index) {
      const auto size = kStringLikeSizes.at(index % kStringLikeSizes.size());
      values.emplace_back(size, static_cast<char>('a' + (index % 26)));
    }
    return values;
  }();
  return kCorpus;
}

std::size_t RecordCorpusBytes() {
  std::size_t result = 0;
  for (const auto& value : RecordCorpus()) {
    result += value.size();
  }
  return result;
}

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

template<std::size_t MaximumCachedBlock, std::size_t CacheBudget>
class CachingNewDeleteSource final {
 public:
  static constexpr bool supports_recoverable_failure = true;

  CachingNewDeleteSource() { cached_.reserve(64); }

  CachingNewDeleteSource(const CachingNewDeleteSource&) = delete;
  CachingNewDeleteSource& operator=(const CachingNewDeleteSource&) = delete;
  CachingNewDeleteSource(CachingNewDeleteSource&&) = delete;
  CachingNewDeleteSource& operator=(CachingNewDeleteSource&&) = delete;

  ~CachingNewDeleteSource() {
    for (const auto block : cached_) {
      NewDeleteBlockSource::Release(block);
    }
  }

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < cached_.size(); ++index) {
      const auto& block = cached_.at(index);
      if (block.size >= size && block.alignment >= alignment
          && (!best.has_value() || block.size < cached_.at(*best).size)) {
        best = index;
      }
    }
    if (!best.has_value()) {
      return NewDeleteBlockSource::TryAcquire(size, alignment);
    }
    const auto result = cached_.at(*best);
    cached_bytes_ -= result.size;
    cached_.at(*best) = cached_.back();
    cached_.pop_back();
    return result;
  }

  void Release(MemoryBlock block) noexcept {
    if (block.size <= MaximumCachedBlock && block.size <= CacheBudget - std::min(cached_bytes_, CacheBudget)) {
      cached_.push_back(block);
      cached_bytes_ += block.size;
    } else {
      NewDeleteBlockSource::Release(block);
    }
  }

  std::size_t cached_reserved_bytes() const noexcept {
    return cached_bytes_ + (cached_.capacity() * sizeof(MemoryBlock));
  }

  std::size_t cached_blocks() const noexcept { return cached_.size(); }

 private:
  std::vector<MemoryBlock> cached_;
  std::size_t cached_bytes_ = 0;
};

// NOLINTEND(readability-identifier-naming)

using SmallBlockCacheSource = CachingNewDeleteSource<std::size_t{256} * 1'024, std::size_t{2} * 1'024 * 1'024>;
using FullBlockCacheSource = CachingNewDeleteSource<std::size_t{1'024} * 1'024, std::size_t{8} * 1'024 * 1'024>;

enum class RetentionMode : std::uint8_t { kReset, kRelease };

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

template<typename Source>
std::size_t SourceCachedBytes(const Arena<Source, kGrowthTwo>& arena) {
  if constexpr (requires { arena.source().cached_reserved_bytes(); }) {
    return arena.source().cached_reserved_bytes();
  } else {
    return 0;
  }
}

template<typename Source>
std::size_t SourceCachedBlocks(const Arena<Source, kGrowthTwo>& arena) {
  if constexpr (requires { arena.source().cached_blocks(); }) {
    return arena.source().cached_blocks();
  } else {
    return 0;
  }
}

template<RetentionMode Mode, typename Source>
void EndRetentionCycle(Arena<Source, kGrowthTwo>& arena) {
  if constexpr (Mode == RetentionMode::kReset) {
    arena.Reset();
  } else {
    arena.Release();
  }
}

template<RetentionMode Mode, typename Source>
void BmRetentionAfterBurst(benchmark::State& state) {
  constexpr std::size_t kSteadyAllocationCount = 4'096;
  Arena<Source, kGrowthTwo> arena;
  for (std::size_t index = 0; index < kAllocationCount; ++index) {
    const auto ordinary_size = kStringLikeSizes.at(index % kStringLikeSizes.size());
    const auto size = index % 257 == 256 ? std::size_t{64} * 1'024 : ordinary_size;
    benchmark::DoNotOptimize(arena.Allocate(size, 1));
  }
  EndRetentionCycle<Mode>(arena);
  std::size_t peak_reserved = 0;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t index = 0; index < kSteadyAllocationCount; ++index) {
      benchmark::DoNotOptimize(arena.Allocate(kStringLikeSizes.at(index % kStringLikeSizes.size()), 1));
    }
    peak_reserved = arena.bytes_reserved() + SourceCachedBytes(arena);
    EndRetentionCycle<Mode>(arena);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kSteadyAllocationCount));
  state.counters["peak_reserved"] = static_cast<double>(peak_reserved);
  state.counters["retained"] = static_cast<double>(arena.bytes_reserved() + SourceCachedBytes(arena));
  state.counters["retained_blocks"] = static_cast<double>(arena.block_count() + SourceCachedBlocks(arena));
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

template<typename Records>
void BmRecordInsert(benchmark::State& state) {
  const auto bytes = RecordCorpusBytes();
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    Records records(kAllocationCount, bytes);
    for (const auto& value : RecordCorpus()) {
      records.Add(value);
    }
    state.counters["reserved"] = static_cast<double>(records.bytes_reserved());
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
}

template<typename Records, bool Permuted>
void BmRecordLookup(benchmark::State& state) {
  const auto bytes = RecordCorpusBytes();
  Records records(kAllocationCount, bytes);
  for (const auto& value : RecordCorpus()) {
    records.Add(value);
  }
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  for (auto _ : state) {
    for (std::size_t ordinal = 0; ordinal < kAllocationCount; ++ordinal) {
      const auto index = Permuted ? (ordinal * 40'503) % kAllocationCount : ordinal;
      const auto value = records.Get(index);
      benchmark::DoNotOptimize(value.data());
      benchmark::DoNotOptimize(value.size());
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kAllocationCount));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
  state.counters["reserved"] = static_cast<double>(records.bytes_reserved());
}

void RegisterRecordBenchmarks() {
  benchmark::RegisterBenchmark("Records/Insert/Pointer", BmRecordInsert<PointerRecords>);
  benchmark::RegisterBenchmark("Records/Insert/PointerSoA", BmRecordInsert<PointerSoARecords>);
  benchmark::RegisterBenchmark("Records/Insert/ContiguousOffsetFixed", BmRecordInsert<OffsetRecords>);
  benchmark::RegisterBenchmark("Records/Insert/SegmentOffset", BmRecordInsert<SegmentOffsetRecords>);
  benchmark::RegisterBenchmark("Records/Insert/ContiguousInlineFixed", BmRecordInsert<InlineRecords>);
  benchmark::RegisterBenchmark("Records/LookupSequential/Pointer", BmRecordLookup<PointerRecords, false>);
  benchmark::RegisterBenchmark("Records/LookupSequential/PointerSoA", BmRecordLookup<PointerSoARecords, false>);
  benchmark::RegisterBenchmark("Records/LookupSequential/ContiguousOffsetFixed", BmRecordLookup<OffsetRecords, false>);
  benchmark::RegisterBenchmark("Records/LookupSequential/SegmentOffset", BmRecordLookup<SegmentOffsetRecords, false>);
  benchmark::RegisterBenchmark("Records/LookupSequential/ContiguousInlineFixed", BmRecordLookup<InlineRecords, false>);
  benchmark::RegisterBenchmark("Records/LookupPermuted/Pointer", BmRecordLookup<PointerRecords, true>);
  benchmark::RegisterBenchmark("Records/LookupPermuted/PointerSoA", BmRecordLookup<PointerSoARecords, true>);
  benchmark::RegisterBenchmark("Records/LookupPermuted/ContiguousOffsetFixed", BmRecordLookup<OffsetRecords, true>);
  benchmark::RegisterBenchmark("Records/LookupPermuted/SegmentOffset", BmRecordLookup<SegmentOffsetRecords, true>);
  benchmark::RegisterBenchmark("Records/LookupPermuted/ContiguousInlineFixed", BmRecordLookup<InlineRecords, true>);
}

void RegisterRetentionBenchmarks() {
  benchmark::RegisterBenchmark(
      "Retention/AfterBurst/RetainAll", BmRetentionAfterBurst<RetentionMode::kReset, NewDeleteBlockSource>);
  benchmark::RegisterBenchmark(
      "Retention/AfterBurst/ReleaseAll", BmRetentionAfterBurst<RetentionMode::kRelease, NewDeleteBlockSource>);
  benchmark::RegisterBenchmark(
      "Retention/AfterBurst/CacheSmallBlocks", BmRetentionAfterBurst<RetentionMode::kRelease, SmallBlockCacheSource>);
  benchmark::RegisterBenchmark(
      "Retention/AfterBurst/CacheAllWithinBudget",
      BmRetentionAfterBurst<RetentionMode::kRelease, FullBlockCacheSource>);
}

[[maybe_unused]] const bool kRegistered = [] {
  RegisterGrowthBenchmarks();
  benchmark::RegisterBenchmark("Growth/Retained/Listed", BmListedRetained);
  benchmark::RegisterBenchmark("Growth/Fresh/Listed", BmListedFresh);
  RegisterRecordBenchmarks();
  RegisterRetentionBenchmarks();
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
  benchmark::AddCustomContext("experiment", "arena-layouts-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
