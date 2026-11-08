// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/hamt/hamt_node_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_interner_policy.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/strings/string_interner.h"

namespace mbo::container::experimental::hamt {
namespace {
using ::testing::Eq;
using ::testing::Optional;
using ::testing::SizeIs;

using Id = mbo::strings::StringIdU32<>;
using Hash = std::hash<std::string_view>;
using SafePolicy = mbo::strings::StringInternerPolicyHolder<
    mbo::strings::StringInternerErrorStlPolicy,
    mbo::strings::StringInternerLookupMboPolicy,
    HamtStringInternerIndexPolicy,
    mbo::strings::StringInternerExceptionSafePolicy>;

struct Mapped final {
  explicit Mapped(int input) : value(input) {
    if (input < 0) {
      throw std::runtime_error("mapped construction failed");
    }
  }

  Mapped(const Mapped&) = delete;
  Mapped& operator=(const Mapped&) = delete;
  Mapped(Mapped&&) = delete;
  Mapped& operator=(Mapped&&) = delete;
  ~Mapped() = default;
  int value;
};

template<typename Index>
struct HamtStringInternerExceptionsTest : ::testing::Test {
  using Interner = mbo::strings::StringInternerMap<
      Id,
      Mapped,
      Hash,
      Index,
      mbo::strings::ArenaStringStorage<>,
      mbo::container::SegmentedVector<std::optional<std::pair<const std::string_view, Mapped>>>,
      SafePolicy>;
};

using IndexTypes = ::testing::Types<HamtStringIndex<>, HamtNodeStringIndex<>>;

struct IndexNames final {
  template<typename T>
  static const char* GetName(int /*index*/) {
    if constexpr (std::same_as<T, HamtStringIndex<>>) {
      return "Flat";
    } else {
      return "Node";
    }
  }
};

TYPED_TEST_SUITE(HamtStringInternerExceptionsTest, IndexTypes, IndexNames);

TYPED_TEST(HamtStringInternerExceptionsTest, ThrowingMappedConstructionErasesOnlyTheUnpublishedIndexEntry) {
  typename TestFixture::Interner interner;
  ASSERT_THAT(interner.try_emplace("first", 7).has_value(), Eq(true));
  auto* const first = std::addressof(interner.at(Id(0)));
  EXPECT_THROW(static_cast<void>(interner.try_emplace("failed", -1)), std::runtime_error);
  EXPECT_THAT(interner, SizeIs(1));
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  EXPECT_THAT(interner.find("first"), Optional(Eq(Id(0))));
  const auto retry = interner.try_emplace("failed", 9);
  ASSERT_THAT(retry.has_value(), Eq(true));
  EXPECT_THAT(retry->first, Eq(Id(1)));
  EXPECT_THAT(interner.at(Id(1)).value, Eq(9));
  EXPECT_THAT(std::addressof(interner.at(Id(0))), Eq(first));
  EXPECT_THAT(first->value, Eq(7));
}

}  // namespace
}  // namespace mbo::container::experimental::hamt
