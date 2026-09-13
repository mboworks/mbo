// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_packed_node_block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;

struct Header final {
  std::uint64_t bitmap;
};

using Source = mbo::memory::InlineBlockSource<256, alignof(std::max_align_t)>;
using Block = HamtPackedNodeBlock<Header, int, const void*, Source>;

struct HamtPackedNodeBlockTest : ::testing::Test {};

struct LifetimeValue final {
  int* copies;
  int* destructions;

  LifetimeValue(int& copy_count, int& destruction_count) noexcept
      : copies(&copy_count), destructions(&destruction_count) {}

  LifetimeValue(const LifetimeValue& other) noexcept : copies(other.copies), destructions(other.destructions) {
    ++*copies;
  }

  LifetimeValue& operator=(const LifetimeValue&) = delete;
  LifetimeValue(LifetimeValue&&) = delete;
  LifetimeValue& operator=(LifetimeValue&&) = delete;

  ~LifetimeValue() noexcept { ++*destructions; }
};

TEST_F(HamtPackedNodeBlockTest, DestroysEveryOwnedObjectExactlyOnceBeforeReuse) {
  int copies = 0;
  int destructions = 0;
  const LifetimeValue header(copies, destructions);
  const std::array<LifetimeValue, 2> entries = {
      LifetimeValue(copies, destructions), LifetimeValue(copies, destructions)};
  const std::array<LifetimeValue, 2> children = {
      LifetimeValue(copies, destructions), LifetimeValue(copies, destructions)};
  Source source;
  {
    HamtPackedNodeBlock<LifetimeValue, LifetimeValue, LifetimeValue, Source> block(source);
    ASSERT_THAT(block.TryInitialize(header, entries, children), Eq(true));
    EXPECT_THAT(copies, Eq(5));
    EXPECT_THAT(destructions, Eq(0));
    const auto& const_block = block;
    EXPECT_THAT(const_block.entries().size(), Eq(2));
    EXPECT_THAT(const_block.children().size(), Eq(2));
    EXPECT_THAT(const_block.header().copies, Eq(&copies));
    block.clear();
    EXPECT_THAT(destructions, Eq(5));
    block.clear();
    EXPECT_THAT(destructions, Eq(5));
    ASSERT_THAT(block.TryInitialize(header, {}, {}), Eq(true));
    EXPECT_THAT(copies, Eq(6));
  }
  EXPECT_THAT(destructions, Eq(6));
}

// NOLINTBEGIN(readability-identifier-naming) -- BlockSource vocabulary.
struct RecordingSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return 32; }

  alignas(32) std::array<std::byte, 256> storage{};
  mbo::memory::MemoryBlock offered{.data = storage.data(), .size = storage.size(), .alignment = 32};
  mbo::memory::MemoryBlock released{};
  std::size_t release_count = 0;

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t, std::size_t) noexcept { return offered; }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    released = block;
    ++release_count;
  }
};

// NOLINTEND(readability-identifier-naming)

TEST_F(HamtPackedNodeBlockTest, ReturnsOriginalOversizedAllocationMetadata) {
  RecordingSource source;
  {
    HamtPackedNodeBlock<Header, int, const void*, RecordingSource> block(source);
    ASSERT_THAT(block.TryInitialize(Header{42}, {}, {}), Eq(true));
    EXPECT_THAT(source.release_count, Eq(0));
  }
  EXPECT_THAT(source.release_count, Eq(1));
  EXPECT_THAT(source.released, Eq(source.offered));
}

TEST_F(HamtPackedNodeBlockTest, RejectsAndReturnsUnusableSourceBlocks) {
  RecordingSource source;
  const auto unusable = std::to_array<mbo::memory::MemoryBlock>({
      {.data = nullptr, .size = 256, .alignment = 32},
      {.data = source.storage.data(), .size = 1, .alignment = 32},
      {.data = source.storage.data(), .size = 256, .alignment = 1},
      {.data = source.storage.data() + 1, .size = 255, .alignment = 32},
  });
  for (const auto& offered : unusable) {
    source.offered = offered;
    source.release_count = 0;
    HamtPackedNodeBlock<Header, int, const void*, RecordingSource> block(source);
    EXPECT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(false));
    EXPECT_THAT(block.empty(), Eq(true));
    EXPECT_THAT(source.release_count, Eq(1));
    EXPECT_THAT(source.released, Eq(offered));
  }
}

TEST_F(HamtPackedNodeBlockTest, OwnsOneAlignedBlockWithDenseArrays) {
  Source source;
  Block block(source);
  constexpr auto kEntries = std::to_array<int>({3, 5, 7});
  const auto children = std::to_array<const void*>({&source, &block});

  ASSERT_THAT(block.TryInitialize(Header{.bitmap = 42}, kEntries, children), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(42));
  EXPECT_THAT(block.entries(), ElementsAre(3, 5, 7));
  EXPECT_THAT(block.children(), ElementsAre(&source, &block));
  EXPECT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(false));

  block.clear();
  EXPECT_THAT(block.empty(), Eq(true));
  EXPECT_THAT(block.TryInitialize(Header{.bitmap = 9}, {}, {}), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(9));
}

TEST_F(HamtPackedNodeBlockTest, EmptyAndClearedBlocksExposeEmptySpans) {
  Source source;
  Block block(source);
  const auto& const_block = block;
  EXPECT_THAT(block.entries(), IsEmpty());
  EXPECT_THAT(block.children(), IsEmpty());
  EXPECT_THAT(const_block.entries(), IsEmpty());
  EXPECT_THAT(const_block.children(), IsEmpty());
  ASSERT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(true));
  block.clear();
  block.clear();
  EXPECT_THAT(const_block.entries(), IsEmpty());
  EXPECT_THAT(const_block.children(), IsEmpty());
}

TEST_F(HamtPackedNodeBlockTest, HeaderAccessRequiresInitialization) {
  Source source;
  Block block(source);
  const auto& const_block = block;
  EXPECT_DEATH((void)block.header(), "requires an initialized block");
  EXPECT_DEATH((void)const_block.header(), "requires an initialized block");
}

TEST_F(HamtPackedNodeBlockTest, SourceExhaustionLeavesBlockEmptyAndDestructionReleasesStorage) {
  Source source;
  Block waiting(source);
  {
    Block owner(source);
    ASSERT_THAT(owner.TryInitialize(Header{}, {}, {}), Eq(true));
    EXPECT_THAT(waiting.TryInitialize(Header{}, {}, {}), Eq(false));
    EXPECT_THAT(waiting.empty(), Eq(true));
  }
  EXPECT_THAT(waiting.TryInitialize(Header{}, {}, {}), Eq(true));
}

}  // namespace
}  // namespace mbo::container::container_internal
