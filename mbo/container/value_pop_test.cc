// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if __cpp_exceptions
# include <stdexcept>
#endif

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/experimental/circular_buffer.h"
#include "mbo/container/limited_vector.h"
#include "mbo/container/segmented_deque.h"
#include "mbo/container/segmented_options.h"
#include "mbo/container/segmented_vector.h"

namespace mbo::container {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::SizeIs;
#if __cpp_exceptions
using ::testing::Throws;
#endif

struct LimitedBack final {
  template<typename T>
  using Container = LimitedVector<T, 4>;
  static constexpr bool kFront = false;
  static constexpr std::string_view kName = "LimitedVectorBack";
};

struct SegmentedBack final {
  template<typename T>
  using Container = SegmentedVector<T, SegmentedOptions{.segment_size = 2}>;
  static constexpr bool kFront = false;
  static constexpr std::string_view kName = "SegmentedVectorBack";
};

struct DequeBack {
  template<typename T>
  using Container = SegmentedDeque<T, SegmentedOptions{.segment_size = 2}>;
  static constexpr bool kFront = false;
  static constexpr std::string_view kName = "SegmentedDequeBack";
};

struct DequeFront final : DequeBack {
  static constexpr bool kFront = true;
  static constexpr std::string_view kName = "SegmentedDequeFront";
};

struct CircularBack {
  template<typename T>
  using Container = experimental::CircularBuffer<T>;
  static constexpr bool kFront = false;
  static constexpr std::string_view kName = "CircularBufferBack";
};

struct CircularFront final : CircularBack {
  static constexpr bool kFront = true;
  static constexpr std::string_view kName = "CircularBufferFront";
};

struct CaseNames final {
  template<typename Case>
  static std::string GetName(int /*unused*/) {
    return std::string(Case::kName);
  }
};

template<typename Case, typename Container>
constexpr auto Pop(Container& container) noexcept(
    !config::kRequireThrows && std::is_nothrow_move_constructible_v<typename Container::value_type>) {
  if constexpr (Case::kFront) {
    return container.pop_front_value();
  } else {
    return container.pop_back_value();
  }
}

template<typename Case>
constexpr bool ConstexprPops() {
  typename Case::template Container<int> container;
  container.emplace_back(1);
  container.emplace_back(2);
  return Pop<Case>(container) == (Case::kFront ? 1 : 2) && container.size() == 1
         && Pop<Case>(container) == (Case::kFront ? 2 : 1) && container.empty();
}

template<typename Case>
struct ValuePopTest : ::testing::Test {};

using Cases = ::testing::Types<LimitedBack, SegmentedBack, DequeBack, DequeFront, CircularBack, CircularFront>;
TYPED_TEST_SUITE(ValuePopTest, Cases, CaseNames);

TYPED_TEST(ValuePopTest, ReturnsMoveOnlyValueAndPreservesRemainingElements) {
  typename TypeParam::template Container<std::unique_ptr<int>> container;
  container.emplace_back(std::make_unique<int>(1));
  container.emplace_back(std::make_unique<int>(2));
  const auto capacity = container.capacity();
  const auto value = Pop<TypeParam>(container);
  EXPECT_THAT(value, Pointee(Eq(TypeParam::kFront ? 1 : 2)));
  EXPECT_THAT(container, SizeIs(1));
  EXPECT_THAT(container.front(), Pointee(Eq(TypeParam::kFront ? 2 : 1)));
  EXPECT_THAT(container.capacity(), Eq(capacity));
  EXPECT_THAT(Pop<TypeParam>(container), Pointee(Eq(TypeParam::kFront ? 2 : 1)));
  EXPECT_THAT(container, IsEmpty());
  container.emplace_back(std::make_unique<int>(3));
  EXPECT_THAT(Pop<TypeParam>(container), Pointee(Eq(3)));
}

TYPED_TEST(ValuePopTest, SupportsConstantEvaluationAndConditionalNoexcept) {
  static_assert(ConstexprPops<TypeParam>());
  using Container = typename TypeParam::template Container<int>;
  if constexpr (TypeParam::kFront) {
    static_assert(noexcept(std::declval<Container&>().pop_front_value()) == !config::kRequireThrows);
  } else {
    static_assert(noexcept(std::declval<Container&>().pop_back_value()) == !config::kRequireThrows);
  }
  EXPECT_THAT(ConstexprPops<TypeParam>(), Eq(true));
}

#if __cpp_exceptions
struct MoveState final {
  int live = 0;
  int copies = 0;
  int moves = 0;
  bool fail = false;
  bool modify_before_throw = false;
};

template<bool Copyable>
struct TrackedValue final {
  explicit TrackedValue(MoveState& state, int value) : state(&state), value(value) { ++state.live; }

