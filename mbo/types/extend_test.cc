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

#include "mbo/types/extend.h"

#include <concepts>  // IWYU pragma: keep
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/absl_log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/extender.h"
#include "mbo/types/internal/decompose_count.h"
#include "mbo/types/internal/extend.h"
#include "mbo/types/traits.h"
#include "mbo/types/tuple_extras.h"

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wunknown-pragmas"
# pragma clang diagnostic ignored "-Wunused-local-typedef"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif  // defined(__clang__)

namespace std {
// NOLINTBEGIN(misc-use-anonymous-namespace): These are ADL constructs and must be in the correct namespace.
template<typename Sink>
static void AbslStringify(Sink& sink, const std::pair<const int, std::string>& val) {  // NOLINT(cert-dcl58-cpp)
  absl::Format(&sink, R"({%d, "%s"})", val.first, val.second);
}

template<typename Sink, typename... Args>
static void AbslStringify(Sink& sink, const std::variant<Args...>& val) {  // NOLINT(cert-dcl58-cpp)
  std::visit([&sink](auto&& arg) { absl::Format(&sink, "%v", arg); }, val);
}

// NOLINTEND(misc-use-anonymous-namespace)
}  // namespace std

namespace mbo::types {
namespace {

// NOLINTBEGIN(*-magic-numbers)

using ::mbo::types::extender::AbslHashable;
using ::mbo::types::extender::AbslStringify;
using ::mbo::types::extender::Comparable;
using ::mbo::types::extender::Printable;
using ::mbo::types::extender::Streamable;
using ::mbo::types::types_internal::kStructNameSupport;
using ::testing::AnyOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;
using ::testing::WhenSorted;

// Verify the extenders are available as aliases.
static_assert(std::same_as<::mbo::types::extender::AbslStringify, ::mbo::extender::AbslStringify>);
static_assert(std::same_as<::mbo::types::extender::Default, ::mbo::extender::Default>);

// Verify expansion.
static_assert(std::same_as<
              types_internal::ExtendExtenderTupleT<std::tuple<extender::NoPrint>>,
              std::tuple<AbslHashable, AbslStringify, Comparable>>);
static_assert(std::same_as<
              types_internal::ExtendExtenderTupleT<std::tuple<extender::NoPrint, Printable>>,
              std::tuple<AbslHashable, AbslStringify, Comparable, Printable>>);

// Verify order matters.
static_assert(types_internal::ExtenderTupleValid<std::tuple<AbslStringify, Printable>>::value);
static_assert(!types_internal::ExtenderTupleValid<std::tuple<Printable, AbslStringify>>::value);

// Verify the actual short-hand tuples `Default` and `NoPrint`
static_assert(types_internal::ExtenderTupleValid<std::tuple<extender::Default>>::value);
static_assert(types_internal::ExtenderTupleValid<std::tuple<extender::NoPrint>>::value);

// Verify lists...
static_assert(types_internal::ExtenderListValid<extender::Default>);
static_assert(types_internal::ExtenderListValid<AbslHashable, AbslStringify, Comparable, Printable, Streamable>);
static_assert(types_internal::ExtenderListValid<extender::NoPrint, Printable, Streamable>);

struct Empty {};

struct Base1 {
  int a = 0;
};

struct Base2 {
  int a = 0;
  int b = 0;
};

struct Base3 {
  int a = 0;
  int b = 0;
  int c = 0;
};

struct Extend0 : public Extend<Extend0> {};

struct Extend1 : public Extend<Extend1> {
  int a = 0;
};

struct Extend2 : public Extend<Extend2> {
  int a = 0;
  int b = 0;
};

struct Extend4 : public Extend<Extend4> {
  int a = 0;
  int b = 0;
  std::string c;
  const int* ptr = nullptr;
};

struct SimpleName {
  std::string first;
  std::string last;
};

struct SimplePerson : Empty {
  SimpleName name;
  unsigned int age = 0;
};

struct Name : Extend<Name> {
  std::string first;
  std::string last;
};

struct Person : Extend<Person> {
  Name name;
  unsigned age = 0;
};

class ExtendTest : public ::testing::Test {};

TEST_F(ExtendTest, TestDecomposeInfo) {
  using ::mbo::types::types_internal::DecomposeInfo;
#define DEBUG_AND_TEST(Type, kExpected)                            \
  ABSL_LOG(INFO) << #Type << ": " << DecomposeInfo<Type>::Debug(); \
  EXPECT_THAT(DecomposeInfo<Type>::kDecomposeCount, kExpected)

  DEBUG_AND_TEST(Extend4, 4);
  DEBUG_AND_TEST(SimpleName, 2);
  DEBUG_AND_TEST(SimplePerson, 2);
  DEBUG_AND_TEST(Name, 2);
  DEBUG_AND_TEST(Person, 2);
#undef DEBUG_AND_TEST
}

static_assert(kStructNameSupport);

TEST_F(ExtendTest, Test) {
  ASSERT_THAT(IsAggregate<Extend2>, true);
  ASSERT_THAT(IsEmptyType<Extend2>, false);
  ASSERT_THAT(std::is_default_constructible_v<Extend2>, true);
  ASSERT_THAT(types_internal::AggregateHasNonEmptyBase<Extend2>, false);
  ASSERT_THAT(DecomposeCountV<Extend2>, 2);
  ASSERT_THAT((std::same_as<Extend2, Extend2::Type>), true);
}

TEST_F(ExtendTest, Print) {
  {
    const Extend2 ext2{.a = 25, .b = 42};
    ASSERT_THAT(DecomposeCountV<decltype(ext2)>, 2);
    EXPECT_THAT(ext2.ToString(), "{.a: 25, .b: 42}");
  }

  {
    const Extend4 ext4{.a = 25, .b = 42, .c = "Hello There!"};
    ASSERT_THAT(DecomposeCountV<decltype(ext4)>, 4);
    EXPECT_THAT(ext4.ToString(), R"({.a: 25, .b: 42, .c: "Hello There!", .ptr: <nullptr>})");
  }
  {
    constexpr int kVal = 1'337;
    const Extend4 ext4{.a = 25, .b = 42, .c = "Hello There!", .ptr = &kVal};
    ASSERT_THAT(DecomposeCountV<decltype(ext4)>, 4);
    EXPECT_THAT(ext4.ToString(), R"({.a: 25, .b: 42, .c: "Hello There!", .ptr: *{1337}})");
  }
}

TEST_F(ExtendTest, NestedPrint) {
  const Person person{.name = {.first = "First", .last = "Last"}, .age = 42};
  static constexpr std::string_view kExpected = R"({.name: {.first: "First", .last: "Last"}, .age: 42})";
  EXPECT_THAT(person.ToString(), kExpected);
  EXPECT_THAT(absl::StrFormat("%v", person), kExpected);
}

TEST_F(ExtendTest, Streamable) {
  const Extend4 ext4{.a = 25, .b = 42};
  std::ostringstream ss4;
  ss4 << ext4;
  EXPECT_THAT(ss4.str(), R"({.a: 25, .b: 42, .c: "", .ptr: <nullptr>})");
}

#if defined(__clang__)

namespace debug {

int DumpStructVisitor(
    std::size_t /*field_index*/,
    std::string_view format,
    std::string_view indent = {},
    std::string_view type = {},
    std::string_view name = {},
    ...) {
  std::cout << "Format: '" << format << "'";
  std::cout << ", Indent: '" << indent << "'";
  std::cout << ", Type: '" << type << "'";
  std::cout << ", Name: '" << name << "'";
  std::cout << '\n';
  return 0;
}

struct StructVisitorElement {
  std::string format;
  std::string indent;
  std::string type;
  std::string name;
  std::string line;
};

void PrintStructVisitor(
    std::size_t /*field_index*/,
    std::vector<StructVisitorElement>& fields,
    std::string& longest_type,
    bool print,
    std::string_view format,
    std::string_view indent = {},
    std::string_view type = {},
    std::string_view name = {},
    ...) {
  const std::string line = [&]() -> std::string {
    if (!format.starts_with('%')) {
      return std::string(format);
    } else if (format == "%s" || format == "%s}\n") {
      return absl::StrCat(indent, format.substr(2));
    } else if (format == "%s%s") {
      return absl::StrFormat("%s%s", indent.data(), type.data());
    } else if (format == "%s%s %s =") {
      return absl::StrFormat("%s%s %s =", indent.data(), type.data(), name.data());
    } else if (format.starts_with("%s%s %s =")) {
      return absl::StrFormat("%s%s %s =\n", indent.data(), type.data(), name.data());
    } else {
      return absl::StrFormat("Unknown format: '%s'", format);
    }
  }();
  if (print) {
    std::cout << line;
  }
  if (type.length() > longest_type.length()) {
    longest_type = type;
  }
  if (format.starts_with("%s%s %s =") && indent == "  ") {
    fields.emplace_back(StructVisitorElement{.format{format}, .indent{indent}, .type{type}, .name{name}, .line = line});
  }
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
template<typename T>
std::string Print(const T* ptr, bool print = false) {
  std::string longest_type;
  std::size_t field_index{0};
  std::vector<StructVisitorElement> fields;
  __builtin_dump_struct(ptr, &PrintStructVisitor, field_index, fields, longest_type, print);
  if (print) {
    field_index = 0;
    __builtin_dump_struct(ptr, &DumpStructVisitor, field_index);
  }
  return longest_type;
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg,hicpp-vararg)

}  // namespace debug

#endif  // __clang__

struct PersonData : Extend<PersonData> {
  int index = 0;
  Person person;
  const std::set<std::string>* data = nullptr;
};

TEST_F(ExtendTest, StreamableComplexFields) {
  const std::set<std::string> data{"foo", "bar"};
  const PersonData person{
      .index = 25,
      .person =
          {
              .name =
                  {
                      .first = "Hugo",
                      .last = "Meyer",
                  },
              .age = 42,
          },
      .data = &data,
  };
  std::ostringstream out;
  out << person;
  // NOTE: For Clang the top struct's field names are not present because the generated type names are far too long.
  EXPECT_THAT(
      out.str(),
      R"({.index: 25, .person: {.name: {.first: "Hugo", .last: "Meyer"}, .age: 42}, .data: *{{"bar", "foo"}}})");
#ifdef __clang__
  std::cout << "Person:\n";
  EXPECT_THAT(debug::Print(&person), SizeIs(Le(198)));
  std::cout << "Person::person.name:\n";
  EXPECT_THAT(debug::Print(&person.person.name), SizeIs(Le(192)));
#endif  // __clang__
}

struct WithUnion : mbo::types::Extend<WithUnion> {
  union U {
    int first{2};
    int second;

