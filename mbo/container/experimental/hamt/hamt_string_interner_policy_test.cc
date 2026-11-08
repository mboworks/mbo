// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/experimental/hamt/hamt_string_interner_policy.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/hamt/hamt_node_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/hash/hash.h"
#include "mbo/strings/string_interner.h"

namespace mbo::container::experimental::hamt {
namespace {
using ::testing::Eq;
using ::testing::Optional;
using ::testing::SizeIs;

using Id = mbo::strings::StringIdU32<>;
using Hash = mbo::hash::DefaultHasher;
using Policy = mbo::strings::StringInternerPolicyHolder<
    mbo::strings::StringInternerErrorStlPolicy,
    mbo::strings::StringInternerLookupMboPolicy,
    HamtStringInternerIndexPolicy>;

template<typename Index>
struct HamtStringInternerPolicyTest : ::testing::Test {
  using Interner = mbo::strings::StringInternerSet<
      Id,
      Hash,
      Index,
      mbo::strings::ArenaStringStorage<>,
      mbo::container::SegmentedVector<std::string_view>,
      Policy>;
};

using FlatIndex = HamtStringIndex<Id, Hash, std::equal_to<>, HamtOptions{.fragment_bits = 6}>;
using IndexTypes = ::testing::Types<FlatIndex, HamtNodeStringIndex<Id, Hash>>;

struct IndexNames final {
  template<typename T>
  static const char* GetName(int /*index*/) {
    if constexpr (std::same_as<T, FlatIndex>) {
      return "Flat";
    } else {
      return "Node";
    }
  }
};

TYPED_TEST_SUITE(HamtStringInternerPolicyTest, IndexTypes, IndexNames);

TYPED_TEST(HamtStringInternerPolicyTest, GenericInternerOwnsBytesAndRetainsDenseStableViews) {
  typename TestFixture::Interner interner;
  std::string key = "first";
  const auto first = interner.insert(key);
  key.assign("other");
  ASSERT_THAT(first.has_value(), Eq(true));
  EXPECT_THAT(first->first, Eq(Id(0)));
  EXPECT_THAT(first->second, Eq(true));
  const auto view = interner.get_string(Id(0));
  ASSERT_THAT(view.has_value(), Eq(true));
  const auto* address = view.value_or(std::string_view{}).data();
  for (std::uint32_t index = 1; index < 256; ++index) {
    ASSERT_THAT(interner.insert(std::to_string(index)).has_value(), Eq(true));
  }
  EXPECT_THAT(interner.insert("first")->second, Eq(false));
  EXPECT_THAT(interner.get_string(Id(0)), Optional(Eq("first")));
  EXPECT_THAT(interner.get_string(Id(0)).value_or(std::string_view{}).data(), Eq(address));
  EXPECT_THAT(interner, SizeIs(256));
  EXPECT_THAT(interner.find("255"), Optional(Eq(Id(255))));
}

TYPED_TEST(HamtStringInternerPolicyTest, ParentVisibilityUsesTheGenericCapturedPrefix) {
  typename TestFixture::Interner root;
  ASSERT_THAT(root.insert("root").has_value(), Eq(true));
  typename TestFixture::Interner child(&root);
  ASSERT_THAT(root.insert("later").has_value(), Eq(true));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  ASSERT_THAT(child.insert("child").has_value(), Eq(true));
  EXPECT_THAT(child.find("root"), Optional(Eq(Id(0))));
  EXPECT_THAT(child.rfind("child"), Optional(Eq(Id(1))));
  EXPECT_THAT(child.get_string(Id(1)), Optional(Eq("child")));
}

TYPED_TEST(HamtStringInternerPolicyTest, GenericMapKeepsMoveOnlyMappedValuesStable) {
  using Interner = mbo::strings::StringInternerMap<
      Id, std::unique_ptr<int>, Hash, TypeParam, mbo::strings::ArenaStringStorage<>,
      mbo::container::SegmentedVector<std::optional<std::pair<const std::string_view, std::unique_ptr<int>>>>, Policy>;
  Interner interner;
  ASSERT_THAT(interner.try_emplace("first", std::make_unique<int>(7)).has_value(), Eq(true));
  auto* const address = std::addressof(interner.at(Id(0)));
  ASSERT_THAT(interner.try_emplace("second", std::make_unique<int>(9)).has_value(), Eq(true));
  EXPECT_THAT(std::addressof(interner.at(Id(0))), Eq(address));
  EXPECT_THAT(*interner.at(Id(0)), Eq(7));
  EXPECT_THAT(interner.try_emplace("first", std::make_unique<int>(10))->second, Eq(false));
  EXPECT_THAT(*interner.at(Id(0)), Eq(7));
}

struct HamtStringInternerFailureTest : ::testing::Test {};

TEST_F(HamtStringInternerFailureTest, IndexExhaustionKeepsIdsDenseAndPriorEntriesVisible) {
  using Index = HamtStringIndex<Id, Hash, std::equal_to<>, {.maximum_size = 1}>;
  using Interner = HamtStringInternerPolicyTest<Index>::Interner;
  Interner interner;
  ASSERT_THAT(interner.insert("first").has_value(), Eq(true));
  const auto failed = interner.insert("second");
  ASSERT_THAT(failed.has_value(), Eq(false));
  EXPECT_THAT(failed.error(), Eq(mbo::strings::StringInternerError::kIndexExhausted));
  EXPECT_THAT(interner, SizeIs(1));
  EXPECT_THAT(interner.get_string(Id(0)), Optional(Eq("first")));
  EXPECT_THAT(interner.find("second"), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("first")->second, Eq(false));
}

TEST_F(HamtStringInternerFailureTest, CallerOwnedNodeExhaustionPropagatesThroughGenericInterner) {
  using Index = HamtNodeStringIndex<Id, Hash, std::equal_to<>, {}, mbo::memory::InlineBlockSource<1>>;
  using Interner = HamtStringInternerPolicyTest<Index>::Interner;
  mbo::memory::InlineBlockSource<4'096> control;
  {
    auto index = Index::try_create_in(control, Hash{}, std::equal_to<>{});
    if (!index) {
      FAIL() << "control-storage domain creation failed";
      return;
    }
    Interner interner(Interner::options_type{.container = std::move(*index)});
    const auto failed = interner.insert("uncommitted");
    ASSERT_THAT(failed.has_value(), Eq(false));
    EXPECT_THAT(failed.error(), Eq(mbo::strings::StringInternerError::kIndexExhausted));
    EXPECT_THAT(interner, SizeIs(0));
    EXPECT_THAT(interner.find("uncommitted"), Eq(std::nullopt));
    EXPECT_THAT(Index::try_create_in(control, Hash{}, std::equal_to<>{}).has_value(), Eq(false));
  }
  EXPECT_THAT(Index::try_create_in(control, Hash{}, std::equal_to<>{}).has_value(), Eq(true));
}

}  // namespace
}  // namespace mbo::container::experimental::hamt