  TrackedValue(const TrackedValue& other)
  requires Copyable
      : state(other.state), value(other.value) {
    ++state->live;
    ++state->copies;
  }

  TrackedValue(const TrackedValue&)
  requires(!Copyable)
  = delete;

  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  TrackedValue(TrackedValue&& other) : state(other.state), value(other.value) {
    ++state->moves;
    if (state->modify_before_throw) {
      other.value = -1;
    }
    if (state->fail) {
      throw std::runtime_error("value move failed");
    }
    other.value = -1;
    ++state->live;
  }

  TrackedValue& operator=(const TrackedValue&) = delete;
  TrackedValue& operator=(TrackedValue&&) = delete;

  ~TrackedValue() noexcept { --state->live; }

  MoveState* state;
  int value;
};

TYPED_TEST(ValuePopTest, MovesOnceWithoutCopyFallbackOrExtraReturnMove) {
  MoveState state;
  {
    typename TypeParam::template Container<TrackedValue<true>> container;
    container.reserve(4);
    container.emplace_back(state, 1);
    container.emplace_back(state, 2);
    const auto value = Pop<TypeParam>(container);
    EXPECT_THAT(value.value, Eq(TypeParam::kFront ? 1 : 2));
    EXPECT_THAT(state.moves, Eq(1));
    EXPECT_THAT(state.copies, Eq(0));
    EXPECT_THAT(state.live, Eq(2));
    EXPECT_THAT(container, SizeIs(1));
  }
  EXPECT_THAT(state.live, Eq(0));
}

TYPED_TEST(ValuePopTest, FailedMovePreservesSizeOwnershipAndElementIdentity) {
  for (int modify_source = 0; modify_source != 2; ++modify_source) {
    MoveState state;
    {
      using Container = typename TypeParam::template Container<TrackedValue<false>>;
      Container container;
      container.reserve(4);
      container.emplace_back(state, 1);
      container.emplace_back(state, 2);
      auto* const endpoint = std::addressof(TypeParam::kFront ? container.front() : container.back());
      const auto capacity = container.capacity();
      state.fail = true;
      state.modify_before_throw = modify_source != 0;
      EXPECT_THAT([&container] { static_cast<void>(Pop<TypeParam>(container)); }, Throws<std::runtime_error>());
      EXPECT_THAT(container, SizeIs(2));
      EXPECT_THAT(container.capacity(), Eq(capacity));
      EXPECT_THAT(std::addressof(TypeParam::kFront ? container.front() : container.back()), Eq(endpoint));
      EXPECT_THAT(endpoint->value, Eq(modify_source != 0 ? -1 : (TypeParam::kFront ? 1 : 2)));
      EXPECT_THAT(state.live, Eq(2));
      state.fail = false;
      state.moves = 0;
      endpoint->value = 9;
      const auto value = Pop<TypeParam>(container);
      EXPECT_THAT(value.value, Eq(9));
      EXPECT_THAT(state.moves, Eq(1));
      EXPECT_THAT(state.live, Eq(2));
      EXPECT_THAT(container, SizeIs(1));
    }
    EXPECT_THAT(state.live, Eq(0));
  }
}

TYPED_TEST(ValuePopTest, EmptyRequirementPropagatesWithoutMutation) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "Requires --//mbo/config:require_throws=true";
  }
  typename TypeParam::template Container<int> container;
  const auto capacity = container.capacity();
  EXPECT_THAT([&container] { static_cast<void>(Pop<TypeParam>(container)); }, Throws<std::runtime_error>());
  EXPECT_THAT(container, IsEmpty());
  EXPECT_THAT(container.capacity(), Eq(capacity));
}
#endif  // __cpp_exceptions

}  // namespace
}  // namespace mbo::container