    template<typename Sink>
    friend void AbslStringify(Sink& sink, const U& val) {
      absl::Format(&sink, "%d", val.first);  // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
  };

  int first{1};
  U second;
  int third{3};
};

TEST_F(ExtendTest, StaticTests) {
  static_assert(!HasUnionMember<int>);
  static_assert(HasUnionMember<WithUnion>);

#if !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET
  static_assert(types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<0>::value);
  static_assert(types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<1>::value);
  static_assert(types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<2>::value);
  static_assert(types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<3>::value);
  static_assert(types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<4>::value);
  static_assert(!types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<5>::value);
  static_assert(!types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<6>::value);
  static_assert(!types_internal::AggregateInitializeTest<WithUnion>::IsInitializable<7>::value);
  static_assert(types_internal::DecomposeInfo<WithUnion>::kInitializerCount == 4);
  static_assert(types_internal::DecomposeInfo<WithUnion>::kFieldCount == 4);
  static_assert(types_internal::DecomposeInfo<WithUnion>::kCountBases == 0);
  static_assert(types_internal::DecomposeInfo<WithUnion>::kCountEmptyBases == 1);
  static_assert(types_internal::DecomposeCountImpl<WithUnion>::value == 3);
#endif  // !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET
}

struct WithAnonymousUnion {  // Cannot extend due to anonymous union
  int first{1};

