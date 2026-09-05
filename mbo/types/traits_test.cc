// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mbo/types/traits.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/internal/test_types.h"

namespace mbo::types {
namespace {

// NOLINTBEGIN(*-runtime-int)

using ::mbo::types::types_internal::AnyBaseType;
using ::mbo::types::types_internal::AnyType;
using ::testing::Ne;

// NOLINTNEXTLINE(google-build-using-namespace)
using namespace ::mbo::types::types_internal::test_types;

struct TraitsTest : ::testing::Test {
  template<typename T>
  static std::string DecomposeInfo() {
    return absl::StrCat("  DecomposeInfo: {\n", mbo::types::types_internal::DecomposeInfo<T>::Debug("\n    "), "  \n}");
  }
};

template<IsSameAsAnyOfRaw<int, short> T>
constexpr bool Tester() noexcept {
  return true;
}

template<typename T>
requires NotSameAsAnyOfRaw<T, int, short>
constexpr bool Tester() noexcept {
  return false;
}

TEST_F(TraitsTest, Concepts) {
  static_assert(Tester<int>());
  static_assert(Tester<short>());  // NOLINT(google-runtime-int)
  static_assert(!Tester<uint16_t>());
  static_assert(!Tester<unsigned>());
  static_assert(!Tester<void>());

  static_assert(!Tester<int*>());
  static_assert(Tester<int&>());

  static_assert(std::same_as<int, int>);
  static_assert(!std::same_as<int, unsigned>);
  static_assert(!std::same_as<int, double>);

  static_assert(!IsSameAsAnyOf<int, unsigned, double>);
  static_assert(IsSameAsAnyOf<int, int, unsigned, double>);  // Uses the first type parameter as the type to check.
  static_assert(IsSameAsAnyOf<int, unsigned, int, double>);  // Uses the first type parameter as the type to check.
  static_assert(IsSameAsAnyOf<int, unsigned, double, int>);  // Uses the first type parameter as the type to check.
  static_assert(!IsSameAsAnyOf<unsigned, double, int, int>);
  static_assert(!IsSameAsAnyOf<unsigned, int, double, int>);
}

template<typename TestType>
struct GenTraitsTest : public ::testing::Test {
  static constexpr std::size_t kDerivedFieldCount = static_cast<std::size_t>(TestType::kFieldCount);
  static constexpr std::size_t kBaseFieldCount = static_cast<std::size_t>(TestType::BaseType::kFieldCount);
};

TYPED_TEST_SUITE(GenTraitsTest, AllConstructedTypes);

// NOLINTBEGIN(*-special-member-functions)
struct Virtual {
  virtual ~Virtual() = default;
};

// NOLINTEND(*-special-member-functions)

struct CtorDefault {
  CtorDefault() = default;
};

struct CtorUser {
  CtorUser() {}  // NOLINT(*-use-equals-default)
};

struct CtorBase : CtorUser {};

TEST_F(TraitsTest, DecomposeCountV) {
  ASSERT_THAT(NotDecomposableV, Ne(0));

  ASSERT_THAT(IsAggregate<void>, false);
  EXPECT_THAT(IsDecomposable<void>, false);

  ASSERT_THAT(IsAggregate<void>, false);
  EXPECT_THAT(IsDecomposable<void>, false);
  EXPECT_THAT(DecomposeCountV<void>, NotDecomposableV);

  ASSERT_THAT(IsAggregate<Virtual>, false);
  EXPECT_THAT(IsDecomposable<Virtual>, false);
  EXPECT_THAT(DecomposeCountV<Virtual>, NotDecomposableV);

  ASSERT_THAT(IsAggregate<CtorDefault>, false);
  EXPECT_THAT(IsDecomposable<CtorDefault>, false);
  EXPECT_THAT(DecomposeCountV<CtorDefault>, NotDecomposableV);

  ASSERT_THAT(IsAggregate<CtorUser>, false);
  EXPECT_THAT(IsDecomposable<CtorUser>, false);
  EXPECT_THAT(DecomposeCountV<CtorUser>, NotDecomposableV);

  ASSERT_THAT(types_internal::AggregateHasNonEmptyBase<CtorBase>, false);
  ASSERT_THAT(IsAggregate<CtorBase>, true);
  EXPECT_THAT(IsDecomposable<CtorBase>, false);
  EXPECT_THAT(DecomposeCountV<CtorBase>, NotDecomposableV);

  ASSERT_THAT(types_internal::AggregateHasNonEmptyBase<CtorBase>, false);
  EXPECT_THAT(IsDecomposable<Empty>, false);
  EXPECT_THAT(DecomposeCountV<Empty>, NotDecomposableV);

  ASSERT_THAT(types_internal::AggregateHasNonEmptyBase<CtorBase>, false);
  EXPECT_THAT(IsDecomposable<Base1>, true);
  EXPECT_THAT(DecomposeCountV<Base1>, 1);

  ASSERT_THAT(types_internal::AggregateHasNonEmptyBase<CtorBase>, false);
  EXPECT_THAT(IsDecomposable<Base2>, true);
  EXPECT_THAT(DecomposeCountV<Base2>, 2);
}

struct MadMix {
  int a = 0;
  std::string b;
  char c[5]{};  // NOLINT(*-magic-numbers,*-avoid-c-arrays)

