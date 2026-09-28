// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/experimental_circular_buffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;
using ::testing::Pointee;
using ::testing::SizeIs;

struct ExperimentalCircularBufferTest : ::testing::Test {};

using IntBuffer = ExperimentalCircularBuffer<int>;

static_assert(std::random_access_iterator<IntBuffer::iterator>);
static_assert(std::random_access_iterator<IntBuffer::const_iterator>);
static_assert(std::random_access_iterator<IntBuffer::reverse_iterator>);
static_assert(std::random_access_iterator<IntBuffer::const_reverse_iterator>);
static_assert(std::ranges::random_access_range<IntBuffer>);
static_assert(std::ranges::random_access_range<const IntBuffer>);
static_assert(!std::ranges::contiguous_range<IntBuffer>);
static_assert(std::convertible_to<IntBuffer::iterator, IntBuffer::const_iterator>);
static_assert(!std::convertible_to<IntBuffer::const_iterator, IntBuffer::iterator>);
static_assert(std::same_as<IntBuffer::reference, int&>);
static_assert(std::same_as<IntBuffer::const_reference, const int&>);
static_assert(std::is_nothrow_move_constructible_v<IntBuffer>);
static_assert(std::is_nothrow_move_assignable_v<IntBuffer>);
static_assert(!std::is_copy_constructible_v<ExperimentalCircularBuffer<std::unique_ptr<int>>>);

constexpr bool ConstantEvaluation() {
  IntBuffer buffer;
  buffer.reserve(4);
  buffer.push_front(2);
  buffer.push_front(1);
  buffer.push_back(3);
  buffer.push_back(4);
  if (!buffer.full() || buffer.size() != 4 || buffer.capacity() != 4) {
    return false;
  }
  buffer.pop_front();
  buffer.push_back(5);
  buffer.pop_back();
  buffer.push_front(1);
  buffer.push_back(5);
  if (buffer.capacity() != 8 || buffer.at(2) != 3 || buffer.back() != 5) {
    return false;
  }
  IntBuffer copy(buffer);
  copy.insert(copy.cbegin() + 2, 9);
  copy.erase(copy.cbegin() + 2);
  if (copy != buffer || copy.end() - copy.begin() != 5 || *copy.crbegin() != 5) {
    return false;
  }
  copy.clear();
  copy.shrink_to_fit();
  return copy.empty() && copy.capacity() == 0;
}

static_assert(ConstantEvaluation());

TEST_F(ExperimentalCircularBufferTest, EmptyReservationDoesNotCreateElements) {
  IntBuffer buffer;
  EXPECT_THAT(buffer, IsEmpty());
  EXPECT_THAT(buffer.capacity(), Eq(0));
  EXPECT_THAT(buffer.begin(), Eq(buffer.end()));
  EXPECT_THAT(buffer.full(), IsTrue());
  buffer.reserve(3);
  EXPECT_THAT(buffer, IsEmpty());
  EXPECT_THAT(buffer.capacity(), Eq(4));
  EXPECT_THAT(buffer.full(), IsFalse());
  buffer.reserve(1);
  EXPECT_THAT(buffer.capacity(), Eq(4));
  EXPECT_THAT(buffer.max_size(), Ge(buffer.capacity()));
  EXPECT_THAT(buffer.get_allocator(), Eq(std::allocator<int>()));
  EXPECT_THAT(ConstantEvaluation(), IsTrue());
}

TEST_F(ExperimentalCircularBufferTest, CapacityRoundsThroughReserveGrowthShrinkAndReuse) {
  IntBuffer buffer;
  buffer.reserve(5);
  EXPECT_THAT(buffer.capacity(), Eq(8));
  for (int value = 0; value < 8; ++value) {
    buffer.push_back(value);
  }
  buffer.pop_front();
  buffer.push_back(8);
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));
  buffer.push_back(9);
  EXPECT_THAT(buffer.capacity(), Eq(16));
  buffer.resize(3);
  buffer.shrink_to_fit();
  EXPECT_THAT(buffer.capacity(), Eq(4));
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3));
  buffer.clear();
  buffer.shrink_to_fit();
  EXPECT_THAT(buffer.capacity(), Eq(0));
  buffer.push_front(9);
  EXPECT_THAT(buffer.capacity(), Eq(1));
  EXPECT_THAT(buffer, ElementsAre(9));
}