  union {
    int second{2};
    int third;
  };

  int fourth{3};
};

// cannot decompose class type '...WithAnonymousUnion' because it has an anonymous union member
//    auto& [a1, a2, a3] = data;
// static_assert(::mbo::types::HasUnionMember<WithAnonymousUnion>);

TEST_F(ExtendTest, StreamableWithUnion) {
  constexpr WithUnion kTest{
      .first = 25,
      .second{.second = 42},
      .third = 99,
  };

#if defined(__GNUC__) && !defined(__clang__)
  EXPECT_THAT(kTest.ToString(), R"({.first: 25, .second: 42, .third: 99})");
#else
  EXPECT_THAT(kTest.ToString(), R"({25, 42, 99})");
#endif
}

struct ComparableTest : ExtendTest {
  struct TestComparable : public ExtendNoDefault<TestComparable, Comparable> {
    int a = 0;
    int b = 0;
  };

  struct Flat {
    int a = 0;
    int b = 0;
  };

  static constexpr TestComparable kTest1{.a = 25, .b = 42};
  static constexpr TestComparable kTest2{.a = 25, .b = 43};
  static constexpr TestComparable kTest3{.a = 26, .b = 42};
  static constexpr TestComparable kTest4{.a = 25, .b = 42};

  static constexpr Flat kFlat1{.a = 25, .b = 42};
  static constexpr Flat kFlat2{.a = 25, .b = 43};
  static constexpr Flat kFlat3{.a = 26, .b = 42};
  static constexpr Flat kFlat4{.a = 25, .b = 42};
};

TEST_F(ComparableTest, SameType1vs1) {
  EXPECT_THAT(kTest1 == kTest1, true);
  EXPECT_THAT(kTest1 != kTest1, false);
  EXPECT_THAT(kTest1 < kTest1, false);
  EXPECT_THAT(kTest1 <= kTest1, true);
  EXPECT_THAT(kTest1 > kTest1, false);
  EXPECT_THAT(kTest1 >= kTest1, true);
}

TEST_F(ComparableTest, SameType1vs2) {
  EXPECT_THAT(kTest1 == kTest2, false);
  EXPECT_THAT(kTest1 != kTest2, true);
  EXPECT_THAT(kTest1 < kTest2, true);
  EXPECT_THAT(kTest1 <= kTest2, true);
  EXPECT_THAT(kTest1 > kTest2, false);
  EXPECT_THAT(kTest1 >= kTest2, false);
}

TEST_F(ComparableTest, SameType1vs3) {
  EXPECT_THAT(kTest1 == kTest3, false);
  EXPECT_THAT(kTest1 != kTest3, true);
  EXPECT_THAT(kTest1 < kTest3, true);
  EXPECT_THAT(kTest1 <= kTest3, true);
  EXPECT_THAT(kTest1 > kTest3, false);
  EXPECT_THAT(kTest1 >= kTest3, false);
}

TEST_F(ComparableTest, SameType1vs4) {
  EXPECT_THAT(kTest1 == kTest4, true);
  EXPECT_THAT(kTest1 != kTest4, false);
  EXPECT_THAT(kTest1 < kTest4, false);
  EXPECT_THAT(kTest1 <= kTest4, true);
  EXPECT_THAT(kTest1 > kTest4, false);
  EXPECT_THAT(kTest1 >= kTest4, true);
}

TEST_F(ComparableTest, SameType2vs3) {
  EXPECT_THAT(kTest2 == kTest3, false);
  EXPECT_THAT(kTest2 != kTest3, true);
  EXPECT_THAT(kTest2 < kTest3, true);
  EXPECT_THAT(kTest2 <= kTest3, true);
  EXPECT_THAT(kTest2 > kTest3, false);
  EXPECT_THAT(kTest2 >= kTest3, false);
  EXPECT_THAT(kTest2, Not(kTest3));
  EXPECT_THAT(kTest2, Not(Eq(kTest3)));
  EXPECT_THAT(kTest2, Lt(kTest3));
  EXPECT_THAT(kTest2, Le(kTest3));
  EXPECT_THAT(kTest2, Not(Gt(kTest3)));
  EXPECT_THAT(kTest2, Not(Ge(kTest3)));
}

TEST_F(ComparableTest, SameType3vs2) {
  EXPECT_THAT(kTest3 == kTest2, false);
  EXPECT_THAT(kTest3 != kTest2, true);
  EXPECT_THAT(kTest3 < kTest2, false);
  EXPECT_THAT(kTest3 <= kTest2, false);
  EXPECT_THAT(kTest3 > kTest2, true);
  EXPECT_THAT(kTest3 >= kTest2, true);
  EXPECT_THAT(kTest3, Not(kTest2));
  EXPECT_THAT(kTest3, Not(Eq(kTest2)));
  EXPECT_THAT(kTest3, Not(Lt(kTest2)));
  EXPECT_THAT(kTest3, Not(Le(kTest2)));
  EXPECT_THAT(kTest3, Gt(kTest2));
  EXPECT_THAT(kTest3, Ge(kTest2));
}

TEST_F(ComparableTest, SimilarTypeT1vsF1) {
  // Same kind of comparison, just with similar type: T1 vs F1.
  EXPECT_THAT(kTest1 == kFlat1, true);
  EXPECT_THAT(kTest1 != kFlat1, false);
  EXPECT_THAT(kTest1 < kFlat1, false);
  EXPECT_THAT(kTest1 <= kFlat1, true);
  EXPECT_THAT(kTest1 > kFlat1, false);
  EXPECT_THAT(kTest1 >= kFlat1, true);
}

TEST_F(ComparableTest, SimilarTypeF1vsT1) {
  // Same kind of comparison, just with similar type, otherway round: F1 vs T1.
  EXPECT_THAT(kFlat1 == kTest1, true);
  EXPECT_THAT(kFlat1 != kTest1, false);
  EXPECT_THAT(kFlat1 < kTest1, false);
  EXPECT_THAT(kFlat1 <= kTest1, true);
  EXPECT_THAT(kFlat1 > kTest1, false);
  EXPECT_THAT(kFlat1 >= kTest1, true);
}

TEST_F(ComparableTest, SimilarTypeT1vsFx) {
  // Same kind of comparison, just with similar type: T1 vs F2.
  EXPECT_THAT(kTest1 == kFlat2, false);
  EXPECT_THAT(kTest1 != kFlat2, true);
  EXPECT_THAT(kTest1 < kFlat2, true);
  EXPECT_THAT(kTest1 <= kFlat2, true);
  EXPECT_THAT(kTest1 > kFlat2, false);
  EXPECT_THAT(kTest1 >= kFlat2, false);
  // Same kind of comparison, just with similar type: T1 vs F3.
  EXPECT_THAT(kTest1 == kFlat3, false);
  EXPECT_THAT(kTest1 != kFlat3, true);
  EXPECT_THAT(kTest1 < kFlat3, true);
  EXPECT_THAT(kTest1 <= kFlat3, true);
  EXPECT_THAT(kTest1 > kFlat3, false);
  EXPECT_THAT(kTest1 >= kFlat3, false);
  // Same kind of comparison, just with similar type: T1 vs F4.
  EXPECT_THAT(kTest1 == kFlat4, true);
  EXPECT_THAT(kTest1 != kFlat4, false);
  EXPECT_THAT(kTest1 < kFlat4, false);
  EXPECT_THAT(kTest1 <= kFlat4, true);
  EXPECT_THAT(kTest1 > kFlat4, false);
  EXPECT_THAT(kTest1 >= kFlat4, true);
}

TEST_F(ComparableTest, UsingGMock1vs1) {
  EXPECT_THAT(kTest1, kTest1);  // shortcut for Eq, see below
  EXPECT_THAT(kTest1, Eq(kTest1));
  EXPECT_THAT(kTest1, Not(Lt(kTest1)));
  EXPECT_THAT(kTest1, Le(kTest1));
  EXPECT_THAT(kTest1, Not(Gt(kTest1)));
  EXPECT_THAT(kTest1, Ge(kTest1));
}

TEST_F(ComparableTest, UsingGMock1vs2) {
  EXPECT_THAT(kTest1, Not(kTest2));
  EXPECT_THAT(kTest1, Not(Eq(kTest2)));
  EXPECT_THAT(kTest1, Lt(kTest2));
  EXPECT_THAT(kTest1, Le(kTest2));
  EXPECT_THAT(kTest1, Not(Gt(kTest2)));
  EXPECT_THAT(kTest1, Not(Ge(kTest2)));
}

TEST_F(ComparableTest, UsingGMock1vs3) {
  EXPECT_THAT(kTest1, Not(kTest3));
  EXPECT_THAT(kTest1, Not(Eq(kTest3)));
  EXPECT_THAT(kTest1, Lt(kTest3));
  EXPECT_THAT(kTest1, Le(kTest3));
  EXPECT_THAT(kTest1, Not(Gt(kTest3)));
  EXPECT_THAT(kTest1, Not(Ge(kTest3)));
}

TEST_F(ComparableTest, UsingGMock1vs4) {
  EXPECT_THAT(kTest1, kTest4);  // shortcut for Eq, see below
  EXPECT_THAT(kTest1, Eq(kTest4));
  EXPECT_THAT(kTest1, Not(Lt(kTest4)));
  EXPECT_THAT(kTest1, Le(kTest4));
  EXPECT_THAT(kTest1, Not(Gt(kTest4)));
  EXPECT_THAT(kTest1, Ge(kTest4));
}

struct PlainName {
  std::string first;
  std::string last;