  [[maybe_unused]] friend consteval std::size_t MboTypesDecomposeCount(const MadMix* /*unused*/) { return 3; }

  std::string Print() const {
    const std::string_view str(&c[0], sizeof(c));
    return absl::StrCat("{.a=", a, ", .b=\"", b, "\", .c={", str, "}}");
  }
};

TEST_F(TraitsTest, DecomposeMadMix) {
  ASSERT_THAT(IsAggregate<MadMix>, true);
  EXPECT_THAT(IsDecomposable<MadMix>, true);
  EXPECT_THAT(DecomposeCountV<MadMix>, 3)
      << "  Note: With some restrictions the type can be xreated with up to 7 initializers.\n"
      << "        However C++ does not actually like it and the example results in an error:\n"
      << "        Bad example:\n"
      << "          MadMix{42, \"hallo\", '1', '2', '3', '4', '5'};\n"
      << "          error: suggest braces around initialization of subobject [-Werror,-Wmissing-braces]";
  EXPECT_THAT((MadMix{42, "hallo", {'1', '2', '3', '4', '5'}}).Print(), R"({.a=42, .b="hallo", .c={12345}})");
}

struct StructWithStrings {
  std::string_view one = "1";
  std::string_view two = "22";
  std::string_view three = "333";
  std::string_view four = "4444";
  std::string_view five;
};

TEST_F(TraitsTest, StructWithStrings) {
  using namespace ::mbo::types::types_internal;  // NOLINT(*-build-using-namespace)
  using T = StructWithStrings;
  EXPECT_THAT(DecomposeCountV<T>, 5) << DecomposeInfo<T>();
#if !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET
  EXPECT_THAT((AggregateFieldInitializerCount<T, 0>::value), 2);
  EXPECT_THAT((AggregateFieldInitializerCount<T, 1>::value), 2);
  EXPECT_THAT((AggregateFieldInitializerCount<T, 2>::value), 2);
  EXPECT_THAT((DetectSpecial<AggregateFieldInitializerCount<T, 0>::value, 5 - 0>::value), 2);
  EXPECT_THAT((DetectSpecial<AggregateFieldInitializerCount<T, 1>::value, 5 - 1>::value), 2);
#endif  // !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET
}

TYPED_TEST(GenTraitsTest, DecomposeCountV) {
  using Type = TypeParam;
  constexpr std::size_t kBase = TestFixture::kBaseFieldCount;
  constexpr std::size_t kDerived = TestFixture::kDerivedFieldCount;
  EXPECT_THAT(
      DecomposeCountV<Type>, (kBase && kDerived) || (kBase + kDerived == 0) ? NotDecomposableV : kBase + kDerived);
}

TEST_F(TraitsTest, IsBracesContructible) {
  EXPECT_THAT((IsBracesConstructibleV<Empty>), true);
  EXPECT_THAT((IsBracesConstructibleV<Empty, AnyType>), false);

  EXPECT_THAT((IsBracesConstructibleV<Base1>), true) << "Default construction";
  EXPECT_THAT((IsBracesConstructibleV<Base1, AnyType>), true);
  EXPECT_THAT((IsBracesConstructibleV<Base1, AnyBaseType<void>>), false);
  EXPECT_THAT((IsBracesConstructibleV<Base1, AnyBaseType<Base1>>), false);
  EXPECT_THAT((IsBracesConstructibleV<Base1, int>), true);
  EXPECT_THAT((IsBracesConstructibleV<Base1, void>), false);
  EXPECT_THAT((IsBracesConstructibleV<Base1, std::string>), false);
  EXPECT_THAT((IsBracesConstructibleV<Base1, int, void>), false);

  EXPECT_THAT((IsBracesConstructibleV<Base2>), true) << "Default construction";
  EXPECT_THAT((IsBracesConstructibleV<Base2, AnyType>), true);
  EXPECT_THAT((IsBracesConstructibleV<Base2, int>), true);
  EXPECT_THAT((IsBracesConstructibleV<Base2, int, int>), true);
  EXPECT_THAT((IsBracesConstructibleV<Base2, std::string>), false);
  EXPECT_THAT((IsBracesConstructibleV<Base2, std::string, std::string>), false);
}

TEST_F(TraitsTest, IsBracesContructibleDerived) {
  static_assert(std::is_base_of_v<Empty, ConstructType<0, 0>>, "Bad type generation");
  EXPECT_THAT((IsBracesConstructibleV<ConstructType<0, 0>, Empty>), true);
}

TYPED_TEST(GenTraitsTest, IsAggregate) {
  EXPECT_THAT(IsAggregate<TypeParam>, true);
}

TYPED_TEST(GenTraitsTest, IsBracesContructibleGenerateDerived) {
  using Type = TypeParam;
  constexpr std::size_t kBase = TestFixture::kBaseFieldCount;
  constexpr std::size_t kDerived = TestFixture::kDerivedFieldCount;

  EXPECT_THAT(IsBracesConstructibleV<Type>, true);
  EXPECT_THAT((IsBracesConstructibleV<Type, void>), false);
  EXPECT_THAT((IsBracesConstructibleV<Type, int>), kBase >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, Empty>), kBase == 0);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base1>), kBase == 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base2>), kBase == 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base3>), kBase == 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType>), true);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>>), true);

  EXPECT_THAT((IsBracesConstructibleV<Type, Empty, int>), kBase == 0 && kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base1, int>), kBase == 1 && kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base2, int>), kBase == 2 && kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base3, int>), kBase == 3 && kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, int>), kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType>), kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, int>), kDerived >= 1);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType>), kDerived > 0);

  EXPECT_THAT((IsBracesConstructibleV<Type, Empty, int, int>), kBase == 0 && kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base1, int, int>), kBase == 1 && kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base2, int, int>), kBase == 2 && kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base3, int, int>), kBase == 3 && kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, int, int>), kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType, int>), kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType, AnyType>), kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, int, int>), kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType, int>), kDerived >= 2);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType, AnyType>), kDerived >= 2);

  EXPECT_THAT((IsBracesConstructibleV<Type, Empty, int, int, int>), kBase == 0 && kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base1, int, int, int>), kBase == 1 && kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base2, int, int, int>), kBase == 2 && kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, Base3, int, int, int>), kBase == 3 && kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, int, int, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType, int, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType, AnyType, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyType, AnyType, AnyType, AnyType>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, int, int, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType, int, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType, AnyType, int>), kDerived >= 3);
  EXPECT_THAT((IsBracesConstructibleV<Type, AnyBaseType<Type>, AnyType, AnyType, AnyType>), kDerived >= 3);
}