TEST_F(ExperimentalCircularBufferTest, BothEndsWrapAndReuseAllReservedSlots) {
  IntBuffer buffer;
  buffer.reserve(4);
  buffer.push_front(2);
  buffer.push_front(1);
  buffer.push_back(3);
  buffer.push_back(4);
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(buffer.full(), IsTrue());
  int* const second = std::addressof(buffer.at(1));
  buffer.pop_front();
  buffer.push_back(5);
  EXPECT_THAT(buffer, ElementsAre(2, 3, 4, 5));
  EXPECT_THAT(std::addressof(buffer.front()), Eq(second));
  buffer.pop_back();
  buffer.push_front(1);
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(buffer.capacity(), Eq(4));
  buffer.push_front(0);
  EXPECT_THAT(buffer, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(buffer.capacity(), Eq(8));
}

TEST_F(ExperimentalCircularBufferTest, OneSlotDistinguishesFullAndEmptyAcrossEitherEnd) {
  IntBuffer buffer;
  buffer.reserve(1);
  for (int value = 0; value < 32; ++value) {
    buffer.push_front(value);
    EXPECT_THAT(buffer, ElementsAre(value));
    EXPECT_THAT(buffer.full(), IsTrue());
    buffer.pop_back();
    EXPECT_THAT(buffer, IsEmpty());
    buffer.push_back(value);
    buffer.pop_front();
    EXPECT_THAT(buffer, IsEmpty());
    EXPECT_THAT(buffer.capacity(), Eq(1));
  }
}

TEST_F(ExperimentalCircularBufferTest, IteratorsTraverseLogicalOrderAcrossTheWrap) {
  IntBuffer buffer;
  buffer.reserve(8);
  buffer.push_front(3);
  buffer.push_front(5);
  buffer.push_back(1);
  buffer.push_back(4);
  buffer.push_back(2);
  const IntBuffer& constant = buffer;
  EXPECT_THAT(constant.front(), Eq(5));
  EXPECT_THAT(constant.back(), Eq(2));
  EXPECT_THAT(constant.at(2), Eq(1));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): test valid unchecked access.
  EXPECT_THAT(constant[3], Eq(4));
  EXPECT_THAT(std::ranges::subrange(buffer.rbegin(), buffer.rend()), ElementsAre(2, 4, 1, 3, 5));
  EXPECT_THAT(std::ranges::subrange(constant.rbegin(), constant.rend()), ElementsAre(2, 4, 1, 3, 5));
  EXPECT_THAT(std::ranges::subrange(buffer.crbegin(), buffer.crend()), ElementsAre(2, 4, 1, 3, 5));
  auto iter = buffer.begin();
  EXPECT_THAT(*iter++, Eq(5));
  EXPECT_THAT(*++iter, Eq(1));
  EXPECT_THAT(*iter--, Eq(1));
  EXPECT_THAT(*--iter, Eq(5));
  iter += 4;
  EXPECT_THAT(*iter, Eq(2));
  iter -= 2;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): test iterator indexing.
  EXPECT_THAT(iter[1], Eq(4));
  EXPECT_THAT(*(2 + buffer.begin()), Eq(1));
  EXPECT_THAT(*(buffer.end() - 1), Eq(2));
  EXPECT_THAT(iter - buffer.cbegin(), Eq(2));
  EXPECT_THAT(buffer.cend() - iter, Eq(3));
  EXPECT_THAT(buffer.begin() == constant.cbegin(), IsTrue());
  EXPECT_THAT(iter < buffer.cend(), IsTrue());
  EXPECT_THAT(IntBuffer::iterator(), Eq(IntBuffer::iterator()));
  *iter = 6;
  std::ranges::sort(buffer);
  EXPECT_THAT(buffer, ElementsAre(2, 3, 4, 5, 6));
}

TEST_F(ExperimentalCircularBufferTest, IteratorArrowExposesMutableAndConstElements) {
  struct Value {
    int number;
  };

  ExperimentalCircularBuffer<Value> buffer;
  buffer.emplace_front(1);
  buffer.begin()->number = 2;
  EXPECT_THAT(std::as_const(buffer).begin()->number, Eq(2));
  EXPECT_THAT(buffer.rbegin()->number, Eq(2));
}