  template<typename H>
  friend H AbslHashValue(H hash, const PlainName& obj) noexcept {
    return H::combine(std::move(hash), obj.first, obj.last);
  }
};

struct PlainPerson {
  PlainName name;
  std::size_t age = 0;

  template<typename H>
  friend H AbslHashValue(H hash, const PlainPerson& obj) noexcept {
    return H::combine(std::move(hash), obj.name, obj.age);
  }
};

TEST_F(ExtendTest, Hashable) {
  const Person person{.name = {.first = "First", .last = "Last"}, .age = 42};
  const PlainPerson plain_person{.name = {.first = "First", .last = "Last"}, .age = 42};

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({person, Person{}}));

  ASSERT_THAT((::mbo::types::IsExtended<Name>), true);
  ASSERT_THAT((::mbo::types::IsExtended<Person>), true);
  ASSERT_THAT((::mbo::types::IsExtended<PlainName>), false);
  ASSERT_THAT((::mbo::types::IsExtended<PlainPerson>), false);
  ASSERT_THAT(Person::RegisteredExtenderNames(), Contains("AbslHashable"));
  ASSERT_THAT(Person::RegisteredExtenderNames().size(), std::tuple_size_v<Person::RegisteredExtenders>);
  ASSERT_THAT((::mbo::types::types_internal::HasExtender<Person, AbslHashable>), true);

