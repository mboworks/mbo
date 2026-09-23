// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/segmented_sequence.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): tests exercise indexed access.

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::SizeIs;

constexpr SegmentedSequenceOptions kSmallSegments{
    .segment_size = 2,
    .segment_capacity = 8,
    .segment_reservation = 8,
};

using IntSequence = SegmentedSequence<int, kSmallSegments>;

// NOLINTBEGIN(readability-identifier-naming): test double models BlockSource spelling.
struct MalformedBlockSource final {
  enum class Result { kNullData, kShortBlock, kWeakAlignment, kMisalignedData };

  static constexpr bool supports_recoverable_failure = true;

  Result result = Result::kNullData;
  int* releases = nullptr;
  alignas(128) std::array<std::byte, 4'096> storage{};

  static constexpr std::size_t max_alignment() noexcept { return 128; }

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

struct RecordingBlockSource final {
  static constexpr bool supports_recoverable_failure = true;

  int* acquisitions = nullptr;
  int* releases = nullptr;

  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const noexcept {
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

struct ThrowingDestructor final {
  ThrowingDestructor() = default;
  ThrowingDestructor(const ThrowingDestructor&) = default;
  ThrowingDestructor& operator=(const ThrowingDestructor&) = default;
  ThrowingDestructor(ThrowingDestructor&&) = default;
  ThrowingDestructor& operator=(ThrowingDestructor&&) = default;

  // NOLINTNEXTLINE(modernize-use-equals-default): user-provided body preserves the throwing trait on GCC.
  ~ThrowingDestructor() noexcept(false) {}
};

struct alignas(128) OverAlignedElement final {
  explicit OverAlignedElement(int value) : value(value) {}

  int value;
};

struct IncompleteElement;

struct DirectoryAllocationState final {
  std::array<int, 8> allocations{};
  std::array<int, 8> deallocations{};
  std::array<std::size_t, 8> request_counts{};
  std::array<std::array<std::size_t, 16>, 8> requested_sizes{};
};

template<typename T>
struct StatefulDirectoryAllocator final {
  using value_type = T;
  using propagate_on_container_copy_assignment = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_swap = std::false_type;
  using is_always_equal = std::false_type;

  template<typename U>
  struct rebind final {
    using other = StatefulDirectoryAllocator<U>;
  };

  constexpr StatefulDirectoryAllocator() noexcept = default;

  constexpr StatefulDirectoryAllocator(DirectoryAllocationState* state, int allocator_id) noexcept
      : state(state), id(allocator_id) {}

  template<typename U>
  constexpr explicit StatefulDirectoryAllocator(const StatefulDirectoryAllocator<U>& other) noexcept
      : state(other.state), id(other.id) {}

  T* allocate(std::size_t count) {
    if (state != nullptr) {
      const auto index = static_cast<std::size_t>(id);
      ++state->allocations.at(index);
      const std::size_t request_index = state->request_counts.at(index)++;
      state->requested_sizes.at(index).at(request_index) = count;
    }
    return std::allocator<T>{}.allocate(count);
  }

  void deallocate(T* data, std::size_t count) noexcept {
    if (state != nullptr) {
      ++state->deallocations.at(static_cast<std::size_t>(id));
    }
    std::allocator<T>{}.deallocate(data, count);
  }

  constexpr StatefulDirectoryAllocator select_on_container_copy_construction() const noexcept {
    return StatefulDirectoryAllocator(state, id + 2);
  }

  template<typename U>
  friend constexpr bool operator==(
      const StatefulDirectoryAllocator& lhs,
      const StatefulDirectoryAllocator<U>& rhs) noexcept {
    return lhs.state == rhs.state && lhs.id == rhs.id;
  }

  template<typename>
  friend struct StatefulDirectoryAllocator;

  DirectoryAllocationState* state = nullptr;
  int id = 0;
};

template<typename T>
struct PropagatingCopyAllocator final {
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using is_always_equal = std::true_type;

  template<typename U>
  struct rebind final {
    using other = PropagatingCopyAllocator<U>;
  };

  constexpr PropagatingCopyAllocator() noexcept = default;

  template<typename U>
  constexpr explicit PropagatingCopyAllocator(const PropagatingCopyAllocator<U>& /*other*/) noexcept {}

  T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }

  void deallocate(T* data, std::size_t count) noexcept { std::allocator<T>{}.deallocate(data, count); }

  friend constexpr bool operator==(const PropagatingCopyAllocator&, const PropagatingCopyAllocator&) = default;
};

// NOLINTEND(readability-identifier-naming)

static_assert(std::ranges::random_access_range<IntSequence>);
static_assert(std::ranges::random_access_range<const IntSequence>);
static_assert(!std::ranges::contiguous_range<IntSequence>);
static_assert(std::ranges::random_access_range<IntSequence::segment_view>);
static_assert(std::ranges::random_access_range<IntSequence::const_segment_view>);
static_assert(!std::ranges::contiguous_range<IntSequence::segment_view>);
static_assert(!std::ranges::contiguous_range<IntSequence::const_segment_view>);
static_assert(std::same_as<std::ranges::range_reference_t<IntSequence::segment_view>, int&>);
static_assert(std::same_as<std::ranges::range_reference_t<IntSequence::const_segment_view>, const int&>);
static_assert(std::equality_comparable_with<IntSequence::iterator, IntSequence::const_iterator>);
static_assert(std::sized_sentinel_for<IntSequence::iterator, IntSequence::const_iterator>);
static_assert(std::sized_sentinel_for<IntSequence::const_iterator, IntSequence::iterator>);
static_assert(SegmentedSequenceElement<int>);
static_assert(!SegmentedSequenceElement<const int>);
static_assert(!SegmentedSequenceElement<volatile int>);
static_assert(!SegmentedSequenceElement<std::remove_const_t<decltype("x")>>);
static_assert(!SegmentedSequenceElement<ThrowingDestructor>);
static_assert(!SegmentedSequenceElement<void>);
static_assert(!SegmentedSequenceElement<IncompleteElement>);

constexpr SegmentedSequenceOptions kUnrepresentableSegment{
    .segment_size = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1),
};
static_assert(kUnrepresentableSegment.IsValid());
static_assert(!RepresentableSegmentedSequenceOptions<int, kUnrepresentableSegment>);

constexpr SegmentedSequenceOptions kUnrepresentableDirectory{
    .segment_size = 2,
    .segment_capacity = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1),
};
static_assert(kUnrepresentableDirectory.IsValid());
static_assert(!RepresentableSegmentedSequenceOptions<int, kUnrepresentableDirectory>);

