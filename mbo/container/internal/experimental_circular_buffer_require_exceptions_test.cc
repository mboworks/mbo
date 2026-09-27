// SPDX-FileCopyrightText: Copyright (c) M. Boerger, The MBO Works Authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/internal/experimental_circular_buffer.h"
#include "mbo/container/segmented_deque.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Throws;
using ::testing::ThrowsMessage;

struct Value final {
  static inline int live = 0;
  static inline int remaining = -1;

  explicit Value(int value = 0) : value(value) {
    if (remaining == 0) {
      throw std::runtime_error("construction failed");
    }
    if (remaining > 0) {
      --remaining;
    }
    ++live;
  }

  Value(const Value& other) : Value(other.value) {}

  Value(Value&&) = delete;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) = delete;

  ~Value() noexcept { --live; }

  int value;
};

struct ExperimentalCircularBufferRequireExceptionsTest : ::testing::Test {
 protected:
  void SetUp() override {
    Value::remaining = -1;
    EXPECT_THAT(Value::live, Eq(0));
  }

  void TearDown() override {
    Value::remaining = -1;
    EXPECT_THAT(Value::live, Eq(0));
  }
};

using Buffer = ExperimentalCircularBuffer<Value>;
using IntBuffer = ExperimentalCircularBuffer<int>;

static_assert(noexcept(std::declval<IntBuffer&>().at(0)) == !config::kRequireThrows);
static_assert(noexcept(std::declval<const IntBuffer&>().at(0)) == !config::kRequireThrows);
static_assert(noexcept(std::declval<IntBuffer&>().pop_front()) == !config::kRequireThrows);
static_assert(noexcept(std::declval<IntBuffer&>().pop_back()) == !config::kRequireThrows);

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, EndInsertionRollsBackEachConstructionFailure) {
  for (int front = 0; front != 2; ++front) {
    for (int failure = 0; failure != 5; ++failure) {
      Buffer buffer;
      buffer.reserve(4);
      for (int value = 1; value <= 4; ++value) {
        buffer.emplace_back(value);
      }
      buffer.pop_front();
      buffer.emplace_back(5);
      Value::remaining = failure;
      EXPECT_THAT(
          ([&buffer, front] {
            if (front != 0) {
              buffer.emplace_front(9);
            } else {
              buffer.emplace_back(9);
            }
          }),
          ThrowsMessage<std::runtime_error>(HasSubstr("construction failed")));
      Value::remaining = -1;
      EXPECT_THAT(buffer | std::views::transform(&Value::value), ElementsAre(2, 3, 4, 5));
      EXPECT_THAT(buffer.capacity(), Eq(4));
      EXPECT_THAT(Value::live, Eq(4));
    }
  }
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, ReservedEndInsertionPreservesLiveRangeOnFailure) {
  Buffer buffer;
  buffer.reserve(8);
  buffer.emplace_back(1);
  Value::remaining = 0;
  EXPECT_THAT([&buffer] { buffer.emplace_back(2); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.emplace_front(0); }, Throws<std::runtime_error>());
  EXPECT_THAT(buffer | std::views::transform(&Value::value), ElementsAre(1));
  EXPECT_THAT(Value::live, Eq(1));
}

struct ThrowingMove final {
  static inline int live = 0;
  static inline int remaining = -1;

  explicit ThrowingMove(int value) : value(value) { ++live; }