TEST_F(ExperimentalCircularBufferTest, ConstructorsCopyMoveAndAssignWrappedContents) {
  const IntBuffer zeros(3);
  EXPECT_THAT(zeros, ElementsAre(0, 0, 0));
  const IntBuffer repeated(2, 7);
  EXPECT_THAT(repeated, ElementsAre(7, 7));
  IntBuffer original{2, 3, 4};
  original.reserve(8);
  original.pop_back();
  original.push_front(1);
  IntBuffer copy(original);
  EXPECT_THAT(copy, ElementsAre(1, 2, 3));
  EXPECT_THAT(copy.capacity(), Eq(8));
  copy.front() = 8;
  EXPECT_THAT(original.front(), Eq(1));
  IntBuffer assigned;
  assigned = original;
  EXPECT_THAT(assigned, Eq(original));
  IntBuffer& self = assigned;
  assigned = self;
  EXPECT_THAT(assigned, Eq(original));
  IntBuffer moved(std::move(copy));
  EXPECT_THAT(moved, ElementsAre(8, 2, 3));
  // NOLINTNEXTLINE(bugprone-use-after-move): verify the moved-from empty state and reuse below.
  EXPECT_THAT(copy, IsEmpty());
  EXPECT_THAT(copy.capacity(), Eq(0));
  copy.push_front(9);
  EXPECT_THAT(copy, ElementsAre(9));
  assigned = std::move(moved);
  EXPECT_THAT(assigned, ElementsAre(8, 2, 3));
  // NOLINTNEXTLINE(bugprone-use-after-move): verify the moved-from empty state.
  EXPECT_THAT(moved, IsEmpty());
  assigned = std::move(self);
  EXPECT_THAT(assigned, ElementsAre(8, 2, 3));
  assigned = {4, 5};
  EXPECT_THAT(assigned, ElementsAre(4, 5));
  original.clear();
  const IntBuffer reserved_copy(original);
  EXPECT_THAT(reserved_copy, IsEmpty());
  EXPECT_THAT(reserved_copy.capacity(), Eq(8));
}

TEST_F(ExperimentalCircularBufferTest, GrowingInsertionAcceptsAliasedValues) {
  ExperimentalCircularBuffer<std::string> buffer;
  buffer.push_back(std::string(128, 'a'));
  buffer.push_back(buffer.front());
  buffer.push_front(buffer.back());
  EXPECT_THAT(buffer, ElementsAre(std::string(128, 'a'), std::string(128, 'a'), std::string(128, 'a')));
  buffer.resize(8, buffer.back());
  EXPECT_THAT(buffer, SizeIs(8));
  for (const auto& value : buffer) {
    EXPECT_THAT(value, Eq(std::string(128, 'a')));
  }
}

TEST_F(ExperimentalCircularBufferTest, InsertEraseAndResizeUseLogicalPositions) {
  IntBuffer buffer{2, 4};
  buffer.reserve(8);
  EXPECT_THAT(*buffer.emplace(buffer.begin(), 1), Eq(1));
  EXPECT_THAT(*buffer.insert(buffer.begin() + 2, 3), Eq(3));
  const int fifth = 5;
  EXPECT_THAT(*buffer.insert(buffer.end(), fifth), Eq(5));
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4, 5));
  EXPECT_THAT(*buffer.insert(buffer.begin() + 2, 2, 8), Eq(8));
  EXPECT_THAT(buffer, ElementsAre(1, 2, 8, 8, 3, 4, 5));
  EXPECT_THAT(*buffer.erase(buffer.begin() + 2, buffer.begin() + 4), Eq(3));
  EXPECT_THAT(*buffer.erase(buffer.begin()), Eq(2));
  const auto after_erase = buffer.erase(buffer.end() - 1);
  EXPECT_THAT(after_erase, Eq(buffer.end()));
  EXPECT_THAT(buffer, ElementsAre(2, 3, 4));
  EXPECT_THAT(buffer.erase(buffer.begin(), buffer.begin()), Eq(buffer.begin()));
  EXPECT_THAT(buffer.insert(buffer.end(), 0, 9), Eq(buffer.end()));
  buffer.resize(5);
  EXPECT_THAT(buffer, ElementsAre(2, 3, 4, 0, 0));
  buffer.resize(2);
  buffer.resize(4, 7);
  buffer.resize(3, 0);
  EXPECT_THAT(buffer, ElementsAre(2, 3, 7));
  buffer.assign(2, 9);
  EXPECT_THAT(buffer, ElementsAre(9, 9));
  buffer.assign({1, 2});
  EXPECT_THAT(buffer, ElementsAre(1, 2));
}

