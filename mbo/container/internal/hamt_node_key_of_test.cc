// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_key_of.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_options.h"
#include "mbo/container/internal/hamt_key_of.h"
#include "mbo/container/internal/hamt_node_value.h"
#include "mbo/container/internal/hamt_source_domain.h"
#include "mbo/container/internal/hamt_tree.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::NotNull;

template<typename Value>
struct BorrowedHandle final {
  const Value* value;

  // NOLINTNEXTLINE(readability-identifier-naming): handle vocabulary.
  constexpr const Value* get() const noexcept { return value; }
};

struct ThrowingKeyOf final {
  const int& operator()(const int& key) const noexcept(false) { return key; }
};

using SetKeyOf = HamtNodeKeyOf<HamtIdentityKey<int>>;
using MapKeyOf = HamtNodeKeyOf<HamtPairKey<int, int>>;
static_assert(std::is_nothrow_invocable_v<const SetKeyOf&, const BorrowedHandle<int>&>);
static_assert(!std::is_invocable_v<const HamtNodeKeyOf<ThrowingKeyOf>&, const BorrowedHandle<int>&>);

constexpr bool PreservesConstexprIdentity() {
  const int key = 42;
  const BorrowedHandle<int> handle{.value = &key};
  return std::addressof(SetKeyOf{}(handle)) == &key;
}

static_assert(PreservesConstexprIdentity());

struct HamtNodeKeyOfTest : ::testing::Test {};

TEST_F(HamtNodeKeyOfTest, SetProjectionBorrowsTheOriginalKey) {
  const int key = 42;
  const BorrowedHandle<int> handle{.value = &key};
  EXPECT_THAT(SetKeyOf{}(handle), Eq(42));
  EXPECT_THAT(std::addressof(SetKeyOf{}(handle)), Eq(&key));
}

TEST_F(HamtNodeKeyOfTest, MapProjectionBorrowsTheConstKeyRatherThanTheMappedValue) {
  const std::pair<const int, int> entry(42, 99);
  const BorrowedHandle<std::pair<const int, int>> handle{.value = &entry};
  EXPECT_THAT(MapKeyOf{}(handle), Eq(42));
  EXPECT_THAT(std::addressof(MapKeyOf{}(handle)), Eq(&entry.first));
}

TEST_F(HamtNodeKeyOfTest, NodeSetStorageUsesTheSharedCoreAndKeepsPayloadAddressesAcrossMutation) {
  using Source = mbo::memory::NewDeleteBlockSource;
  using Domain = HamtSourceDomain<Source>;
  using Payload = HamtNodeValue<int, Source>;
  using Core = HamtTree<HamtOptions{}, Payload, std::hash<int>, SetKeyOf, std::equal_to<>, Source>;
  auto domain_result = Domain::TryCreate();
  ASSERT_THAT(domain_result.has_value(), Eq(true));
  const auto domain = domain_result.value_or(Domain{});
  ASSERT_THAT(domain.get(), NotNull());
  Core tree{*domain.get(), std::hash<int>{}, SetKeyOf{}, std::equal_to<>{}};
  auto first_result = Payload::TryCreate(domain, 42);
  ASSERT_THAT(first_result.has_value(), Eq(true));
  const Payload first = first_result.value_or(Payload{});
  EXPECT_THAT(tree.try_insert(first).changed, Eq(true));
  const Core snapshot = tree;
  auto second_result = Payload::TryCreate(domain, 99);
  ASSERT_THAT(second_result.has_value(), Eq(true));
  EXPECT_THAT(tree.try_insert(second_result.value_or(Payload{})).changed, Eq(true));
  const auto* const current = tree.Find(42);
  const auto* const previous = snapshot.Find(42);
  ASSERT_THAT(current, NotNull());
  ASSERT_THAT(previous, NotNull());
  EXPECT_THAT(current->get(), Eq(first.get()));
  EXPECT_THAT(previous->get(), Eq(first.get()));
  EXPECT_THAT(tree.try_erase(42).changed, Eq(true));
  EXPECT_THAT(tree.contains(42), Eq(false));
  EXPECT_THAT(snapshot.contains(42), Eq(true));
}

}  // namespace
}  // namespace mbo::container::container_internal