  ThrowingMove(const ThrowingMove&) = delete;
  ThrowingMove& operator=(const ThrowingMove&) = delete;

  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  ThrowingMove(ThrowingMove&& other) : value(std::exchange(other.value, -1)) {
    if (remaining-- == 0) {
      throw std::runtime_error("move failed");
    }
    ++live;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  ThrowingMove& operator=(ThrowingMove&& other) {
    value = std::exchange(other.value, -1);
    if (remaining-- == 0) {
      throw std::runtime_error("assignment failed");
    }
    return *this;
  }

  ~ThrowingMove() noexcept { --live; }

  int value;
};

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, ThrowingMovePreservesLifetimesAndAllowsReuse) {
  {
    ExperimentalCircularBuffer<ThrowingMove> buffer;
    buffer.reserve(4);
    for (int value = 1; value <= 4; ++value) {
      buffer.emplace_back(value);
    }
    ThrowingMove::remaining = 1;
    EXPECT_THAT([&buffer] { buffer.reserve(8); }, Throws<std::runtime_error>());
    EXPECT_THAT(buffer | std::views::transform(&ThrowingMove::value), ElementsAre(-1, -1, 3, 4));
    EXPECT_THAT(ThrowingMove::live, Eq(4));
    ThrowingMove::remaining = 0;
    EXPECT_THAT([&buffer] { buffer.erase(buffer.cbegin() + 1); }, Throws<std::runtime_error>());
    EXPECT_THAT(buffer, SizeIs(4));
    EXPECT_THAT(ThrowingMove::live, Eq(4));
    buffer.clear();
    buffer.emplace_front(9);
    EXPECT_THAT(buffer.front().value, Eq(9));
  }
  EXPECT_THAT(ThrowingMove::live, Eq(0));
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, RelocationAndCopyFailuresDestroyPartialReplacements) {
  Buffer buffer;
  for (int value = 1; value <= 4; ++value) {
    buffer.emplace_back(value);
  }
  for (int failure = 0; failure < 4; ++failure) {
    Value::remaining = failure;
    EXPECT_THAT([&buffer] { buffer.reserve(16); }, Throws<std::runtime_error>());
    Value::remaining = failure;
    EXPECT_THAT([&buffer] { return Buffer(buffer); }, Throws<std::runtime_error>());
    Value::remaining = -1;
    Buffer destination;
    destination.emplace_back(9);
    Value::remaining = failure;
    EXPECT_THAT(([&buffer, &destination] { destination = buffer; }), Throws<std::runtime_error>());
    Value::remaining = -1;
    EXPECT_THAT(destination | std::views::transform(&Value::value), ElementsAre(9));
    EXPECT_THAT(buffer | std::views::transform(&Value::value), ElementsAre(1, 2, 3, 4));
    EXPECT_THAT(buffer.capacity(), Eq(4));
    EXPECT_THAT(Value::live, Eq(5));
  }
  buffer.reserve(16);
  Value::remaining = 2;
  EXPECT_THAT([&buffer] { buffer.shrink_to_fit(); }, Throws<std::runtime_error>());
  EXPECT_THAT(buffer.capacity(), Eq(16));
  EXPECT_THAT(Value::live, Eq(4));
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, ResizeAndStagedInsertRollBackPartialConstruction) {
  Buffer buffer;
  buffer.reserve(8);
  buffer.emplace_back(1);
  buffer.emplace_back(2);
  for (int failure = 0; failure < 4; ++failure) {
    Value::remaining = failure;
    EXPECT_THAT([&buffer] { buffer.resize(6); }, Throws<std::runtime_error>());
    Value::remaining = failure;
    EXPECT_THAT([&buffer] { buffer.emplace(buffer.cbegin() + 1, 9); }, Throws<std::runtime_error>());
    Value::remaining = -1;
    EXPECT_THAT(buffer | std::views::transform(&Value::value), ElementsAre(1, 2));
    EXPECT_THAT(Value::live, Eq(2));
  }
  Value::remaining = 1;
  EXPECT_THAT([&buffer] { buffer.assign(4, buffer.front()); }, Throws<std::runtime_error>());
  EXPECT_THAT(buffer | std::views::transform(&Value::value), ElementsAre(1, 2));
  EXPECT_THAT(Value::live, Eq(2));
}

struct AllocationState final {
  bool fail_allocate = false;
  int constructions_before_failure = -1;
  int allocations = 0;
};

