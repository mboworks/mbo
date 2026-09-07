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

#include "mbo/json/json.h"

#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/types/typed_view.h"

// NOLINTBEGIN(*-magic-*,*-avoid-unchecked-container-access)
// The subscript operators are the API under test here. The property overload
// deliberately inserts, while indexed access bounds-checks through
// MBO_CONFIG_REQUIRE.

namespace mbo::json {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsNullopt;
using ::mbo::types::TypedView;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

struct JsonTest : ::testing::Test {};

TEST_F(JsonTest, Test) {
  EXPECT_THAT(Json{}, IsNullopt());
  EXPECT_THAT(Json{}.Serialize(), EqualsText(R"({}
)"));
}

TEST_F(JsonTest, Comparison) {
  static_assert(std::three_way_comparable_with<Json, Json>);
  static_assert(types::ThreeWayComparableTo<Json, int>);
  static_assert(types::ThreeWayComparableTo<Json, unsigned>);
  EXPECT_THAT(Json{}, std::nullopt);
  EXPECT_THAT(Json{}, IsNullopt());
  EXPECT_THAT(Json{1}, Not(std::nullopt));
  EXPECT_THAT(Json{1}, Not(IsNullopt()));
  EXPECT_THAT(Json{2}, 2);
  EXPECT_THAT(Json{"yes"}, "yes");
  EXPECT_THAT(Json{3}, Not("nope"));
}

TEST_F(JsonTest, ScalarComparisonsCoverSupportedRepresentations) {
  EXPECT_THAT(Json{false}, false);
  EXPECT_THAT(Json{true}, true);
  EXPECT_THAT(Json{std::int8_t{-8}}, std::int8_t{-8});
  EXPECT_THAT(Json{std::int16_t{-16}}, std::int16_t{-16});
  EXPECT_THAT(Json{std::int32_t{-32}}, std::int32_t{-32});
  EXPECT_THAT(Json{std::int64_t{-64}}, std::int64_t{-64});
  EXPECT_THAT(Json{std::uint8_t{8}}, std::uint8_t{8});
  EXPECT_THAT(Json{std::uint16_t{16}}, std::uint16_t{16});
  EXPECT_THAT(Json{std::uint32_t{32}}, std::uint32_t{32});
  EXPECT_THAT(Json{std::uint64_t{64}}, std::uint64_t{64});
  EXPECT_THAT(Json{1.25F}, 1.25F);
  EXPECT_THAT(Json{2.5}, 2.5);
  EXPECT_THAT(Json{"text"}, "text");
  EXPECT_THAT(Json{std::string{"text"}}, std::string{"text"});
  EXPECT_THAT(Json{std::string_view{"text"}}, std::string_view{"text"});

  EXPECT_THAT(Json{-1}, Lt(0U));
  EXPECT_THAT(Json{0U}, Lt(1.0));
  EXPECT_THAT(Json{1.0}, Lt(2));
  EXPECT_THAT(Json{std::numeric_limits<std::uint64_t>::max()}, Gt(std::numeric_limits<std::int64_t>::max()));
}

TEST_F(JsonTest, JsonComparisonsCoverEveryStoredKind) {
  Json array_lhs;
  array_lhs.MakeArray();
  array_lhs.emplace_back(1);
  Json array_rhs;
  array_rhs.MakeArray();
  array_rhs.emplace_back(2);

  Json object_lhs;
  object_lhs["value"] = 1;
  Json object_rhs;
  object_rhs["value"] = 2;
  const Json object_equal = object_lhs;
  Json object_key_lhs;
  object_key_lhs["alpha"] = 1;
  Json object_key_rhs;
  object_key_rhs["omega"] = 1;

  EXPECT_THAT(Json{} <=> Json{}, std::strong_ordering::equal);
  EXPECT_THAT(Json{false}, Lt(Json{true}));
  EXPECT_THAT(Json{-1}, Lt(Json{1U}));
  EXPECT_THAT(Json{1U}, Lt(Json{2.0}));
  EXPECT_THAT(Json{"a"}, Lt(Json{"b"}));
  EXPECT_THAT(array_lhs, Lt(array_rhs));
  EXPECT_THAT(object_lhs, object_equal);
  EXPECT_THAT(object_lhs, Lt(object_rhs));
  EXPECT_THAT(object_key_lhs, Lt(object_key_rhs));
  EXPECT_THAT(Json{}, Lt(array_lhs));
  EXPECT_THAT(array_lhs, Lt(Json{false}));
  EXPECT_THAT(Json{false}, Lt(Json{0}));
  EXPECT_THAT(Json{0}, Lt(object_lhs));
  EXPECT_THAT(object_lhs, Lt(Json{"value"}));
}

TEST_F(JsonTest, ObjectValuePointersOrderNullAndStoredValues) {
  const std::unique_ptr<Json> null;
  const auto one = std::make_unique<Json>(1);
  const auto two = std::make_unique<Json>(2);

  EXPECT_THAT(json_internal::operator<=>(null, null), std::strong_ordering::equal);
  EXPECT_THAT(json_internal::operator<=>(null, one), std::strong_ordering::less);
  EXPECT_THAT(json_internal::operator<=>(one, null), std::strong_ordering::greater);
  EXPECT_THAT(json_internal::operator<=>(one, two), std::strong_ordering::less);
}

TEST_F(JsonTest, CopyOperationsDeepCopyArraysAndObjects) {
  Json original;
  original["array"].emplace_back(1);
  original["nested"]["value"] = "before";

  Json copy(original);
  copy["array"][0] = 2;
  copy["nested"]["value"] = "after";

  EXPECT_THAT(original["array"][0], 1);
  EXPECT_THAT(original["nested"]["value"], "before");
  EXPECT_THAT(copy["array"][0], 2);
  EXPECT_THAT(copy["nested"]["value"], "after");

  Json assigned;
  const Json& original_ref = original;
  assigned = original_ref;
  assigned["array"][0] = 3;
  const Json* same = &assigned;
  assigned = *same;
  EXPECT_THAT(original["array"][0], 1);
  EXPECT_THAT(assigned["array"][0], 3);

  const std::unique_ptr<Json> empty;
  assigned = empty;
  EXPECT_THAT(assigned.IsNull(), true);

  const auto pointer = std::make_unique<Json>("pointed-to");
  assigned = pointer;
  EXPECT_THAT(assigned, "pointed-to");
  EXPECT_THAT(&assigned, Not(pointer.get()));
}

TEST_F(JsonTest, ContainerModifiersAndAccessors) {
  Json array;
  array.MakeArray();
  array.resize(2, 7);
  EXPECT_THAT(array, SizeIs(2));
  EXPECT_THAT(array.at(0), 7);
  EXPECT_THAT(array.at(1), 7);
  const Json& const_array = array;
  EXPECT_THAT(const_array.at(0), 7);
  array.pop_back();
  array.push_back(8);
  array.emplace_back(9);
  EXPECT_THAT(array, ElementsAre(7, 8, 9));
  array.erase(array.begin() + 1);
  EXPECT_THAT(array, ElementsAre(7, 9));
  array.erase(array.begin(), array.end());
  EXPECT_THAT(array, IsEmpty());
  array.resize(2);
  array.clear();
  EXPECT_THAT(array, IsEmpty());

  Json object;
  object.emplace("one", 1);
  object.emplace("two", 2);
  EXPECT_THAT(object.contains("one"), true);
  EXPECT_THAT(object.at("one"), 1);
  const Json& const_object = object;
  EXPECT_THAT(const_object.at("one"), 1);
  EXPECT_DEATH(static_cast<void>(object.at("missing")), "Property not present");
  EXPECT_THAT(object.contains("missing"), false);
  EXPECT_THAT(object, SizeIs(2));
  EXPECT_THAT(object.erase("missing"), 0);
  EXPECT_THAT(object.erase("one"), 1);
  EXPECT_THAT(object.contains("one"), false);
  EXPECT_THAT(object.erase("two"), 1);
  EXPECT_THAT(object, IsEmpty());
  object["again"] = 3;
  object.clear();
  EXPECT_THAT(object, IsEmpty());

  Json scalar{1};
  EXPECT_THAT(static_cast<bool>(scalar), true);
  EXPECT_THAT(scalar.empty(), true);
  EXPECT_THAT(scalar, IsEmpty());
  EXPECT_THAT(scalar.erase("none"), 0);
  scalar.clear();
  EXPECT_THAT(scalar.IsNull(), true);
}

TEST_F(JsonTest, ValueIteratorErase) {
  Json array;
  array.emplace_back(1);
  array.emplace_back(2);
  array.emplace_back(3);
  const Json::const_value_iterator array_pos{array.values().begin()};
  EXPECT_THAT(*array.erase(array_pos), 2);
  const auto array_values = array.values();
  const Json::const_value_iterator array_first{array_values.begin()};
  const Json::const_value_iterator array_last{array_values.end()};
  const auto array_after_erase = array.erase(array_first, array_last);
  EXPECT_THAT(array_after_erase, array.values().end());
  EXPECT_THAT(array, IsEmpty());

  Json object;
  object["one"] = 1;
  object["two"] = 2;
  object["three"] = 3;
  auto values = object.values();
  const Json::const_value_iterator object_pos{values.begin()};
  object.erase(object_pos);
  const auto remaining_values = object.values();
  const Json::const_value_iterator object_first{remaining_values.begin()};
  const Json::const_value_iterator object_last{remaining_values.end()};
  const auto object_after_erase = object.erase(object_first, object_last);
  EXPECT_THAT(object_after_erase, object.values().end());
  EXPECT_THAT(object, IsEmpty());
}

TEST_F(JsonTest, BooleanPredicates) {
  EXPECT_THAT(Json{false}.IsFalse(), true);
  EXPECT_THAT(Json{false}.IsTrue(), false);
  EXPECT_THAT(Json{true}.IsFalse(), false);
  EXPECT_THAT(Json{true}.IsTrue(), true);
  EXPECT_THAT(Json{}.IsFalse(), false);
  EXPECT_THAT(Json{}.IsTrue(), false);
}

TEST_F(JsonTest, BasicsAndSerialize) {
  Json data;
  data["foo"] = "bar";
  data["bar"] = "baz";
  ASSERT_THAT(data["bar"], IsEmpty());
  EXPECT_THAT(
      data.Serialize(),  //
      AnyOf(
          EqualsText(R"({"bar":"baz","foo":"bar"}
)"),
          EqualsText(R"({"foo":"bar","bar":"baz"}
)")));
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "bar": "baz",
  "foo": "bar"
}
)"));
  data["null"] = std::nullopt;
  ASSERT_THAT(data["null"].IsNull(), true);
  ASSERT_THAT(data["null"], IsEmpty());
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "bar": "baz",
  "foo": "bar",
  "null": null
}
)"));
  data["null"].MakeObject();
  ASSERT_THAT(data["null"].IsObject(), true);
  ASSERT_THAT(data["null"], IsEmpty());
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "bar": "baz",
  "foo": "bar",
  "null": {
  }
}
)"));
  data["null"].Reset();
  ASSERT_THAT(data["null"].IsNull(), true);
  ASSERT_THAT(data["null"], IsEmpty());
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "bar": "baz",
  "foo": "bar",
  "null": null
}
)"));
  data["array"].MakeArray();
  ASSERT_THAT(data["array"].IsArray(), true);
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "array": [
  ],
  "bar": "baz",
  "foo": "bar",
  "null": null
}
)"));
  data["array"].emplace_back(25);
  data["array"].emplace_back("42");
  ASSERT_THAT(data["array"], SizeIs(2));
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "array": [
    25,
    "42"
  ],
  "bar": "baz",
  "foo": "bar",
  "null": null
}
)"));
  data["object"].MakeObject();
  ASSERT_THAT(data["object"].IsObject(), true);
  ASSERT_THAT(data["object"], IsEmpty());
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "array": [
    25,
    "42"
  ],
  "bar": "baz",
  "foo": "bar",
  "null": null,
  "object": {
  }
}
)"));
  data["object"]["one"] = 33;
  data["object"]["two"].MakeString("Two");
  ASSERT_THAT(data["object"], SizeIs(2));
  EXPECT_THAT(data.Serialize(Json::SerializeMode::kPretty), EqualsText(R"({
  "array": [
    25,
    "42"
  ],
  "bar": "baz",
  "foo": "bar",
  "null": null,
  "object": {
    "one": 33,
    "two": "Two"
  }
}
)"));
}