constexpr SegmentedSequenceOptions kUnrepresentableReservation{
    .segment_size = 2,
    .segment_reservation = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1),
};
static_assert(kUnrepresentableReservation.IsValid());
static_assert(!RepresentableSegmentedSequenceOptions<int, kUnrepresentableReservation>);

constexpr SegmentedSequenceOptions kConstexprOptions{
    .segment_size = 2,
    .segment_capacity = 4,
};

constexpr bool ConstexprSegmentedSequenceWorks() {
  SegmentedSequence<int, kConstexprOptions> sequence;
  sequence.push_back(1);
  sequence.push_back(2);
  sequence.push_back(3);
  sequence.push_back(4);
  sequence.push_back(5);
  if (sequence.size() != 5 || sequence.capacity() != 6 || sequence.segment_count() != 3 || sequence[3] != 4
      || sequence.bytes_reserved() == 0) {
    return false;
  }
  const SegmentedSequence<int, kConstexprOptions> copy(sequence);
  if (copy.size() != 5 || copy.front() != 1 || copy.back() != 5) {
    return false;
  }
  sequence.pop_back();
  sequence.clear();
  return sequence.empty() && sequence.capacity() == 6;
}

static_assert(ConstexprSegmentedSequenceWorks());

constexpr bool ConstexprCustomSourceIsNotBypassed() {
  using Sequence = SegmentedSequence<int, kConstexprOptions, mbo::memory::InlineBlockSource<4'096, 128>>;
  Sequence sequence;
  return !sequence.try_push_back(1).has_value() && sequence.empty() && sequence.capacity() == 0;
}

