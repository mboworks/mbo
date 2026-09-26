// SPDX-FileCopyrightText: Copyright (c) M. Boerger, The MBO Works Authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::ThrowsMessage;

constexpr SegmentedVectorOptions kTwoSegments{
    .segment_size = 2,
    .segment_capacity = 4,
};

constexpr SegmentedVectorOptions kBoundedTwoSegments{
    .segment_size = 2,
    .segment_capacity = 2,
};

struct SegmentedVectorRequireExceptionsTest : ::testing::Test {};

// NOLINTBEGIN(readability-identifier-naming): test double models BlockSource spelling.
struct CountingBlockSource final {
  static constexpr bool supports_recoverable_failure = true;

  int* acquisitions = nullptr;
  int* releases = nullptr;

  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const {
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      ++*acquisitions;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) const noexcept {
    ++*releases;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }
};

struct OneBlockSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) {
    if (acquired) {
      return std::nullopt;
    }
    acquired = true;
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    acquired = false;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  bool acquired = false;
};

// NOLINTEND(readability-identifier-naming)

struct ThrowingElement final {
  static inline int live = 0;
  static inline int successful_constructions_before_throw = -1;

  explicit ThrowingElement(int value = 0) : value(value) { OnConstruction(); }

  ThrowingElement(const ThrowingElement& other) : value(other.value) { OnConstruction(); }

  ThrowingElement& operator=(const ThrowingElement&) = delete;
  ThrowingElement(ThrowingElement&&) = delete;
  ThrowingElement& operator=(ThrowingElement&&) = delete;

  ~ThrowingElement() noexcept { --live; }

  static void Arm(int successful_constructions) noexcept {
    successful_constructions_before_throw = successful_constructions;
  }

  static void Disarm() noexcept { successful_constructions_before_throw = -1; }

  int value;

 private:
  static void OnConstruction() {
    if (successful_constructions_before_throw == 0) {
      throw std::runtime_error("construction failed");
    }
    if (successful_constructions_before_throw > 0) {
      --successful_constructions_before_throw;
    }
    ++live;
  }
};

struct MutatesThenThrowsOnMove final {
  explicit MutatesThenThrowsOnMove(int value) : value(value) {}

  MutatesThenThrowsOnMove(const MutatesThenThrowsOnMove&) = delete;
  MutatesThenThrowsOnMove& operator=(const MutatesThenThrowsOnMove&) = delete;

  // Intentionally throws to exercise the aliased-rvalue rollback caveat.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  MutatesThenThrowsOnMove(MutatesThenThrowsOnMove&& other) : value(other.value) {
    other.value = -1;
    throw std::runtime_error("move failed");
  }

  MutatesThenThrowsOnMove& operator=(MutatesThenThrowsOnMove&&) = delete;

  ~MutatesThenThrowsOnMove() = default;

  int value;
};

static_assert(noexcept(std::declval<SegmentedVector<int>&>().at(0)) == !config::kRequireThrows);
static_assert(noexcept(std::declval<SegmentedVector<int>&>().front()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<SegmentedVector<int>&>().back()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<SegmentedVector<int>&>().pop_back()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<SegmentedVector<int>&>().pop_back_value()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<const SegmentedVector<int>&>().at(0)) == !config::kRequireThrows);
static_assert(noexcept(std::declval<const SegmentedVector<int>&>().front()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<const SegmentedVector<int>&>().back()) == !config::kRequireThrows);

TEST_F(SegmentedVectorRequireExceptionsTest, AtPropagatesOutOfRangeRequirement) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int> sequence;

  EXPECT_THAT(
      [&sequence] { static_cast<void>(sequence.at(0)); }, ThrowsMessage<std::runtime_error>(HasSubstr("out of range")));
}

TEST_F(SegmentedVectorRequireExceptionsTest, FrontPropagatesEmptyRequirement) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int> sequence;

  EXPECT_THAT(
      [&sequence] { static_cast<void>(sequence.front()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("out of range")));
}

TEST_F(SegmentedVectorRequireExceptionsTest, BackPropagatesEmptyRequirement) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int> sequence;

  EXPECT_THAT(
      [&sequence] { static_cast<void>(sequence.back()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("out of range")));
}