  EXPECT_THAT(absl::HashOf(person), Ne(0));
  EXPECT_THAT(absl::HashOf(person), absl::HashOf(plain_person));
  EXPECT_THAT(absl::HashOf(person), std::hash<Person>{}(person));
}

TEST_F(ExtendTest, Default) {
  struct NameDefault : Extend<NameDefault> {
    std::string first;
    std::string last;
  };

  ASSERT_THAT((::mbo::types::IsExtended<NameDefault>), true);
  EXPECT_THAT(
      NameDefault::RegisteredExtenderNames(),
      UnorderedElementsAre("AbslHashable", "AbslStringify", "Comparable", "Printable", "Streamable"));
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameDefault, Printable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameDefault, Streamable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameDefault, Comparable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameDefault, AbslHashable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameDefault, AbslStringify>), true);
}

TEST_F(ExtendTest, NoDefault) {
  struct NameNoDefault : ExtendNoDefault<NameNoDefault> {
    std::string first;
    std::string last;
  };

  ASSERT_THAT((::mbo::types::IsExtended<NameNoDefault>), true);
  EXPECT_THAT(NameNoDefault::RegisteredExtenderNames(), IsEmpty());
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoDefault, Printable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoDefault, Streamable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoDefault, Comparable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoDefault, AbslHashable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoDefault, AbslStringify>), false);
}

