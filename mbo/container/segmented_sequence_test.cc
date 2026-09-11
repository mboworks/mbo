// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/segmented_sequence.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): tests exercise indexed access.

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::SizeIs;

constexpr SegmentedSequenceOptions kSmallSegments{
    .segment_capacities = {2, 3},
    .listed_capacities = 2,
    .repeat_last = true,
    .maximum_size = 11,
};

using IntSequence = SegmentedSequence<int, kSmallSegments>;

// NOLINTBEGIN(readability-identifier-naming): test double models BlockSource spelling.
struct MalformedBlockSource final {
  enum class Result { kNullData, kShortBlock, kWeakAlignment, kMisalignedData };

  static constexpr bool supports_recoverable_failure = true;

  Result result = Result::kNullData;
  int* releases = nullptr;
  alignas(int) std::array<std::byte, sizeof(int) * 3> storage{};

  static constexpr std::size_t max_alignment() noexcept { return alignof(int); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (result == Result::kNullData) {
      return mbo::memory::MemoryBlock{.data = nullptr, .size = size, .alignment = alignment};
    }
    if (result == Result::kShortBlock) {
      return mbo::memory::MemoryBlock{.data = storage.data(), .size = size - 1, .alignment = alignment};
    }
    if (result == Result::kWeakAlignment) {
      return mbo::memory::MemoryBlock{.data = storage.data(), .size = size, .alignment = 1};
    }
    return mbo::memory::MemoryBlock{.data = storage.data() + 1, .size = size, .alignment = alignment};
  }

  void Release(mbo::memory::MemoryBlock /*unused*/) const noexcept { ++*releases; }
};

// NOLINTEND(readability-identifier-naming)

constexpr SegmentedSequenceOptions kPageSegments{
    .segment_capacities = {64, 128},
    .listed_capacities = 2,
    .repeat_last = true,
    .maximum_size = 320,
};

using PageSequence = SegmentedSequence<std::uint64_t, kPageSegments>;

constexpr SegmentedSequenceOptions kRetainOneSegment{
    .segment_capacities = {2, 3},
    .listed_capacities = 2,
    .repeat_last = true,
    .maximum_size = 11,
    .retained_segment_limit = 1,
};

constexpr SegmentedSequenceOptions kRetainFourBytes{
    .segment_capacities = {2},
    .listed_capacities = 1,
    .repeat_last = true,
    .maximum_size = 8,
    .retained_byte_limit = sizeof(int),
};

static_assert(std::ranges::random_access_range<IntSequence>);
static_assert(std::ranges::random_access_range<const IntSequence>);
static_assert(!std::ranges::contiguous_range<IntSequence>);

struct SegmentedSequenceTest : ::testing::Test {};

TEST_F(SegmentedSequenceTest, OptionsRejectEmptyInvalidAndOverflowingCapacityLists) {
  SegmentedSequenceOptions options;
  EXPECT_THAT(options.IsValid(), Eq(true));

  options.listed_capacities = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.listed_capacities = options.segment_capacities.size() + 1;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.listed_capacities = 1;
  options.segment_capacities[0] = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_capacities[0] = 2;
  options.maximum_size = 1;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_capacities[0] = 1;
  options.maximum_size = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
}