TEST_F(SegmentedVectorRequireExceptionsTest, PopBackPropagatesEmptyRequirement) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int> sequence;

  EXPECT_THAT([&sequence] { sequence.pop_back(); }, ThrowsMessage<std::runtime_error>(HasSubstr("Cannot pop")));
}

TEST_F(SegmentedVectorRequireExceptionsTest, PopBackValuePropagatesEmptyRequirement) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int> sequence;

  EXPECT_THAT(
      [&sequence] { static_cast<void>(sequence.pop_back_value()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("out of range")));
}

TEST_F(SegmentedVectorRequireExceptionsTest, ReserveRejectsGrowthBeyondSegmentCapacity) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int, kBoundedTwoSegments> sequence;

  EXPECT_THAT([&sequence] { sequence.reserve(5); }, ThrowsMessage<std::runtime_error>(HasSubstr("maximum capacity")));
  EXPECT_THAT(sequence, IsEmpty());
}

TEST_F(SegmentedVectorRequireExceptionsTest, ResizeRejectsGrowthBeyondSegmentCapacity) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int, kBoundedTwoSegments> sequence;

  EXPECT_THAT([&sequence] { sequence.resize(5); }, ThrowsMessage<std::runtime_error>(HasSubstr("maximum capacity")));
  EXPECT_THAT(sequence, IsEmpty());
}

TEST_F(SegmentedVectorRequireExceptionsTest, SizedAppendRejectsGrowthBeyondSegmentCapacity) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int, kBoundedTwoSegments> sequence;
  const std::array values = {1, 2, 3, 4, 5};

  EXPECT_THAT(
      ([&sequence, &values] { sequence.append_range(values); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("maximum capacity")));
  EXPECT_THAT(sequence, IsEmpty());
}

TEST_F(SegmentedVectorRequireExceptionsTest, ReserveRollsBackNewSegmentsAfterAllocationFailure) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int, kTwoSegments, OneBlockSource> sequence;

  EXPECT_THAT([&sequence] { sequence.reserve(5); }, ThrowsMessage<std::runtime_error>(HasSubstr("allocation failed")));
  EXPECT_THAT(sequence, IsEmpty());
  EXPECT_THAT(sequence.capacity(), 0);
  EXPECT_THAT(sequence.segment_count(), 0);
}

TEST_F(SegmentedVectorRequireExceptionsTest, SizedAppendRollsBackSegmentsAfterAllocationFailure) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  SegmentedVector<int, kTwoSegments, OneBlockSource> sequence;
  const std::array values = {1, 2, 3};

  EXPECT_THAT(
      ([&sequence, &values] { sequence.append_range(values); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("allocation failed")));
  EXPECT_THAT(sequence, IsEmpty());
  EXPECT_THAT(sequence.capacity(), 0);
  EXPECT_THAT(sequence.segment_count(), 0);
}

TEST_F(SegmentedVectorRequireExceptionsTest, ValueResizeRollsBackAfterConstructionFailure) {
  ASSERT_THAT(ThrowingElement::live, Eq(0));
  constexpr SegmentedVectorOptions kResizeOptions{
      .segment_size = 2,
      .segment_capacity = 2,
  };
  {
    SegmentedVector<ThrowingElement, kResizeOptions> sequence;
    sequence.emplace_back(1);
    const ThrowingElement value(9);
    ThrowingElement::Arm(1);

    EXPECT_THAT(
        ([&sequence, &value] { sequence.resize(4, value); }),
        ThrowsMessage<std::runtime_error>(HasSubstr("construction failed")));

    ThrowingElement::Disarm();
    ASSERT_THAT(sequence, SizeIs(1));
    EXPECT_THAT(sequence.front().value, Eq(1));
    EXPECT_THAT(sequence.capacity(), Eq(2));
    EXPECT_THAT(sequence.segment_count(), Eq(1));
    EXPECT_THAT(ThrowingElement::live, Eq(2));
  }
  EXPECT_THAT(ThrowingElement::live, Eq(0));
}