TEST_F(ExtendTest, NoPrint) {
  struct NameNoPrint : ExtendNoPrint<NameNoPrint> {
    std::string first;
    std::string last;
  };

  ASSERT_THAT((::mbo::types::IsExtended<NameNoPrint>), true);
  static_assert(::mbo::types::IsExtended<NameNoPrint>);
  EXPECT_THAT(
      NameNoPrint::RegisteredExtenderNames(), UnorderedElementsAre("AbslHashable", "AbslStringify", "Comparable"));
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoPrint, Printable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoPrint, Streamable>), false);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoPrint, Comparable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoPrint, AbslHashable>), true);
  EXPECT_THAT((::mbo::types::types_internal::HasExtender<NameNoPrint, AbslStringify>), true);
}

TEST_F(ExtendTest, ExtenderNames) {
  struct T0 : ExtendNoDefault<T0> {};

  struct T1 : ExtendNoDefault<T1, AbslStringify, Printable> {};

  struct T2 : ExtendNoDefault<T2, AbslStringify, Streamable> {};

  struct T3a : ExtendNoDefault<T3a, AbslStringify, Comparable, Printable, Streamable> {};

  struct T3b : ExtendNoDefault<T3b, AbslStringify, Streamable, Printable, Comparable> {};

  struct T4 : Extend<T4> {};

  EXPECT_THAT(  // No defaults, no extras.
      T0::RegisteredExtenderNames(), IsEmpty());
  EXPECT_THAT(  // No default, only the specified extra.
      T1::RegisteredExtenderNames(), WhenSorted(ElementsAre("AbslStringify", "Printable")));
  EXPECT_THAT(  // No defaults, two extra.
      T2::RegisteredExtenderNames(), WhenSorted(ElementsAre("AbslStringify", "Streamable")));
  EXPECT_THAT(  // All defaults, no extra.
      T3a::RegisteredExtenderNames(),
      WhenSorted(ElementsAre("AbslStringify", "Comparable", "Printable", "Streamable")));
  EXPECT_THAT(  // All defaults, no extra.
      T3b::RegisteredExtenderNames(),
      WhenSorted(ElementsAre("AbslStringify", "Comparable", "Printable", "Streamable")));
  EXPECT_THAT(  // All defaults, and as extra.
      T4::RegisteredExtenderNames(),
      WhenSorted(ElementsAre("AbslHashable", "AbslStringify", "Comparable", "Printable", "Streamable")));
}

template<typename T>
struct Crtp {
  // Technically `Crtp` is not needed for the test, but we want to ensure this works with CRTP types.

 private:
  Crtp() = default;
  friend T;
};

struct Crtp1 : Crtp<Crtp1> {
  Crtp1() = delete;

  explicit Crtp1(int v) : value(v) {}

  int value{0};
};

struct Crtp2 : Crtp<Crtp2> {
  Crtp2() = delete;

  explicit Crtp2(int v) : value(v) {}

  int value{0};
};

struct UseCrtp1 : Extend<UseCrtp1> {
  Crtp1 crtp;
};

struct UseCrtp2 : Extend<UseCrtp2> {
  Crtp2 crtp;
};

struct UseBoth : Extend<UseBoth> {
  Crtp1 crtp1;
  Crtp2 crtp2;
};

static_assert(IsAggregate<UseBoth>);

TEST_F(ExtendTest, StaticTestsCrtpInCrtp) {
#if !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<0>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<1>::value);
  static_assert(types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<2>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<3>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<4>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<5>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<6>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseCrtp1>::IsInitializable<7>::value);

  static_assert(types_internal::AggregateInitializerCount<UseCrtp1>::value == 2);
  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kInitializerCount == 2);

  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kFieldCount == 2);
  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kDecomposeCount == 1);
  static_assert(!types_internal::DecomposeInfo<UseCrtp1>::kBadFieldCount);
  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kIsAggregate);
  static_assert(!types_internal::DecomposeInfo<UseCrtp1>::kIsEmpty);
  static_assert(!types_internal::DecomposeInfo<UseCrtp1>::kOneNonEmptyBase);
  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kOnlyEmptyBases);
  static_assert(!types_internal::DecomposeInfo<UseCrtp1>::kOneNonEmptyBasePlusFields);

  static_assert(types_internal::DecomposeInfo<UseCrtp1>::kDecomposeCount == 1);

  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<0>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<1>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<2>::value);
  static_assert(types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<3>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<4>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<5>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<6>::value);
  static_assert(!types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<7>::value);

  static_assert(types_internal::IsAggregateInitializableWithNumArgs<UseBoth, 3>);
  static_assert(types_internal::AggregateInitializeTest<UseBoth>::IsInitializable<3>::value);
  static_assert(types_internal::AggregateInitializerCount<UseBoth>::value == 3);
  static_assert(types_internal::DecomposeInfo<UseBoth>::kInitializerCount == 3);

  static_assert(types_internal::DecomposeInfo<UseBoth>::kFieldCount == 3);
  static_assert(!types_internal::DecomposeInfo<UseBoth>::kBadFieldCount);
  static_assert(types_internal::DecomposeInfo<UseBoth>::kIsAggregate);
  static_assert(!types_internal::DecomposeInfo<UseBoth>::kIsEmpty);
  static_assert(!types_internal::DecomposeInfo<UseBoth>::kOneNonEmptyBase);
  static_assert(types_internal::DecomposeInfo<UseBoth>::kOnlyEmptyBases);
  static_assert(!types_internal::DecomposeInfo<UseBoth>::kOneNonEmptyBasePlusFields);

  static_assert(types_internal::DecomposeInfo<UseBoth>::kDecomposeCount == 2);