TEST_F(SegmentedSequenceTest, GrowsAcrossListedAndRepeatedSegments) {
  IntSequence sequence;
  for (int value = 0; value < 10; ++value) {
    EXPECT_THAT(std::addressof(sequence.emplace_back(value)), NotNull());
  }

  EXPECT_THAT(sequence, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(sequence.size(), Eq(10));
  EXPECT_THAT(sequence.capacity(), Eq(11));
  EXPECT_THAT(sequence.front(), Eq(0));
  EXPECT_THAT(sequence.back(), Eq(9));
  EXPECT_THAT(sequence[7], Eq(7));
  EXPECT_THAT(sequence.at(8), Eq(8));
}

TEST_F(SegmentedSequenceTest, GrowthPreservesAddresses) {
  IntSequence sequence;
  int& first = sequence.emplace_back(1);
  int& second = sequence.emplace_back(2);
  int* const first_address = std::addressof(first);
  int* const second_address = std::addressof(second);

  sequence.reserve(11);
  EXPECT_THAT(std::addressof(sequence[0]), Eq(first_address));
  EXPECT_THAT(std::addressof(sequence[1]), Eq(second_address));
}

TEST_F(SegmentedSequenceTest, ConstructsAndAppendsRangesAcrossSegments) {
  const std::array initial = {1, 2, 3};
  IntSequence sequence(initial.begin(), initial.end());
  const std::array suffix = {4, 5, 6, 7};

  sequence.append_range(suffix);

  EXPECT_THAT(sequence, ElementsAre(1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT(sequence.capacity(), Eq(8));
}

TEST_F(SegmentedSequenceTest, CopyOwnsIndependentElements) {
  IntSequence source;
  source.push_back(1);
  source.push_back(2);
  source.push_back(3);

  IntSequence copy(source);
  EXPECT_THAT(copy, ElementsAre(1, 2, 3));
  EXPECT_THAT(std::addressof(copy.front()), Ne(std::addressof(source.front())));

  copy.front() = 4;
  EXPECT_THAT(source, ElementsAre(1, 2, 3));
  EXPECT_THAT(copy, ElementsAre(4, 2, 3));

  IntSequence assigned;
  assigned = source;
  EXPECT_THAT(assigned, ElementsAre(1, 2, 3));
  const IntSequence* const self = &assigned;
  assigned = *self;
  EXPECT_THAT(assigned, ElementsAre(1, 2, 3));
}

TEST_F(SegmentedSequenceTest, MoveTransfersElementAddresses) {
  IntSequence source;
  source.push_back(1);
  source.push_back(2);
  int* const first = std::addressof(source.front());

  IntSequence destination(std::move(source));
  EXPECT_THAT(destination, ElementsAre(1, 2));
  EXPECT_THAT(std::addressof(destination.front()), Eq(first));
  // The container contract explicitly specifies the moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(source, IsEmpty());
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(source.capacity(), Eq(0));
}

TEST_F(SegmentedSequenceTest, IteratorsAreDenseRandomAccess) {
  IntSequence sequence;
  for (int value = 0; value < 8; ++value) {
    sequence.push_back(value);
  }

  EXPECT_THAT(sequence.end() - sequence.begin(), Eq(8));
  EXPECT_THAT(sequence.begin()[5], Eq(5));
  EXPECT_THAT(*(sequence.end() - 2), Eq(6));
  EXPECT_THAT(sequence.begin() <=> sequence.end(), Eq(std::strong_ordering::less));
  auto iterator = sequence.begin();
  EXPECT_THAT(*(iterator++), Eq(0));
  EXPECT_THAT(*(++iterator), Eq(2));
  EXPECT_THAT(*(iterator--), Eq(2));
  EXPECT_THAT(*(--iterator), Eq(0));
  EXPECT_THAT(*(3 + iterator), Eq(3));
  EXPECT_THAT(std::ranges::reverse_view(sequence), ElementsAre(7, 6, 5, 4, 3, 2, 1, 0));
}

TEST_F(SegmentedSequenceTest, PageMappedLookupSurvivesCopyMoveAndRegrowth) {
  PageSequence sequence;
  for (std::uint64_t value = 0; value < 257; ++value) {
    sequence.push_back(value);
  }
  ASSERT_THAT(sequence.directory_bytes_reserved(), Ne(0));
  EXPECT_THAT(sequence[0], Eq(0));
  EXPECT_THAT(sequence[63], Eq(63));
  EXPECT_THAT(sequence[64], Eq(64));
  EXPECT_THAT(sequence[191], Eq(191));
  EXPECT_THAT(sequence[192], Eq(192));
  EXPECT_THAT(sequence[256], Eq(256));

  PageSequence copy(sequence);
  EXPECT_THAT(copy, ElementsAreArray(sequence));
  PageSequence moved(std::move(copy));
  EXPECT_THAT(moved, ElementsAreArray(sequence));

  while (moved.size() > 63) {
    moved.pop_back();
  }
  moved.trim_capacity();
  EXPECT_THAT(moved.capacity(), Eq(64));
  moved.reserve(320);
  for (std::uint64_t value = 63; value < 320; ++value) {
    moved.push_back(value);
  }
  EXPECT_THAT(moved[63], Eq(63));
  EXPECT_THAT(moved[191], Eq(191));
  EXPECT_THAT(moved[319], Eq(319));
}

TEST_F(SegmentedSequenceTest, SegmentSpansExposeOnlyConstructedPrefixes) {
  IntSequence sequence;
  for (int value = 0; value < 7; ++value) {
    sequence.push_back(value);
  }

  const auto segments = sequence.segments();
  ASSERT_THAT(segments, SizeIs(3));
  EXPECT_THAT(segments[0], ElementsAre(0, 1));
  EXPECT_THAT(segments[1], ElementsAre(2, 3, 4));
  EXPECT_THAT(segments[2], ElementsAre(5, 6));
}

TEST_F(SegmentedSequenceTest, PopClearAndReleaseRespectCapacity) {
  IntSequence sequence;
  sequence.reserve(8);
  sequence.push_back(1);
  sequence.push_back(2);
  sequence.push_back(3);
  const std::size_t reserved = sequence.capacity();

  EXPECT_THAT(sequence.pop_back_value(), Eq(3));
  sequence.pop_back();
  EXPECT_THAT(sequence, ElementsAre(1));
  EXPECT_THAT(sequence.capacity(), Eq(reserved));

  sequence.clear();
  EXPECT_THAT(sequence, IsEmpty());
  EXPECT_THAT(sequence.capacity(), Eq(reserved));

  sequence.release();
  EXPECT_THAT(sequence, IsEmpty());
  EXPECT_THAT(sequence.capacity(), Eq(0));
}

TEST_F(SegmentedSequenceTest, TrimCapacityReleasesOnlyEmptyTailSegments) {
  IntSequence empty;
  empty.trim_capacity();
  empty.trim_capacity(1);
  EXPECT_THAT(empty.segments(), IsEmpty());

  IntSequence sequence;
  sequence.reserve(11);
  for (int value = 0; value < 6; ++value) {
    sequence.push_back(value);
  }
  sequence.pop_back();

  sequence.trim_capacity();
  EXPECT_THAT(sequence, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(sequence.capacity(), Eq(5));

  sequence.trim_capacity(1);
  EXPECT_THAT(sequence.capacity(), Eq(5));
  sequence.trim_capacity(10);
  EXPECT_THAT(sequence.capacity(), Eq(5));
  sequence.reserve(11);
  sequence.trim_capacity(10);
  EXPECT_THAT(sequence.capacity(), Eq(11));
  sequence.trim_capacity(5);
  EXPECT_THAT(sequence.capacity(), Eq(5));
}

TEST_F(SegmentedSequenceTest, NonRepeatingCapacityListStopsGrowth) {
  constexpr SegmentedSequenceOptions kSingleSegment{
      .segment_capacities = {1},
      .listed_capacities = 1,
      .repeat_last = false,
      .maximum_size = 2,
  };
  alignas(int) std::array<std::byte, sizeof(int) * 2> storage{};
  SegmentedSequence<int, kSingleSegment, mbo::memory::FixedBlockSource> sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(storage), alignof(int)));

  ASSERT_THAT(sequence.try_push_back(1), Optional(_));
  EXPECT_THAT(sequence.try_push_back(2), Eq(std::nullopt));
  EXPECT_THAT(sequence, ElementsAre(1));
}

TEST_F(SegmentedSequenceTest, RetainedSegmentLimitPreservesNearestFutureSegment) {
  SegmentedSequence<int, kRetainOneSegment> sequence;
  sequence.reserve(11);
  for (int value = 0; value < 11; ++value) {
    sequence.push_back(value);
  }
  int* const first = std::addressof(sequence.front());

  while (!sequence.empty()) {
    sequence.pop_back();
  }

  EXPECT_THAT(sequence.capacity(), Eq(2));
  EXPECT_THAT(sequence.retained_segment_count(), Eq(1));
  EXPECT_THAT(sequence.retained_bytes(), Eq(sizeof(int) * 2));
  sequence.push_back(42);
  EXPECT_THAT(std::addressof(sequence.front()), Eq(first));
}

TEST_F(SegmentedSequenceTest, RetainedByteLimitCanBeStricterThanCountLimit) {
  SegmentedSequence<int, kRetainFourBytes> sequence;
  sequence.reserve(8);
  sequence.resize(8, 7);

  sequence.clear();

  EXPECT_THAT(sequence.capacity(), Eq(0));
  EXPECT_THAT(sequence.retained_segment_count(), Eq(0));
  EXPECT_THAT(sequence.retained_bytes(), Eq(0));
}

TEST_F(SegmentedSequenceTest, RetentionAccountingSurvivesMoveAndTrim) {
  SegmentedSequence<int, kRetainOneSegment> source;
  source.reserve(11);
  source.resize(6, 7);

  source.pop_back();
  EXPECT_THAT(source.capacity(), Eq(8));
  EXPECT_THAT(source.retained_segment_count(), Eq(1));
  EXPECT_THAT(source.retained_bytes(), Eq(sizeof(int) * 3));

  SegmentedSequence<int, kRetainOneSegment> moved(std::move(source));
  EXPECT_THAT(moved.retained_segment_count(), Eq(1));
  EXPECT_THAT(moved.retained_bytes(), Eq(sizeof(int) * 3));
  // The container contract explicitly specifies the moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_THAT(source.retained_segment_count(), Eq(0));
  moved.trim_capacity();
  EXPECT_THAT(moved.capacity(), Eq(5));
  EXPECT_THAT(moved.retained_segment_count(), Eq(0));
  EXPECT_THAT(moved.retained_bytes(), Eq(0));
}

TEST_F(SegmentedSequenceTest, ResizeConstructsAndDestroysSuffix) {
  IntSequence sequence;
  sequence.resize(7, 42);
  EXPECT_THAT(sequence, ElementsAre(42, 42, 42, 42, 42, 42, 42));

  sequence.resize(3);
  EXPECT_THAT(sequence, ElementsAre(42, 42, 42));
  sequence.resize(5);
  EXPECT_THAT(sequence, ElementsAre(42, 42, 42, 0, 0));
  sequence.resize(2, 9);
  EXPECT_THAT(sequence, ElementsAre(42, 42));
}

TEST_F(SegmentedSequenceTest, PmrBackedCopyUsesTheSameResource) {
  std::pmr::monotonic_buffer_resource resource;
  using PmrSequence = SegmentedSequence<int, kSmallSegments, mbo::memory::PmrBlockSource>;
  PmrSequence source{mbo::memory::PmrBlockSource(&resource)};
  source.push_back(1);
  source.push_back(2);

  const PmrSequence copy(source);

  EXPECT_THAT(copy, ElementsAre(1, 2));
  EXPECT_THAT(std::addressof(copy.front()), Ne(std::addressof(source.front())));
}

TEST_F(SegmentedSequenceTest, TryAppendRejectsEveryMalformedSourceBlock) {
  constexpr SegmentedSequenceOptions kOneSegment{
      .segment_capacities = {2},
      .listed_capacities = 1,
      .repeat_last = false,
      .maximum_size = 2,
  };
  constexpr std::array kResults{
      MalformedBlockSource::Result::kNullData,
      MalformedBlockSource::Result::kShortBlock,
      MalformedBlockSource::Result::kWeakAlignment,
      MalformedBlockSource::Result::kMisalignedData,
  };
  for (const auto result : kResults) {
    int releases = 0;
    SegmentedSequence<int, kOneSegment, MalformedBlockSource> sequence(MalformedBlockSource{
        .result = result,
        .releases = &releases,
    });

    EXPECT_THAT(sequence.try_push_back(1), Eq(std::nullopt));
    EXPECT_THAT(sequence, IsEmpty());
    EXPECT_THAT(releases, Eq(1));
  }
}

TEST_F(SegmentedSequenceTest, TryAppendRejectsCapacityAndByteSizeExhaustion) {
  constexpr SegmentedSequenceOptions kMaximumReached{
      .segment_capacities = {2},
      .listed_capacities = 1,
      .repeat_last = true,
      .maximum_size = 2,
  };
  alignas(int) std::array<std::byte, sizeof(int) * 2> maximum_storage{};
  SegmentedSequence<int, kMaximumReached, mbo::memory::FixedBlockSource> maximum_sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(maximum_storage), alignof(int)));
  maximum_sequence.push_back(1);
  maximum_sequence.push_back(2);
  EXPECT_THAT(maximum_sequence.try_push_back(3), Eq(std::nullopt));

  constexpr SegmentedSequenceOptions kRemainderTooSmall{
      .segment_capacities = {2},
      .listed_capacities = 1,
      .repeat_last = true,
      .maximum_size = 3,
  };
  alignas(int) std::array<std::byte, sizeof(int) * 4> remainder_storage{};
  SegmentedSequence<int, kRemainderTooSmall, mbo::memory::FixedBlockSource> remainder_sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(remainder_storage), alignof(int)));
  remainder_sequence.push_back(1);
  remainder_sequence.push_back(2);
  EXPECT_THAT(remainder_sequence.try_push_back(3), Eq(std::nullopt));

  constexpr SegmentedSequenceOptions kByteSizeOverflow{
      .segment_capacities = {(std::numeric_limits<std::size_t>::max() / sizeof(int)) + 1},
      .listed_capacities = 1,
      .repeat_last = false,
  };
  std::array<std::byte, 1> overflow_storage{};
  SegmentedSequence<int, kByteSizeOverflow, mbo::memory::FixedBlockSource> overflow_sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(overflow_storage), alignof(int)));
  EXPECT_THAT(overflow_sequence.try_push_back(1), Eq(std::nullopt));
}