TEST_F(TraitsTest, ConstructInto) {
  EXPECT_TRUE((ConstructibleInto<std::string, std::string>));
  EXPECT_TRUE((ConstructibleInto<std::string, std::string_view>));
  EXPECT_TRUE((ConstructibleInto<std::string_view, std::string>));
  EXPECT_TRUE((ConstructibleInto<std::string_view, std::string_view>));
  EXPECT_FALSE((ConstructibleInto<std::string, int>));
  EXPECT_FALSE((ConstructibleInto<std::string_view, int>));
  EXPECT_FALSE((ConstructibleInto<int, std::string>));
  EXPECT_FALSE((ConstructibleInto<int, std::string_view>));
}

TEST_F(TraitsTest, ThreeWayComparableTo) {
  static_assert(!ThreeWayComparableTo<double, int, std::strong_ordering>);
  static_assert(!ThreeWayComparableTo<double, int, std::weak_ordering>);
  static_assert(ThreeWayComparableTo<double, int, std::partial_ordering>);
  static_assert(!ThreeWayComparableTo<int, double, std::strong_ordering>);
  static_assert(!ThreeWayComparableTo<int, double, std::weak_ordering>);
  static_assert(ThreeWayComparableTo<int, double, std::partial_ordering>);
}

namespace three_way_comparable_to_1 {
struct A {
  int a;
};

struct B {
  std::string b;