TEST_F(ExperimentalCircularBufferTest, RangeOperationsPreserveInputOrderAndAllowSelfInsertion) {
  constexpr auto kValues = std::to_array({1, 2, 3});
  IntBuffer buffer(kValues.begin(), kValues.end());
  buffer.insert(buffer.begin() + 1, {7, 8});
  EXPECT_THAT(buffer, ElementsAre(1, 7, 8, 2, 3));
  buffer.assign_range(kValues);
  buffer.prepend_range(buffer);
  EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 1, 2, 3));
  buffer.assign(buffer.begin() + 1, buffer.end() - 1);
  EXPECT_THAT(buffer, ElementsAre(2, 3, 1, 2));
  std::istringstream input("4 5 6");
  buffer.append_range(std::ranges::istream_view<int>(input));
  EXPECT_THAT(buffer, ElementsAre(2, 3, 1, 2, 4, 5, 6));
  std::istringstream second_input("8 9");
  const IntBuffer single_pass{std::istream_iterator<int>(second_input), std::istream_iterator<int>()};
  EXPECT_THAT(single_pass, ElementsAre(8, 9));
}

TEST_F(ExperimentalCircularBufferTest, ClearAndShrinkControlRetainedStorage) {
  IntBuffer buffer{1, 2, 3};
  buffer.reserve(16);
  buffer.pop_front();
  buffer.shrink_to_fit();
  EXPECT_THAT(buffer, ElementsAre(2, 3));
  EXPECT_THAT(buffer.capacity(), Eq(2));
  buffer.shrink_to_fit();
  EXPECT_THAT(buffer.capacity(), Eq(2));
  buffer.clear();
  EXPECT_THAT(buffer, IsEmpty());
  EXPECT_THAT(buffer.capacity(), Eq(2));
  buffer.shrink_to_fit();
  EXPECT_THAT(buffer.capacity(), Eq(0));
  buffer.push_front(9);
  EXPECT_THAT(buffer, ElementsAre(9));
}

struct Counted final {
  explicit Counted(int& live, int value) : live(&live), value(value) { ++live; }

  Counted(const Counted& other) : live(other.live), value(other.value) { ++*live; }

  Counted(Counted&& other) noexcept : live(other.live), value(other.value) { ++*live; }

  Counted& operator=(const Counted&) = default;
  Counted& operator=(Counted&&) noexcept = default;

  ~Counted() { --*live; }

  int* live;
  int value;
};

TEST_F(ExperimentalCircularBufferTest, ConstructsOnlyLiveObjectsAndDestroysPoppedValues) {
  int live = 0;
  {
    ExperimentalCircularBuffer<Counted> buffer;
    buffer.reserve(8);
    EXPECT_THAT(live, Eq(0));
    buffer.emplace_front(live, 1);
    buffer.emplace_back(live, 2);
    EXPECT_THAT(live, Eq(2));
    buffer.reserve(32);
    EXPECT_THAT(live, Eq(2));
    buffer.pop_front();
    EXPECT_THAT(live, Eq(1));
    buffer.pop_back();
    EXPECT_THAT(live, Eq(0));
    buffer.emplace_front(live, 3);
    buffer.clear();
    EXPECT_THAT(live, Eq(0));
    buffer.emplace_back(live, 4);
  }
  EXPECT_THAT(live, Eq(0));
}

TEST_F(ExperimentalCircularBufferTest, PointerValuesDoNotConferPointeeOwnership) {
  int value = 7;
  ExperimentalCircularBuffer<int*> buffer;
  buffer.push_front(&value);
  buffer.clear();
  EXPECT_THAT(value, Eq(7));
}

TEST_F(ExperimentalCircularBufferTest, SupportsMoveOnlyAndReservedImmovableElements) {
  ExperimentalCircularBuffer<std::unique_ptr<int>> buffer;
  buffer.push_back(std::make_unique<int>(2));
  buffer.push_front(std::make_unique<int>(1));
  buffer.emplace(buffer.begin() + 1, std::make_unique<int>(3));
  EXPECT_THAT(buffer, ElementsAre(Pointee(Eq(1)), Pointee(Eq(3)), Pointee(Eq(2))));
  buffer.erase(buffer.begin() + 1);
  EXPECT_THAT(buffer, ElementsAre(Pointee(Eq(1)), Pointee(Eq(2))));

  struct Immovable final {
    explicit Immovable(int value) : value(value) {}

    Immovable(const Immovable&) = delete;
    Immovable& operator=(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
    Immovable& operator=(Immovable&&) = delete;
    ~Immovable() = default;
    int value;
  };

  ExperimentalCircularBuffer<Immovable> fixed;
  fixed.reserve(2);
  EXPECT_THAT(fixed.emplace_front(5).value, Eq(5));
  EXPECT_THAT(fixed.emplace_back(6).value, Eq(6));
  fixed.pop_front();
  fixed.pop_back();
  EXPECT_THAT(fixed, IsEmpty());
}

TEST_F(ExperimentalCircularBufferTest, SupportsOveralignedElements) {
  struct alignas(128) Aligned final {
    int value;
  };

  ExperimentalCircularBuffer<Aligned> buffer;
  buffer.emplace_front(1);
  buffer.emplace_back(2);
  for (const Aligned& value : buffer) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): verify allocator alignment.
    EXPECT_THAT(reinterpret_cast<std::uintptr_t>(std::addressof(value)) % alignof(Aligned), Eq(0));
  }
}