TEST_F(SegmentedSequenceTest, TryAppendReportsFixedSourceExhaustionWithoutMutation) {
  alignas(int) std::array<std::byte, sizeof(int) * 2> storage{};
  constexpr SegmentedSequenceOptions kFixedOptions{
      .segment_capacities = {2, 2},
      .listed_capacities = 2,
      .repeat_last = false,
      .maximum_size = 4,
  };
  using FixedSequence = SegmentedSequence<int, kFixedOptions, mbo::memory::FixedBlockSource>;
  FixedSequence sequence(mbo::memory::FixedBlockSource(std::span<std::byte>(storage), alignof(int)));

  const auto first = sequence.try_push_back(1);
  ASSERT_THAT(first, Optional(_));
  // The immediately preceding fatal assertion proves the optional engaged.
  EXPECT_THAT(
      std::addressof(first.value().get()),  // NOLINT(bugprone-unchecked-optional-access)
      Eq(std::addressof(sequence.front())));
  const auto second = sequence.try_push_back(2);
  ASSERT_THAT(second, Optional(_));
  // The immediately preceding fatal assertion proves the optional engaged.
  EXPECT_THAT(
      std::addressof(second.value().get()),  // NOLINT(bugprone-unchecked-optional-access)
      Eq(std::addressof(sequence.back())));
  EXPECT_THAT(sequence.try_push_back(3), Eq(std::nullopt));
  EXPECT_THAT(sequence, ElementsAre(1, 2));
}

TEST_F(SegmentedSequenceTest, DestructionCoversEveryConstructedElement) {
  struct Counted final {
    explicit Counted(int& live_count) : live(&live_count) { ++*live; }

    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;

    Counted(Counted&& other) noexcept : live(std::exchange(other.live, nullptr)) {}

    Counted& operator=(Counted&&) = delete;

    ~Counted() {
      if (live != nullptr) {
        --*live;
      }
    }

    int* live;
  };

  int live = 0;
  {
    SegmentedSequence<Counted, kSmallSegments> sequence;
    sequence.emplace_back(live);
    sequence.emplace_back(live);
    sequence.emplace_back(live);
    EXPECT_THAT(live, Eq(3));
    sequence.pop_back();
    EXPECT_THAT(live, Eq(2));
  }
  EXPECT_THAT(live, Eq(0));
}

}  // namespace
}  // namespace mbo::container

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
