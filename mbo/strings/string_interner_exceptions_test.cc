// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {

using ::testing::Eq;
using ::testing::SizeIs;
using ::testing::ThrowsMessage;

struct StringInternerExceptionsTest : ::testing::Test {};

struct ThrowingValue final {
  explicit ThrowingValue(int value) : value(value) {
    if (value < 0) {
      throw std::runtime_error("value construction failed");
    }
  }

  int value;
};

template<typename Map>
void ExpectConstructionFailurePreservesPublishedEntries() {
  using Inserted = std::pair<std::uint32_t, bool>;
  Map values;
  ASSERT_THAT(values.try_emplace("first", 10), Eq(Inserted{0, true}));
  EXPECT_THAT(
      [&] { static_cast<void>(values.try_emplace("failed", -1)); },
      ThrowsMessage<std::runtime_error>("value construction failed"));
  EXPECT_THAT(values, SizeIs(1));
  EXPECT_THAT(values.find("failed"), Eq(std::nullopt));
  EXPECT_THAT(values.get(1).has_value(), Eq(false));
  EXPECT_THAT(values.at(0).value, Eq(10));
  ASSERT_THAT(values.try_emplace("retry", 20), Eq(Inserted{1, true}));
  EXPECT_THAT(values.find("failed"), Eq(std::nullopt));
  EXPECT_THAT(values.at(1).value, Eq(20));
  ASSERT_THAT(values.try_emplace("failed", 30), Eq(Inserted{2, true}));
  EXPECT_THAT(values.at(2).value, Eq(30));
  EXPECT_THAT(values.try_emplace("failed", -1), Eq(Inserted{2, false}));
}

using ExceptionSafePolicy = StringInternerPolicyHolder<
    StringInternerErrorStlPolicy,
    StringInternerLookupMboPolicy,
    StringInternerIndexStlPolicy,
    StringInternerExceptionSafePolicy>;
using FlatIndex = absl::flat_hash_map<std::string_view, std::uint32_t>;
using FlatMap = StringInternerMap<
    std::uint32_t,
    ThrowingValue,
    absl::DefaultHashContainerHash<std::string_view>,
    FlatIndex,
    ArenaStringStorage<>,
    strings_internal::StringInternerEntriesFor<std::pair<const std::string_view, ThrowingValue>>,
    ExceptionSafePolicy>;

TEST_F(StringInternerExceptionsTest, ViewKeyIndexDiscardsUnpublishedEntryWhenValueConstructionThrows) {
  ExpectConstructionFailurePreservesPublishedEntries<FlatMap>();
}

TEST_F(StringInternerExceptionsTest, OwningKeyIndexDiscardsUnpublishedEntryWhenValueConstructionThrows) {
  using Index = std::map<std::string, std::uint32_t, std::less<>>;
  using OwningMap = StringInternerMap<
      std::uint32_t, ThrowingValue, void, Index, void,
      strings_internal::StringInternerEntriesFor<strings_internal::StringInternerMapElement<Index, ThrowingValue>>,
      ExceptionSafePolicy>;
  ExpectConstructionFailurePreservesPublishedEntries<OwningMap>();
}

}  // namespace
}  // namespace mbo::strings