TEST_F(JsonTest, ArrayIteration) {
  Json json;
  json.MakeArray();
  json.push_back(0);
  json.push_back("hello");
  json.emplace_back("world");
  json.emplace_back(std::nullopt);
  json.emplace_back(true);
  json.emplace_back(false);
  const std::initializer_list<Json> expected = {Json{0},    Json{"hello"}, Json{"world"}, Json{std::nullopt},
                                                Json{true}, Json{false}};
  const auto elements_are = ElementsAreArray<Json>(expected);
  EXPECT_THAT(TypedView(json.array_values()), elements_are);
  EXPECT_THAT(TypedView(json.array_values()), ElementsAre(0, "hello", "world", std::nullopt, true, false));
  EXPECT_THAT(json, elements_are);
  EXPECT_THAT(std::vector<Json>(json.begin(), json.end()), elements_are);
  const auto values = json.values();
  EXPECT_THAT(values, Not(IsEmpty()));
  EXPECT_THAT(values.begin(), Not(values.end()));
  EXPECT_THAT(json.values(), elements_are);
}

TEST_F(JsonTest, ArrayIteratorOverloads) {
  Json array;
  array.emplace_back(1);
  array.emplace_back(2);
  const Json& const_array = array;

  EXPECT_THAT(array.cend(), array.end());
  EXPECT_THAT(*array.rbegin(), 2);
  EXPECT_THAT(*const_array.rbegin(), 2);
  EXPECT_THAT(*array.crbegin(), 2);
  EXPECT_THAT(array.rend(), array.rbegin() + 2);
  EXPECT_THAT(const_array.rend(), const_array.rbegin() + 2);
  EXPECT_THAT(array.crend(), array.crbegin() + 2);
}