  std::weak_ordering operator<=>(A /*unused*/) const { return std::weak_ordering::equivalent; }
};

// `A <=> B` is defined as `std::weak_ordering` so `std::strong_ordering` is not supported.
static_assert(!ThreeWayComparableTo<A, B, std::strong_ordering>);
static_assert(ThreeWayComparableTo<A, B, std::weak_ordering>);
static_assert(ThreeWayComparableTo<A, B, std::partial_ordering>);

// Since `B <=> A` is not provided, the compiler will use `A <=> B`.
static_assert(!ThreeWayComparableTo<B, A, std::strong_ordering>);
static_assert(ThreeWayComparableTo<B, A, std::weak_ordering>);
static_assert(ThreeWayComparableTo<B, A, std::partial_ordering>);
}  // namespace three_way_comparable_to_1

namespace three_way_comparable_to_2 {

struct B;

struct A {
  int a;

  std::weak_ordering operator<=>(const B& /*unused*/) const = delete;
};

struct B {
  std::string b;

  std::weak_ordering operator<=>(const A& /*unused*/) const { return std::weak_ordering::equivalent; }
};

// `A <=> B` is deleted, so it is not supported.
static_assert(!ThreeWayComparableTo<A, B, std::strong_ordering>);
static_assert(!ThreeWayComparableTo<A, B, std::weak_ordering>);
static_assert(!ThreeWayComparableTo<A, B, std::partial_ordering>);

// `B <=> A` is provided, so it can be used in this version.
static_assert(!ThreeWayComparableTo<B, A, std::strong_ordering>);
static_assert(ThreeWayComparableTo<B, A, std::weak_ordering>);
static_assert(ThreeWayComparableTo<B, A, std::partial_ordering>);

struct C {
  std::unique_ptr<int> c;

  std::weak_ordering operator<=>(const B& /*unused*/) const { return std::weak_ordering::equivalent; }
};

static_assert(!ThreeWayComparableTo<C, B, std::strong_ordering>);
static_assert(ThreeWayComparableTo<C, B, std::weak_ordering>);
static_assert(ThreeWayComparableTo<C, B, std::partial_ordering>);

static_assert(!ThreeWayComparableTo<B, C, std::strong_ordering>);
static_assert(ThreeWayComparableTo<B, C, std::weak_ordering>);
static_assert(ThreeWayComparableTo<B, C, std::partial_ordering>);

template<ThreeWayComparableTo<B> T>
struct X {};

[[maybe_unused]] const X<C> kTest1{};
// [[maybe_unused]] const X<A> kTest2{}; Does not compile because `A <=> B` is deleted.

}  // namespace three_way_comparable_to_2

// NOLINTEND(*-runtime-int)

}  // namespace
}  // namespace mbo::types