#endif  // !MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET

  static_assert(!IsDecomposable<Crtp1>);
  static_assert(!IsDecomposable<Crtp2>);
  static_assert(IsDecomposable<UseCrtp1>);
  static_assert(IsDecomposable<UseCrtp2>);
  static_assert(IsDecomposable<UseBoth>);
}

TEST_F(ExtendTest, NoDefaultConstructor) {
  ABSL_LOG(ERROR) << types_internal::DecomposeInfo<UseBoth>::Debug();
}

struct AbslFlatHashMapUser : Extend<AbslFlatHashMapUser> {
  using MboTypesStringifyDoNotPrintFieldNames = void;
  absl::flat_hash_map<int, std::string> flat_hash_map;
};

static_assert(std::is_default_constructible_v<AbslFlatHashMapUser>);
static_assert(!std::is_trivially_default_constructible_v<AbslFlatHashMapUser>);
static_assert(std::default_initializable<AbslFlatHashMapUser>);

static_assert(std::is_destructible_v<AbslFlatHashMapUser>);
static_assert(!std::is_trivially_destructible_v<AbslFlatHashMapUser>);

static_assert(!types::HasUnionMember<AbslFlatHashMapUser>);
static_assert(!types::HasVariantMember<AbslFlatHashMapUser>);

static_assert(DecomposeCountV<AbslFlatHashMapUser> == 1);
static_assert(types_internal::SupportsFieldNames<AbslFlatHashMapUser>);

TEST_F(ExtendTest, AbseilFlatHashMapMember) {
  const AbslFlatHashMapUser data = {
      .flat_hash_map = {{25, "25"}, {42, "42"}},
  };
  EXPECT_THAT(
      data.ToString(), AnyOf(EndsWith(R"({{25, "25"}, {42, "42"}}})"), EndsWith(R"({{42, "42"}, {25, "25"}}})")));
}

struct WithVariant : Extend<WithVariant> {
  std::variant<int, unsigned> value;
};

static_assert(types::HasVariantMember<WithVariant>);

static_assert(DecomposeCountV<WithVariant> == 1);
static_assert(types_internal::SupportsFieldNames<WithVariant>);

TEST_F(ExtendTest, VariantMember) {
  const WithVariant data = {
      .value = 69,
  };
  EXPECT_THAT(data.ToString(), R"({.value: 69})");
}

struct MoveOnly {
  constexpr explicit MoveOnly(int val) noexcept : value(val) {}

  constexpr MoveOnly() = delete;
  constexpr ~MoveOnly() noexcept = default;

  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) noexcept = default;
  MoveOnly& operator=(MoveOnly&&) noexcept = default;

  bool operator==(const MoveOnly& other) const noexcept { return value == other; }

  bool operator==(int other) const noexcept { return value == other; }

  int value{0};
};

struct UseMoveOnly : Extend<UseMoveOnly> {
  MoveOnly one{0};
  MoveOnly two{0};
};

TEST_F(ExtendTest, MoveOnlyTuple) {
  // Verify that `ToTuple` works for `move-eligble` values as expected: ToTuple(Extend&&) -> std::tuple<T...>.
  static constexpr int kValue1{25};
  static constexpr int kValue2{33};
  static constexpr int kValue3{42};
  UseMoveOnly data{
      .one{MoveOnly{kValue1}},
      .two{MoveOnly{kValue2}},
  };
  auto [move1, move2] = std::move(data);
  EXPECT_THAT(move1.value, kValue1);
  EXPECT_THAT(move2.value, kValue2);

  move1.value = kValue3;
  EXPECT_THAT(move1.value, kValue3);
  EXPECT_THAT(move2.value, kValue2);
  EXPECT_THAT(data.one.value, Ne(kValue3))  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
      << "It does not matter what the value is after the move, but it must not be `kValue3`.";
}