TEST_F(JsonTest, PropertyIteration) {
  Json json;
  json["a"] = 1;
  json["b"] = 2;
  json["c"] = 3;
  EXPECT_THAT(TypedView(json.property_names()), UnorderedElementsAreArray({"a", "b", "c"}));
  EXPECT_THAT(TypedView(json.property_pairs()), UnorderedElementsAreArray({Pair("a", 1), Pair("b", 2), Pair("c", 3)}));
  EXPECT_THAT(TypedView(json.property_values()), UnorderedElementsAreArray({1, 2, 3}));
  // EXPECT_THAT(json.values(), UnorderedElementsAreArray({1, 2, 3}));
}

TEST_F(JsonTest, ValueIteratorAssignment) {
  Json array;
  array.emplace_back(1);
  array.emplace_back(2);
  auto array_values = array.values();

  Json::value_iterator mutable_it;
  mutable_it = array_values.begin();
  EXPECT_THAT(*mutable_it, 1);
  const auto* mutable_it_ptr = &mutable_it;
  mutable_it = *mutable_it_ptr;
  EXPECT_THAT(*mutable_it, 1);

  Json::value_iterator copied_it;
  copied_it = mutable_it;
  EXPECT_THAT(*copied_it, 1);
  Json::value_iterator moved_it;
  moved_it = Json::value_iterator{array_values.begin()};
  EXPECT_THAT(*moved_it, 1);

  Json::const_value_iterator const_it;
  const_it = mutable_it;
  EXPECT_THAT(*const_it, 1);
  Json::const_value_iterator moved_const_it;
  moved_const_it = array.values().begin();
  EXPECT_THAT(*moved_const_it, 1);
  const Json::const_value_iterator copied_const_it{mutable_it};
  EXPECT_THAT(*copied_const_it, 1);
  const Json::const_value_iterator constructed_moved_const_it{Json::value_iterator{array_values.begin()}};
  EXPECT_THAT(*constructed_moved_const_it, 1);

  Json object;
  object["value"] = 3;
  auto object_values = object.values();
  moved_it = object_values.begin();
  EXPECT_THAT(*moved_it, 3);
  EXPECT_THAT(moved_it->IsNumber(), true);
  const Json::value_iterator previous_object = moved_it++;
  EXPECT_THAT(*previous_object, 3);
  EXPECT_THAT(moved_it, object_values.end());
  const Json::const_value_iterator copied_object_const_it{previous_object};
  EXPECT_THAT(*copied_object_const_it, 3);
  const Json::const_value_iterator constructed_moved_object_const_it{Json::value_iterator{object_values.begin()}};
  EXPECT_THAT(*constructed_moved_object_const_it, 3);
  const_it = moved_it;
  EXPECT_THAT(const_it, object_values.end());

  auto previous_array = mutable_it++;
  EXPECT_THAT(*previous_array, 1);
  EXPECT_THAT(*mutable_it, 2);
}

}  // namespace
}  // namespace mbo::json

// NOLINTEND(*-magic-*,*-avoid-unchecked-container-access)