// NOLINTBEGIN(readability-identifier-naming): allocator test double.
struct AllocationState final {
  std::map<void*, std::pair<int, std::size_t>> owned;
  std::size_t allocations = 0;
  std::size_t deallocations = 0;
};

template<typename T, bool Propagate = false>
struct TrackingAllocator final {
  using value_type = T;
  using propagate_on_container_copy_assignment = std::bool_constant<Propagate>;
  using propagate_on_container_move_assignment = std::bool_constant<Propagate>;
  using propagate_on_container_swap = std::bool_constant<Propagate>;
  using is_always_equal = std::false_type;

  template<typename U>
  struct rebind {
    using other = TrackingAllocator<U, Propagate>;
  };

  AllocationState* state = nullptr;
  int id = 0;

  T* allocate(std::size_t count) {
    T* result = std::allocator<T>().allocate(count);
    state->owned.emplace(result, std::pair(id, count));
    ++state->allocations;
    return result;
  }

  void deallocate(T* data, std::size_t count) noexcept {
    EXPECT_THAT(state->owned.at(data), Eq(std::pair(id, count)));
    state->owned.erase(data);
    ++state->deallocations;
    std::allocator<T>().deallocate(data, count);
  }

  TrackingAllocator select_on_container_copy_construction() const { return {.state = state, .id = id + 1}; }

  friend bool operator==(const TrackingAllocator&, const TrackingAllocator&) noexcept = default;
};

// NOLINTEND(readability-identifier-naming)

TEST_F(ExperimentalCircularBufferTest, UnequalNonpropagatingAllocatorsKeepTheirStorageOwnership) {
  AllocationState state;
  using Allocator = TrackingAllocator<int>;
  using Buffer = ExperimentalCircularBuffer<int, Allocator>;
  const Allocator first{.state = &state, .id = 1};
  const Allocator second{.state = &state, .id = 2};
  {
    Buffer source({1, 2, 3}, first);
    source.reserve(16);
    source.pop_back();
    source.push_front(0);
    Buffer selected(source);
    EXPECT_THAT(selected.get_allocator().id, Eq(2));
    const Buffer explicit_copy(source, second);
    EXPECT_THAT(explicit_copy.capacity(), Eq(16));
    Buffer target({9}, second);
    target = source;
    EXPECT_THAT(target.get_allocator().id, Eq(2));
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
    target = std::move(source);
    EXPECT_THAT(target.get_allocator().id, Eq(2));
    EXPECT_THAT(target.capacity(), Eq(16));
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
    // NOLINTNEXTLINE(bugprone-use-after-move): unequal-allocator relocation empties the source.
    EXPECT_THAT(source, IsEmpty());
    Buffer equal(std::move(target), second);
    EXPECT_THAT(equal, ElementsAre(0, 1, 2));
    // NOLINTNEXTLINE(bugprone-use-after-move): equal-allocator transfer releases the source allocation.
    EXPECT_THAT(target.capacity(), Eq(0));
    Buffer transfer(second);
    transfer = std::move(equal);
    EXPECT_THAT(transfer, ElementsAre(0, 1, 2));
    swap(transfer, selected);
    EXPECT_THAT(transfer, ElementsAre(0, 1, 2));
  }
  EXPECT_THAT(state.owned, IsEmpty());
  EXPECT_THAT(state.allocations, Eq(state.deallocations));
}