static_assert(ConstexprCustomSourceIsNotBypassed());

constexpr SegmentedSequenceOptions kNoDirectoryReservation{
    .segment_reservation = 0,
};
static_assert(!std::is_nothrow_default_constructible_v<SegmentedSequence<int>>);
static_assert(std::is_nothrow_default_constructible_v<SegmentedSequence<int, kNoDirectoryReservation>>);

template<typename Sequence, typename... Args>
concept CanEmplaceBack =
    requires(Sequence& sequence, Args&&... args) { sequence.emplace_back(std::forward<Args>(args)...); };

static_assert(CanEmplaceBack<IntSequence, int>);
static_assert(!CanEmplaceBack<IntSequence, std::string>);

using PropagatingCopySequence =
    SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, PropagatingCopyAllocator<std::byte>>;
static_assert(std::copy_constructible<PropagatingCopySequence>);
static_assert(!std::is_copy_assignable_v<PropagatingCopySequence>);

struct SegmentedSequenceTest : ::testing::Test {};

TEST_F(SegmentedSequenceTest, OptionsRequirePowerOfTwoSegmentSizeAndFiniteCapacity) {
  SegmentedSequenceOptions options;
  EXPECT_THAT(options.IsValid(), Eq(true));

  options.segment_size = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_size = 3;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_size = 2;
  options.segment_capacity = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_capacity = 3;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_capacity = 1;
  EXPECT_THAT(options.IsValid(), Eq(true));
  options.segment_reservation = 2;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_reservation = 3;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.segment_reservation = 0;
  EXPECT_THAT(options.IsValid(), Eq(true));
  options.segment_capacity = std::numeric_limits<std::size_t>::max();
  EXPECT_THAT(options.IsValid(), Eq(true));
}