TEST_F(ExtendTest, FromTupleToVariants) {
  struct TestStruct : Extend<TestStruct> {
    std::variant<int, std::string, std::string_view> one;
    std::variant<int, std::string, std::string_view> two;
  };

  static constexpr int kInt1{25};
  static constexpr int kInt2{33};
  static constexpr std::string_view kStr1{"a"};
  static constexpr std::string_view kStr2{"b"};
  {
    const auto val1 = TestStruct::ConstructFromTuple(std::make_tuple(kInt1, kInt2));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val1)>, TestStruct>));
    EXPECT_THAT(val1.one, kInt1);
    EXPECT_THAT(val1.two, kInt2);
  }
  {
    const auto val2 = TestStruct::ConstructFromTuple(std::make_tuple(kStr1, kStr2));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val2)>, TestStruct>));
    EXPECT_THAT(val2.one, kStr1);
    EXPECT_THAT(val2.two, kStr2);
  }
  {
    const auto val3 = TestStruct::ConstructFromTuple(std::make_tuple(kInt1, kStr2));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val3)>, TestStruct>));
    EXPECT_THAT(val3.one, kInt1);
    EXPECT_THAT(val3.two, kStr2);
  }
  {
    const auto val4 = TestStruct::ConstructFromArgs(kStr1, kInt2);
    static_assert((std::same_as<std::remove_cvref_t<decltype(val4)>, TestStruct>));
    EXPECT_THAT(val4.one, kStr1);
    EXPECT_THAT(val4.two, kInt2);
  }
}

TEST_F(ExtendTest, FromTupleToStrings) {
  struct TestStruct : Extend<TestStruct> {
    std::string one;
    std::string two;
  };

  const std::string str1{"a"};
  const std::string str2{"b"};
  {
    const auto val1 = TestStruct::ConstructFromTuple(std::make_tuple(str1, str2));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val1)>, TestStruct>));
    EXPECT_THAT(val1.one, str1);
    EXPECT_THAT(val1.two, str2);
  }
  {
    const auto val2 = TestStruct::ConstructFromArgs(str1, str2);
    static_assert((std::same_as<std::remove_cvref_t<decltype(val2)>, TestStruct>));
    EXPECT_THAT(val2.one, str1);
    EXPECT_THAT(val2.two, str2);
  }
}

TEST_F(ExtendTest, FromConversions) {
  // You cannot directly initialize a std::string field with a std::string_view value, a conversion is needed.
  struct TestStruct : Extend<TestStruct> {
    std::string one;
    std::string two;
  };

  constexpr std::string_view kSv1{"aa"};
  constexpr std::string_view kSv2{"bb"};
  {
    const auto val2 = TestStruct::ConstructFromConversions(kSv1, kSv2);
    static_assert((std::same_as<std::remove_cvref_t<decltype(val2)>, TestStruct>));
    EXPECT_THAT(val2.one, kSv1);
    EXPECT_THAT(val2.two, kSv2);
  }
}

TEST_F(ExtendTest, MoveOnlyFromTuple) {
  static constexpr int kInt1{25};
  static constexpr int kInt2{33};
  {
    const auto val2 = UseMoveOnly::ConstructFromTuple(std::make_tuple(MoveOnly(kInt1), MoveOnly(kInt2)));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val2)>, UseMoveOnly>));
    EXPECT_THAT(val2.one, kInt1);
    EXPECT_THAT(val2.two, kInt2);
  }
  // const auto val3 = UseMoveOnly::FromArgs(kValue1, kValue2);
  // static_assert((std::same_as<std::remove_cvref_t<decltype(val3)>, UseMoveOnly>));
  {
    const auto val4 = UseMoveOnly::ConstructFromArgs(MoveOnly(kInt1), MoveOnly(kInt2));
    static_assert((std::same_as<std::remove_cvref_t<decltype(val4)>, UseMoveOnly>));
    EXPECT_THAT(val4.one, kInt1);
    EXPECT_THAT(val4.two, kInt2);
  }
}

TEST_F(ExtendTest, EmptyExtend) {
  struct Empty : Extend<Empty> {};

  ASSERT_TRUE((std::same_as<decltype(StructToTuple(Empty{})), std::tuple<>>));
  ASSERT_TRUE(CanCreateTuple<Empty>);
  ASSERT_FALSE(CanCreateTuple<Empty&>);
}

TEST_F(ExtendTest, SmartPtr) {
  struct T : Extend<T> {
    using MboTypesStringifyDoNotPrintFieldNames = void;
    std::unique_ptr<std::string> ups;
  };

  T val{.ups = std::make_unique<std::string>("foo")};

  EXPECT_THAT(StructToTuple(val), FieldsAre(Pointee(StrEq("foo"))));
  EXPECT_THAT(StructToTuple(std::move(val)), FieldsAre(Pointee(StrEq("foo"))));
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::types

#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif  // defined(__clang__)