TEST_F(ExperimentalCircularBufferTest, PropagatingAllocatorsFollowCopyMoveAndSwap) {
  AllocationState state;
  using Allocator = TrackingAllocator<int, true>;
  using Buffer = ExperimentalCircularBuffer<int, Allocator>;
  {
    const Buffer source({1, 2}, Allocator{.state = &state, .id = 1});
    Buffer target({9}, Allocator{.state = &state, .id = 2});
    target = source;
    EXPECT_THAT(target.get_allocator().id, Eq(1));
    Buffer moved({3}, Allocator{.state = &state, .id = 3});
    moved = std::move(target);
    EXPECT_THAT(moved.get_allocator().id, Eq(1));
    EXPECT_THAT(moved, ElementsAre(1, 2));
    Buffer other({4}, Allocator{.state = &state, .id = 4});
    swap(moved, other);
    EXPECT_THAT(moved.get_allocator().id, Eq(4));
    EXPECT_THAT(moved, ElementsAre(4));
    EXPECT_THAT(other.get_allocator().id, Eq(1));
    EXPECT_THAT(other, ElementsAre(1, 2));
  }
  EXPECT_THAT(state.owned, IsEmpty());
}

TEST_F(ExperimentalCircularBufferTest, ReservedQueueDoesNotAllocateOrDeallocate) {
  AllocationState state;
  using Allocator = TrackingAllocator<int>;
  {
    ExperimentalCircularBuffer<int, Allocator> buffer(Allocator{.state = &state, .id = 1});
    buffer.reserve(4);
    for (int value = 0; value < 4; ++value) {
      buffer.push_back(value);
    }
    for (int value = 4; value < 1'000; ++value) {
      buffer.pop_front();
      buffer.push_back(value);
    }
    for (int value = 0; value < 1'000; ++value) {
      buffer.pop_back();
      buffer.push_front(value);
    }
    EXPECT_THAT(state.allocations, Eq(1));
    EXPECT_THAT(state.deallocations, Eq(0));
  }
  EXPECT_THAT(state.owned, IsEmpty());
}

TEST_F(ExperimentalCircularBufferTest, PmrBufferUsesCallerOwnedArena) {
  std::array<std::byte, 2'048> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
  ExperimentalCircularBuffer<int, std::pmr::polymorphic_allocator<int>> buffer{&resource};
  buffer.reserve(64);
  for (int value = 0; value < 64; ++value) {
    buffer.push_front(value);
  }
  for (int value = 0; value < 4'096; ++value) {
    buffer.pop_back();
    buffer.push_front(value);
  }
  EXPECT_THAT(buffer.front(), Eq(4'095));
  EXPECT_THAT(buffer.back(), Eq(4'032));
  EXPECT_THAT(buffer.get_allocator().resource(), Eq(&resource));
}

TEST_F(ExperimentalCircularBufferTest, ComparisonsUseValuesRatherThanPhysicalLayout) {
  IntBuffer lhs{1, 2, 3};
  IntBuffer rhs;
  rhs.reserve(8);
  rhs.push_front(3);
  rhs.push_front(2);
  rhs.push_front(1);
  EXPECT_THAT(lhs, Eq(rhs));
  EXPECT_THAT(lhs <=> rhs, Eq(std::strong_ordering::equal));
  rhs.back() = 4;
  EXPECT_THAT(lhs, Lt(rhs));
  swap(lhs, rhs);
  EXPECT_THAT(lhs, ElementsAre(1, 2, 4));
  EXPECT_THAT(rhs, ElementsAre(1, 2, 3));
}

TEST_F(ExperimentalCircularBufferTest, MixedOperationsMatchStandardDeque) {
  IntBuffer buffer;
  std::deque<int> expected;
  std::uint32_t state = 17;
  for (int step = 0; step < 10'000; ++step) {
    state = (state * 1'664'525U) + 1'013'904'223U;
    const auto operation = (state >> 24) % 8;
    if (operation == 0 || expected.empty()) {
      buffer.push_front(step);
      expected.push_front(step);
    } else if (operation == 1) {
      buffer.push_back(step);
      expected.push_back(step);
    } else if (operation == 2) {
      buffer.pop_front();
      expected.pop_front();
    } else if (operation == 3) {
      buffer.pop_back();
      expected.pop_back();
    } else if (operation == 4) {
      const auto pos = static_cast<std::ptrdiff_t>(state % expected.size());
      buffer.insert(buffer.begin() + pos, step);
      expected.insert(expected.begin() + pos, step);
    } else if (operation == 5) {
      const auto pos = static_cast<std::ptrdiff_t>(state % expected.size());
      buffer.erase(buffer.begin() + pos);
      expected.erase(expected.begin() + pos);
    } else if (operation == 6) {
      buffer.reserve(expected.size() + 3);
    } else {
      buffer.shrink_to_fit();
    }
    ASSERT_THAT(buffer, ElementsAreArray(expected)) << "step " << step;
  }
}

}  // namespace
}  // namespace mbo::container::container_internal