// NOLINTBEGIN(readability-identifier-naming): allocator interface.
template<typename T>
struct FailingAllocator final {
  using value_type = T;
  using is_always_equal = std::false_type;

  explicit FailingAllocator(AllocationState* state) : state(state) {}

  template<typename U>
  explicit FailingAllocator(const FailingAllocator<U>& other) : state(other.state) {}

  T* allocate(std::size_t count) {
    if (state->fail_allocate) {
      throw std::bad_alloc();
    }
    T* result = std::allocator<T>().allocate(count);
    ++state->allocations;
    return result;
  }

  void deallocate(T* ptr, std::size_t count) noexcept {
    --state->allocations;
    std::allocator<T>().deallocate(ptr, count);
  }

  template<typename U, typename... Args>
  void construct(U* ptr, Args&&... args) {
    if (state->constructions_before_failure == 0) {
      throw std::runtime_error("allocator construction failed");
    }
    if (state->constructions_before_failure > 0) {
      --state->constructions_before_failure;
    }
    std::construct_at(ptr, std::forward<Args>(args)...);
  }

  constexpr std::size_t max_size() const noexcept { return 8; }

  friend bool operator==(const FailingAllocator&, const FailingAllocator&) = default;

  AllocationState* state;
};

// NOLINTEND(readability-identifier-naming)

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, AllocationFailurePreservesContentsAndOwnership) {
  AllocationState state;
  {
    ExperimentalCircularBuffer<int, FailingAllocator<int>> buffer{FailingAllocator<int>(&state)};
    buffer.push_back(1);
    state.fail_allocate = true;
    EXPECT_THAT([&buffer] { buffer.push_front(0); }, Throws<std::bad_alloc>());
    EXPECT_THAT([&buffer] { buffer.push_back(2); }, Throws<std::bad_alloc>());
    EXPECT_THAT([&buffer] { buffer.reserve(8); }, Throws<std::bad_alloc>());
    EXPECT_THAT(buffer, ElementsAre(1));
    EXPECT_THAT(buffer.capacity(), Eq(1));
    EXPECT_THAT(state.allocations, Eq(1));
  }
  EXPECT_THAT(state.allocations, Eq(0));
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, DequeReturnsSegmentWhenPointerConstructionThrows) {
  for (int front = 0; front != 2; ++front) {
    AllocationState state;
    {
      using Deque = SegmentedDeque<
          Value, SegmentedOptions{.segment_size = 2}, memory::NewDeleteBlockSource, FailingAllocator<std::byte>>;
      Deque deque(std::allocator_arg, FailingAllocator<std::byte>(&state));
      deque.reserve(8);
      state.constructions_before_failure = 0;
      EXPECT_THAT(
          ([&deque, front] {
            if (front != 0) {
              deque.emplace_front(1);
            } else {
              deque.emplace_back(1);
            }
          }),
          Throws<std::runtime_error>());
      EXPECT_THAT(deque, IsEmpty());
      EXPECT_THAT(Value::live, Eq(0));
      state.constructions_before_failure = -1;
      deque.emplace_back(9);
      const std::array input{Value(1), Value(2), Value(3)};
      const auto saved = deque.begin();
      state.constructions_before_failure = 1;
      EXPECT_THAT(([&deque, &input] { deque.prepend_range(input); }), Throws<std::runtime_error>());
      state.constructions_before_failure = -1;
      EXPECT_THAT(deque | std::views::transform(&Value::value), ElementsAre(9));
      EXPECT_THAT(saved, Eq(deque.begin()));
      EXPECT_THAT(Value::live, Eq(4));
      deque.prepend_range(input);
      EXPECT_THAT(deque | std::views::transform(&Value::value), ElementsAre(1, 2, 3, 9));
    }
    EXPECT_THAT(state.allocations, Eq(0));
  }
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, CheckedAccessAndIteratorRequirementsPropagate) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "Requires --//mbo/config:require_throws=true";
  }
  IntBuffer buffer;
  const IntBuffer& constant = buffer;
  EXPECT_THAT([&buffer] { static_cast<void>(buffer.at(0)); }, Throws<std::runtime_error>());
  EXPECT_THAT([&constant] { static_cast<void>(constant.at(0)); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { static_cast<void>(buffer.front()); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { static_cast<void>(buffer.back()); }, Throws<std::runtime_error>());
  EXPECT_THAT([&constant] { static_cast<void>(constant.front()); }, Throws<std::runtime_error>());
  EXPECT_THAT([&constant] { static_cast<void>(constant.back()); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.pop_front(); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.pop_back(); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.reserve(buffer.max_size() + 1); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.erase(buffer.cend()); }, Throws<std::runtime_error>());
  buffer.push_back(1);
  EXPECT_THAT([&buffer] { buffer.erase(buffer.cend(), buffer.cbegin()); }, Throws<std::runtime_error>());
  IntBuffer other;
  EXPECT_THAT(([&buffer, &other] { buffer.emplace(other.cbegin(), 2); }), Throws<std::runtime_error>());
  EXPECT_THAT(([&buffer, &other] { static_cast<void>(buffer.begin() - other.begin()); }), Throws<std::runtime_error>());
  EXPECT_THAT(
      ([&buffer, &other] { static_cast<void>(buffer.begin() == other.begin()); }), Throws<std::runtime_error>());
  EXPECT_THAT(([&buffer, &other] { static_cast<void>(buffer.begin() < other.begin()); }), Throws<std::runtime_error>());
  const std::array input{1, 2};
  EXPECT_THAT([&input] { const IntBuffer invalid(input.end(), input.begin()); }, Throws<std::runtime_error>());
}

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, CapacityLimitsAndUnequalAllocatorSwapAreChecked) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "Requires --//mbo/config:require_throws=true";
  }
  AllocationState first;
  AllocationState second;
  using AllocatedBuffer = ExperimentalCircularBuffer<int, FailingAllocator<int>>;
  AllocatedBuffer lhs{FailingAllocator<int>(&first)};
  AllocatedBuffer rhs{FailingAllocator<int>(&second)};
  lhs.resize(8);
  EXPECT_THAT(lhs.max_size(), Eq(8));
  EXPECT_THAT([&lhs] { lhs.push_front(1); }, Throws<std::runtime_error>());
  EXPECT_THAT([&lhs] { lhs.push_back(1); }, Throws<std::runtime_error>());
  EXPECT_THAT([&lhs] { lhs.insert(lhs.cbegin(), 1, 1); }, Throws<std::runtime_error>());
  const std::array input{1};
  EXPECT_THAT(([&lhs, &input] { lhs.append_range(input); }), Throws<std::runtime_error>());
  EXPECT_THAT(([&lhs, &rhs] { lhs.swap(rhs); }), Throws<std::runtime_error>());
  EXPECT_THAT(lhs, SizeIs(8));
  EXPECT_THAT(rhs, IsEmpty());
}

struct Immovable final {
  Immovable() = default;
  Immovable(const Immovable&) = delete;
  Immovable(Immovable&&) = delete;
  Immovable& operator=(const Immovable&) = delete;
  Immovable& operator=(Immovable&&) = delete;
  ~Immovable() = default;
};

TEST_F(ExperimentalCircularBufferRequireExceptionsTest, ImmovableElementsRejectRelocation) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "Requires --//mbo/config:require_throws=true";
  }
  ExperimentalCircularBuffer<Immovable> buffer;
  buffer.emplace_back();
  EXPECT_THAT([&buffer] { buffer.reserve(2); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.emplace_front(); }, Throws<std::runtime_error>());
  EXPECT_THAT([&buffer] { buffer.emplace_back(); }, Throws<std::runtime_error>());
  EXPECT_THAT(buffer, SizeIs(1));
}

}  // namespace
}  // namespace mbo::container::container_internal
