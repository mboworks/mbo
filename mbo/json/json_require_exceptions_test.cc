// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/json/json.h"

namespace mbo::json {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::ThrowsMessage;

static_assert(config::kRequireThrows);

struct JsonRequireExceptionsTest : ::testing::Test {};

static_assert(!noexcept(std::declval<Json&>().pop_back_value()));

TEST_F(JsonRequireExceptionsTest, ValuePopRequirementsPreserveTheOriginalKind) {
  Json empty;
  empty.MakeArray();
  EXPECT_THAT(
      [&empty] { static_cast<void>(empty.pop_back_value()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Cannot pop an empty Json array")));
  EXPECT_THAT(empty, IsEmpty());
  EXPECT_THAT(empty.IsArray(), IsTrue());
  Json scalar{1};
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.pop_back_value()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array")));
  EXPECT_THAT(scalar, 1);
  Json null;
  EXPECT_THAT(
      [&null] { static_cast<void>(null.pop_back_value()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array")));
  EXPECT_THAT(null.IsNull(), IsTrue());
}

TEST_F(JsonRequireExceptionsTest, ReportsInvalidSerializationAndContainerMutation) {
  Json scalar{1};
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.Serialize()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Only Objects can be serialized.")));
  EXPECT_THAT(
      [&scalar] { scalar.pop_back(); }, ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array or Null.")));
  EXPECT_THAT(
      [] {
        Json array;
        static_cast<void>(array[0]);  // NOLINT(*-avoid-unchecked-container-access)
      },
      ThrowsMessage<std::runtime_error>(HasSubstr("Out of range.")));

  const Json const_scalar{1};
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar[0]); },  // NOLINT(*-avoid-unchecked-container-access)
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  Json array;
  array.MakeArray();
  const Json& const_array = array;
  EXPECT_THAT(
      [&const_array] { static_cast<void>(const_array[0]); },  // NOLINT(*-avoid-unchecked-container-access)
      ThrowsMessage<std::runtime_error>(HasSubstr("Out of range.")));

  const Json::const_iterator iterator;
  EXPECT_THAT(
      [&] { static_cast<void>(scalar.erase(iterator)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&] { static_cast<void>(scalar.erase(iterator, iterator)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT([&scalar] { scalar.resize(1); }, ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT([&scalar] { scalar.resize(1, 2); }, ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
}

TEST_F(JsonRequireExceptionsTest, ReportsInvalidIteratorsAndViews) {
  Json scalar{1};
  const Json& const_scalar = scalar;
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.begin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.begin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.cbegin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.end()); }, ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.end()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.cend()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.rbegin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.rbegin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.crbegin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.rend()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.rend()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.crend()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));

  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is neither Array nor Object.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is neither Array nor Object.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.array_values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.array_values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.property_names()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.property_pairs()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.property_pairs()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.property_values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar.property_values()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
}

TEST_F(JsonRequireExceptionsTest, ReportsInvalidObjectAccessAndTypeChanges) {
  Json scalar{1};
  const Json& const_scalar = scalar;
  EXPECT_THAT(
      [&const_scalar] { static_cast<void>(const_scalar["missing"]); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.at("missing")); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object.")));

  Json object;
  object.MakeObject();
  const Json& const_object = object;
  EXPECT_THAT(
      [&const_object] { static_cast<void>(const_object["missing"]); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Property not present:")));
  EXPECT_THAT(
      [&object] { static_cast<void>(object.at("missing")); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Property not present:")));

  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.MakeArray()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Array or Null.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.MakeObject()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an Object or Null.")));
  EXPECT_THAT(
      [&scalar] { static_cast<void>(scalar.MakeString("value")); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Is not an std::string or Null.")));
}

}  // namespace
}  // namespace mbo::json