TEST_F(SegmentedSequenceTest, GrowsAcrossFixedCapacitySegments) {
  IntSequence sequence;
  for (int value = 0; value < 10; ++value) {
    EXPECT_THAT(std::addressof(sequence.emplace_back(value)), NotNull());
  }

  EXPECT_THAT(sequence, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_THAT(sequence, SizeIs(10));
  EXPECT_THAT(sequence.capacity(), Eq(10));
  EXPECT_THAT(sequence.front(), Eq(0));
  EXPECT_THAT(sequence.back(), Eq(9));
  EXPECT_THAT(sequence[7], Eq(7));
  EXPECT_THAT(sequence.at(8), Eq(8));
}

TEST_F(SegmentedSequenceTest, ReservationEqualToCapacityAvoidsDirectoryGrowth) {
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, Allocator>;
  DirectoryAllocationState state;
  Sequence sequence(std::allocator_arg, Allocator(&state, 1));
  ASSERT_THAT(state.request_counts[1], Eq(1));
  EXPECT_THAT(state.requested_sizes[1][0], Eq(8));

  for (int value = 0; value < 11; ++value) {
    sequence.push_back(value);
  }

  EXPECT_THAT(sequence, SizeIs(11));
  EXPECT_THAT(sequence.segment_count(), Eq(6));
  EXPECT_THAT(state.request_counts[1], Eq(1));
}

TEST_F(SegmentedSequenceTest, UnboundedDirectoryGrowthRequestsPowerOfTwoThresholds) {
  constexpr SegmentedSequenceOptions kUnboundedTwo{
      .segment_size = 2,
      .segment_reservation = 0,
  };
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kUnboundedTwo, mbo::memory::NewDeleteBlockSource, Allocator>;
  DirectoryAllocationState state;
  Sequence sequence(std::allocator_arg, Allocator(&state, 1));
  EXPECT_THAT(state.request_counts[1], Eq(0));

  for (int value = 0; value < 5; ++value) {
    sequence.push_back(value);
  }

  ASSERT_THAT(state.request_counts[1], Eq(3));
  EXPECT_THAT(state.requested_sizes[1][0], Eq(1));
  EXPECT_THAT(state.requested_sizes[1][1], Eq(2));
  EXPECT_THAT(state.requested_sizes[1][2], Eq(4));
}

TEST_F(SegmentedSequenceTest, DefaultDirectoryReservationRequestsOneSlot) {
  constexpr SegmentedSequenceOptions kDefaultReservation{
      .segment_size = 2,
  };
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kDefaultReservation, mbo::memory::NewDeleteBlockSource, Allocator>;
  DirectoryAllocationState state;

  const Sequence sequence(std::allocator_arg, Allocator(&state, 1));

  ASSERT_THAT(state.request_counts[1], Eq(1));
  EXPECT_THAT(state.requested_sizes[1][0], Eq(1));
  EXPECT_THAT(sequence, IsEmpty());
}

TEST_F(SegmentedSequenceTest, IntermediateReservationGrowsAtNextPowerOfTwoThreshold) {
  constexpr SegmentedSequenceOptions kReservedTwo{
      .segment_size = 2,
      .segment_capacity = 8,
      .segment_reservation = 2,
  };
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kReservedTwo, mbo::memory::NewDeleteBlockSource, Allocator>;
  DirectoryAllocationState state;
  Sequence sequence(std::allocator_arg, Allocator(&state, 1));

  for (int value = 0; value < 5; ++value) {
    sequence.push_back(value);
  }

  ASSERT_THAT(state.request_counts[1], Eq(2));
  EXPECT_THAT(state.requested_sizes[1][0], Eq(2));
  EXPECT_THAT(state.requested_sizes[1][1], Eq(4));
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

TEST_F(SegmentedSequenceTest, EachSegmentUsesOneSourceAllocationForHeaderAndSlots) {
  int acquisitions = 0;
  int releases = 0;
  SegmentedSequence<int, kSmallSegments, RecordingBlockSource> sequence(
      RecordingBlockSource{.acquisitions = &acquisitions, .releases = &releases});

  for (int value = 0; value < 5; ++value) {
    sequence.push_back(value);
  }

  EXPECT_THAT(sequence.segment_count(), Eq(3));
  EXPECT_THAT(acquisitions, Eq(3));
  sequence.release();
  EXPECT_THAT(releases, Eq(3));
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

TEST_F(SegmentedSequenceTest, CopySelectsDirectoryAllocatorThroughAllocatorTraits) {
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, Allocator>;
  static_assert(std::uses_allocator_v<Sequence, Allocator>);
  DirectoryAllocationState state;
  Sequence source(std::allocator_arg, Allocator(&state, 1));
  source.push_back(1);
  source.push_back(2);

  const Sequence copy(source);

  EXPECT_THAT(copy, ElementsAre(1, 2));
  EXPECT_THAT(copy.get_allocator().id, Eq(3));
  EXPECT_THAT(state.allocations[3], Eq(1));
}

TEST_F(SegmentedSequenceTest, AllocatorExtendedCopyUsesRequestedDirectoryAllocator) {
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, Allocator>;
  DirectoryAllocationState state;
  Sequence source(std::allocator_arg, Allocator(&state, 1));
  source.push_back(1);

  const Sequence copy(std::allocator_arg, Allocator(&state, 4), source);

  EXPECT_THAT(copy, ElementsAre(1));
  EXPECT_THAT(copy.get_allocator().id, Eq(4));
  EXPECT_THAT(state.allocations[4], Eq(1));
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

TEST_F(SegmentedSequenceTest, MoveAssignmentPreservesUnequalNonpropagatingDirectoryAllocators) {
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, Allocator>;
  static_assert(std::is_move_assignable_v<Sequence>);
  static_assert(!std::is_nothrow_move_assignable_v<Sequence>);
  DirectoryAllocationState state;
  Sequence destination(std::allocator_arg, Allocator(&state, 1));
  destination.push_back(9);
  Sequence source(std::allocator_arg, Allocator(&state, 2));
  source.push_back(1);
  source.push_back(2);
  int* const first = std::addressof(source.front());

  destination = std::move(source);

  EXPECT_THAT(destination, ElementsAre(1, 2));
  EXPECT_THAT(std::addressof(destination.front()), Eq(first));
  EXPECT_THAT(destination.get_allocator().id, Eq(1));
  // The container contract explicitly specifies the moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(source, IsEmpty());
}

TEST_F(SegmentedSequenceTest, SwapPreservesUnequalNonpropagatingDirectoryAllocators) {
  using Allocator = StatefulDirectoryAllocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::NewDeleteBlockSource, Allocator>;
  static_assert(std::is_swappable_v<Sequence>);
  static_assert(!std::is_nothrow_swappable_v<Sequence>);
  DirectoryAllocationState state;
  Sequence lhs(std::allocator_arg, Allocator(&state, 1));
  lhs.push_back(1);
  Sequence rhs(std::allocator_arg, Allocator(&state, 2));
  rhs.push_back(2);
  rhs.push_back(3);
  int* const lhs_first = std::addressof(lhs.front());
  int* const rhs_first = std::addressof(rhs.front());

  using std::swap;
  swap(lhs, rhs);

  EXPECT_THAT(lhs, ElementsAre(2, 3));
  EXPECT_THAT(rhs, ElementsAre(1));
  EXPECT_THAT(std::addressof(lhs.front()), Eq(rhs_first));
  EXPECT_THAT(std::addressof(rhs.front()), Eq(lhs_first));
  EXPECT_THAT(lhs.get_allocator().id, Eq(1));
  EXPECT_THAT(rhs.get_allocator().id, Eq(2));
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

  const IntSequence::const_iterator const_iterator = sequence.begin();
  EXPECT_THAT(const_iterator, Eq(sequence.begin()));
  EXPECT_THAT(sequence.end() - const_iterator, Eq(8));
  EXPECT_THAT(const_iterator - sequence.end(), Eq(-8));
}

TEST_F(SegmentedSequenceTest, ConstructsFromIteratorsAndAppendsSizedRange) {
  const std::array initial = {1, 2, 3};
  IntSequence sequence(initial.begin(), initial.end());
  const std::array suffix = {4, 5};
  sequence.append_range(suffix);

  EXPECT_THAT(sequence, ElementsAre(1, 2, 3, 4, 5));
}

TEST_F(SegmentedSequenceTest, AppendsAliasedSelfRange) {
  const std::array initial = {1, 2, 3};
  IntSequence sequence(std::from_range, initial);

  sequence.append_range(sequence);

  EXPECT_THAT(sequence, ElementsAre(1, 2, 3, 1, 2, 3));
}

TEST_F(SegmentedSequenceTest, AppendsSinglePassRange) {
  IntSequence sequence;
  std::istringstream input("6 7");

  sequence.append_range(std::ranges::istream_view<int>(input));

  EXPECT_THAT(sequence, ElementsAre(6, 7));
}

TEST_F(SegmentedSequenceTest, FromRangeMovesElements) {
  std::vector<std::unique_ptr<int>> pointers;
  pointers.push_back(std::make_unique<int>(8));
  pointers.push_back(std::make_unique<int>(9));
  using PointerSequence = SegmentedSequence<std::unique_ptr<int>, kSmallSegments>;
  PointerSequence moved(
      std::from_range,
      std::ranges::subrange(std::make_move_iterator(pointers.begin()), std::make_move_iterator(pointers.end())));
  ASSERT_THAT(moved, SizeIs(2));
  EXPECT_THAT(*moved[0], Eq(8));
  EXPECT_THAT(*moved[1], Eq(9));
  EXPECT_THAT(pointers[0], Eq(nullptr));
  EXPECT_THAT(pointers[1], Eq(nullptr));
}

TEST_F(SegmentedSequenceTest, FromRangeAcceptsCallerOwnedSource) {
  constexpr SegmentedSequenceOptions kFixedOptions{
      .segment_size = 2,
      .segment_capacity = 1,
  };
  alignas(128) std::array<std::byte, 4'096> storage{};
  const std::array values = {1, 2};
  const SegmentedSequence<int, kFixedOptions, mbo::memory::FixedBlockSource> sequence(
      std::from_range, values, mbo::memory::FixedBlockSource(std::span<std::byte>(storage), 128));

  EXPECT_THAT(sequence, ElementsAre(1, 2));
}

TEST_F(SegmentedSequenceTest, FromRangeDefaultConstructsImmovableInlineSource) {
  constexpr SegmentedSequenceOptions kFixedOptions{
      .segment_size = 2,
      .segment_capacity = 1,
  };
  const std::array values = {1, 2};
  using InlineSequence = SegmentedSequence<int, kFixedOptions, mbo::memory::InlineBlockSource<4'096, 128>>;
  const InlineSequence inline_sequence(std::from_range, values);
  EXPECT_THAT(inline_sequence, ElementsAre(1, 2));
}

TEST_F(SegmentedSequenceTest, SegmentViewsExposeOnlyConstructedPrefixes) {
  IntSequence sequence;
  for (int value = 0; value < 7; ++value) {
    sequence.push_back(value);
  }

  const auto segments = sequence.segments();
  ASSERT_THAT(segments, SizeIs(4));
  EXPECT_THAT(segments[0], ElementsAre(0, 1));
  EXPECT_THAT(segments[1], ElementsAre(2, 3));
  EXPECT_THAT(segments[2], ElementsAre(4, 5));
  EXPECT_THAT(segments[3], ElementsAre(6));
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
  EXPECT_THAT(sequence.capacity(), Eq(6));

  sequence.trim_capacity(1);
  EXPECT_THAT(sequence.capacity(), Eq(6));
  sequence.trim_capacity(10);
  EXPECT_THAT(sequence.capacity(), Eq(6));
  sequence.reserve(11);
  sequence.trim_capacity(10);
  EXPECT_THAT(sequence.capacity(), Eq(10));
  sequence.trim_capacity(5);
  EXPECT_THAT(sequence.capacity(), Eq(6));
}

TEST_F(SegmentedSequenceTest, FixedSourceExhaustionStopsGrowth) {
  constexpr SegmentedSequenceOptions kSingleSegment{
      .segment_size = 1,
      .segment_capacity = 2,
  };
  alignas(128) std::array<std::byte, 64> storage{};
  SegmentedSequence<int, kSingleSegment, mbo::memory::FixedBlockSource> sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(storage), 128));

  ASSERT_THAT(sequence.try_push_back(1), Optional(_));
  EXPECT_THAT(sequence.try_push_back(2), Eq(std::nullopt));
  EXPECT_THAT(sequence, ElementsAre(1));
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

TEST_F(SegmentedSequenceTest, OverAlignedElementsRemainAlignedAcrossRetainedStorageReuse) {
  constexpr SegmentedSequenceOptions kOneElementSegment{
      .segment_size = 1,
      .segment_capacity = 2,
  };
  SegmentedSequence<OverAlignedElement, kOneElementSegment> sequence;
  OverAlignedElement* const first = std::addressof(sequence.emplace_back(1));
  EXPECT_THAT(std::bit_cast<std::uintptr_t>(first) % alignof(OverAlignedElement), Eq(0));

  sequence.clear();
  const OverAlignedElement* const reused = std::addressof(sequence.emplace_back(2));
  EXPECT_THAT(reused, Eq(first));
  EXPECT_THAT(reused->value, Eq(2));
}

TEST_F(SegmentedSequenceTest, ReacquiredSourcesRestartArrayLifetime) {
  constexpr SegmentedSequenceOptions kTwoElements{
      .segment_size = 2,
      .segment_capacity = 1,
  };
  alignas(128) std::array<std::byte, 4'096> fixed_storage{};
  {
    SegmentedSequence<int, kTwoElements, mbo::memory::FixedBlockSource> integers(
        mbo::memory::FixedBlockSource(std::span<std::byte>(fixed_storage), 128));
    integers.emplace_back(1);
    integers.emplace_back(2);
    EXPECT_THAT(integers, ElementsAre(1, 2));
  }
  {
    SegmentedSequence<double, kTwoElements, mbo::memory::FixedBlockSource> doubles(
        mbo::memory::FixedBlockSource(std::span<std::byte>(fixed_storage), 128));
    doubles.emplace_back(1.5);
    doubles.emplace_back(2.5);
    EXPECT_THAT(doubles, ElementsAre(1.5, 2.5));
  }

  using InlineSequence = SegmentedSequence<int, kTwoElements, mbo::memory::InlineBlockSource<4'096, 128>>;
  InlineSequence inline_sequence;
  int* const first_inline_address = std::addressof(inline_sequence.emplace_back(3));
  inline_sequence.release();
  EXPECT_THAT(std::addressof(inline_sequence.emplace_back(4)), Eq(first_inline_address));

  using AllocatorSequence = SegmentedSequence<int, kTwoElements, mbo::memory::AllocatorBlockSource<>>;
  AllocatorSequence allocator_sequence;
  allocator_sequence.emplace_back(5);
  allocator_sequence.release();
  allocator_sequence.emplace_back(6);
  EXPECT_THAT(allocator_sequence, ElementsAre(6));
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

TEST_F(SegmentedSequenceTest, PmrResourceCanOwnBothSegmentsAndDirectory) {
  std::array<std::byte, 4'096> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size());
  using Allocator = std::pmr::polymorphic_allocator<std::byte>;
  using Sequence = SegmentedSequence<int, kSmallSegments, mbo::memory::PmrBlockSource, Allocator>;
  Sequence sequence(std::allocator_arg, Allocator(&resource), mbo::memory::PmrBlockSource(&resource));

  sequence.push_back(1);
  sequence.push_back(2);
  sequence.push_back(3);

  EXPECT_THAT(sequence, ElementsAre(1, 2, 3));
  EXPECT_THAT(sequence.get_allocator().resource(), Eq(&resource));
}

TEST_F(SegmentedSequenceTest, TryAppendRejectsEveryMalformedSourceBlock) {
  constexpr SegmentedSequenceOptions kOneSegment{
      .segment_size = 2,
      .segment_capacity = 1,
  };
  constexpr std::array kResults{
      MalformedBlockSource::Result::kNullData,
      MalformedBlockSource::Result::kShortBlock,
      MalformedBlockSource::Result::kWeakAlignment,
      MalformedBlockSource::Result::kMisalignedData,
  };
  for (const auto result : kResults) {
    int releases = 0;
    SegmentedSequence<int, kOneSegment, MalformedBlockSource> sequence(
        MalformedBlockSource{
            .result = result,
            .releases = &releases,
        });

    EXPECT_THAT(sequence.try_push_back(1), Eq(std::nullopt));
    EXPECT_THAT(sequence, IsEmpty());
    EXPECT_THAT(releases, Eq(1));
  }
}

TEST_F(SegmentedSequenceTest, TryAppendStopsAfterTheConfiguredFullSegments) {
  constexpr SegmentedSequenceOptions kBounded{
      .segment_size = 2,
      .segment_capacity = 2,
  };
  SegmentedSequence<int, kBounded> sequence;
  sequence.push_back(1);
  sequence.push_back(2);
  sequence.push_back(3);
  sequence.push_back(4);

  EXPECT_THAT(sequence.capacity(), Eq(4));
  EXPECT_THAT(sequence.segment_count(), Eq(2));
  EXPECT_THAT(sequence.try_push_back(5), Eq(std::nullopt));
}

TEST_F(SegmentedSequenceTest, TryAppendReportsFixedSourceExhaustionWithoutMutation) {
  alignas(128) std::array<std::byte, 64> storage{};
  constexpr SegmentedSequenceOptions kFixedOptions{
      .segment_size = 2,
      .segment_capacity = 2,
  };
  using FixedSequence = SegmentedSequence<int, kFixedOptions, mbo::memory::FixedBlockSource>;
  FixedSequence sequence(mbo::memory::FixedBlockSource(std::span<std::byte>(storage), 128));

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