TEST_F(SegmentedVectorRequireExceptionsTest, DefaultResizeRollsBackAfterConstructionFailure) {
  ASSERT_THAT(ThrowingElement::live, Eq(0));
  constexpr SegmentedVectorOptions kResizeOptions{
      .segment_size = 2,
      .segment_capacity = 2,
  };
  {
    SegmentedVector<ThrowingElement, kResizeOptions> sequence;
    sequence.emplace_back(1);
    ThrowingElement::Arm(1);

    EXPECT_THAT(
        ([&sequence] { sequence.resize(4); }), ThrowsMessage<std::runtime_error>(HasSubstr("construction failed")));

    ThrowingElement::Disarm();
    ASSERT_THAT(sequence, SizeIs(1));
    EXPECT_THAT(sequence.front().value, Eq(1));
    EXPECT_THAT(sequence.capacity(), Eq(2));
    EXPECT_THAT(sequence.segment_count(), Eq(1));
    EXPECT_THAT(ThrowingElement::live, Eq(1));
  }
  EXPECT_THAT(ThrowingElement::live, Eq(0));
}

TEST_F(SegmentedVectorRequireExceptionsTest, AppendRangeRestoresNonemptySequenceAfterConstructionFailure) {
  ASSERT_THAT(ThrowingElement::live, Eq(0));
  {
    SegmentedVector<ThrowingElement, kTwoSegments> sequence;
    sequence.emplace_back(7);
    const std::array source = {ThrowingElement(1), ThrowingElement(2), ThrowingElement(3)};
    ThrowingElement::Arm(1);

    EXPECT_THAT(
        ([&sequence, &source] { sequence.append_range(source); }),
        ThrowsMessage<std::runtime_error>(HasSubstr("construction failed")));

    ThrowingElement::Disarm();
    ASSERT_THAT(sequence, SizeIs(1));
    EXPECT_THAT(sequence.front().value, Eq(7));
    EXPECT_THAT(sequence.capacity(), Eq(2));
    EXPECT_THAT(sequence.segment_count(), Eq(1));
    EXPECT_THAT(ThrowingElement::live, Eq(4));
  }
  EXPECT_THAT(ThrowingElement::live, Eq(0));
}

TEST_F(SegmentedVectorRequireExceptionsTest, AliasedThrowingMoveRestoresStructureButCanModifySourceElement) {
  SegmentedVector<MutatesThenThrowsOnMove, kTwoSegments> sequence;
  sequence.emplace_back(1);
  sequence.emplace_back(2);

  EXPECT_THAT(
      ([&sequence] { sequence.push_back(std::move(sequence.front())); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("move failed")));

  ASSERT_THAT(sequence, SizeIs(2));
  EXPECT_THAT(sequence.front().value, Eq(-1));
  EXPECT_THAT(sequence.back().value, Eq(2));
  EXPECT_THAT(sequence.capacity(), Eq(2));
  EXPECT_THAT(sequence.segment_count(), Eq(1));
}

TEST_F(SegmentedVectorRequireExceptionsTest, FailedRangeConstructionDestroysElementsAndReleasesStorage) {
  ASSERT_THAT(ThrowingElement::live, Eq(0));
  int acquisitions = 0;
  int releases = 0;
  {
    const std::array source = {ThrowingElement(1), ThrowingElement(2), ThrowingElement(3)};
    ThrowingElement::Arm(1);

    EXPECT_THAT(
        ([&source, &acquisitions, &releases] {
          return SegmentedVector<ThrowingElement, kTwoSegments, CountingBlockSource>(
              std::from_range, source, CountingBlockSource{.acquisitions = &acquisitions, .releases = &releases});
        }),
        ThrowsMessage<std::runtime_error>(HasSubstr("construction failed")));

    ThrowingElement::Disarm();
    EXPECT_THAT(ThrowingElement::live, Eq(3));
    EXPECT_THAT(acquisitions, Eq(2));
    EXPECT_THAT(releases, Eq(acquisitions));
  }
  EXPECT_THAT(ThrowingElement::live, Eq(0));
}

}  // namespace
}  // namespace mbo::container
