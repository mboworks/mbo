// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/hamt/hamt_node_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_interner_policy.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/block_source.h"
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

struct AllocationBudget final {
  std::size_t remaining = 1'024;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

// NOLINTBEGIN(readability-identifier-naming): BlockSource vocabulary.
struct BudgetSource final {
  static constexpr bool supports_recoverable_failure = true;

  explicit BudgetSource(AllocationBudget& budget) noexcept : budget(&budget) {}

  static std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const noexcept {
    if (budget->remaining == 0) {
      return std::nullopt;
    }
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      --budget->remaining;
      ++budget->acquired;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) const noexcept {
    ++budget->released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  AllocationBudget* budget;
};

// NOLINTEND(readability-identifier-naming)

struct ByteHash final {
  static std::uint64_t operator()(std::string_view key) noexcept { return static_cast<unsigned char>(key.front()); }
};

template<typename Index>
struct HamtStringInternerExceptionsTest : ::testing::Test {
  static constexpr bool kNodeIndex = std::same_as<Index, HamtNodeStringIndex<>>;
  using BudgetIndex = std::conditional_t<
      kNodeIndex,
      HamtNodeStringIndex<Id, ByteHash, std::equal_to<>, {}, BudgetSource>,
      HamtStringIndex<Id, ByteHash, std::equal_to<>, {}, BudgetSource>>;
  using BudgetInterner = mbo::strings::StringInternerMap<
      Id,
      Mapped,
      ByteHash,
      BudgetIndex,
      mbo::strings::ArenaStringStorage<>,
      mbo::container::SegmentedVector<std::optional<std::pair<const std::string_view, Mapped>>>,
      SafePolicy>;

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

TYPED_TEST(HamtStringInternerExceptionsTest, ThrowingMappedConstructionDiscardsOnlyTheUnpublishedIndexEntry) {
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

TYPED_TEST(HamtStringInternerExceptionsTest, ThrowingPublicationRollsBackAfterTheLastAvailableAllocation) {
  AllocationBudget budget;
  auto index = TestFixture::BudgetIndex::try_create(ByteHash{}, std::equal_to<>{}, budget);
  if (!index) {
    FAIL() << "source-domain creation failed";
    return;
  }
  typename TestFixture::BudgetInterner interner(
      typename TestFixture::BudgetInterner::options_type{.container = std::move(*index)});
  ASSERT_THAT(interner.try_emplace("a", 7).has_value(), Eq(true));
  ASSERT_THAT(interner.try_emplace("b", 8).has_value(), Eq(true));
  auto* const first = std::addressof(interner.at(Id(0)));
  const auto live = budget.acquired - budget.released;
  budget.remaining = TestFixture::kNodeIndex ? 2 : 1;

  EXPECT_THROW(static_cast<void>(interner.try_emplace("c", -1)), std::runtime_error);
  EXPECT_THAT(budget.remaining, Eq(0));
  EXPECT_THAT(budget.acquired - budget.released, Eq(live));
  EXPECT_THAT(interner, SizeIs(2));
  EXPECT_THAT(interner.find("c"), Eq(std::nullopt));
  EXPECT_THAT(interner.find("a"), Optional(Eq(Id(0))));
  EXPECT_THAT(interner.find("b"), Optional(Eq(Id(1))));
  EXPECT_THAT(interner.at(Id(0)).value, Eq(7));
  EXPECT_THAT(interner.at(Id(1)).value, Eq(8));
  EXPECT_THAT(std::addressof(interner.at(Id(0))), Eq(first));

  const auto exhausted = interner.try_emplace("c", 9);
  ASSERT_THAT(exhausted.has_value(), Eq(false));
  EXPECT_THAT(exhausted.error(), Eq(mbo::strings::StringInternerError::kIndexExhausted));
  budget.remaining = TestFixture::kNodeIndex ? 2 : 1;
  const auto retried = interner.try_emplace("c", 9);
  ASSERT_THAT(retried.has_value(), Eq(true));
  EXPECT_THAT(retried->first, Eq(Id(2)));
  EXPECT_THAT(interner.at(Id(2)).value, Eq(9));
}

TYPED_TEST(HamtStringInternerExceptionsTest, FailedIndexInsertionReleasesTemporariesBeforePublication) {
  AllocationBudget budget;
  auto index = TestFixture::BudgetIndex::try_create(ByteHash{}, std::equal_to<>{}, budget);
  if (!index) {
    FAIL() << "source-domain creation failed";
    return;
  }
  typename TestFixture::BudgetInterner interner(
      typename TestFixture::BudgetInterner::options_type{.container = std::move(*index)});
  ASSERT_THAT(interner.try_emplace("a", 7).has_value(), Eq(true));
  ASSERT_THAT(interner.try_emplace("b", 8).has_value(), Eq(true));
  const auto live = budget.acquired - budget.released;
  budget.remaining = TestFixture::kNodeIndex ? 1 : 0;

  // The throwing constructor must never run when index preparation fails.
  const auto exhausted = interner.try_emplace("c", -1);
  ASSERT_THAT(exhausted.has_value(), Eq(false));
  EXPECT_THAT(exhausted.error(), Eq(mbo::strings::StringInternerError::kIndexExhausted));
  EXPECT_THAT(budget.acquired - budget.released, Eq(live));
  EXPECT_THAT(interner, SizeIs(2));
  EXPECT_THAT(interner.find("c"), Eq(std::nullopt));

  budget.remaining = TestFixture::kNodeIndex ? 2 : 1;
  const auto retried = interner.try_emplace("c", 9);
  ASSERT_THAT(retried.has_value(), Eq(true));
  EXPECT_THAT(retried->first, Eq(Id(2)));
}

TYPED_TEST(HamtStringInternerExceptionsTest, TransactionalInsertionCopiesOnlyItsBranch) {
  AllocationBudget budget;
  auto index = TestFixture::BudgetIndex::try_create(ByteHash{}, std::equal_to<>{}, budget);
  if (!index) {
    FAIL() << "source-domain creation failed";
    return;
  }
  typename TestFixture::BudgetInterner interner(
      typename TestFixture::BudgetInterner::options_type{.container = std::move(*index)});
  for (int value = 32; value < 96; ++value) {
    ASSERT_THAT(interner.try_emplace(std::string(1, static_cast<char>(value)), value).has_value(), Eq(true));
  }
  // The tree now has 33 nodes. Two path copies (plus one node-map payload)
  // suffice; a whole-tree mutable-iterator preparation would exhaust this budget.
  budget.remaining = TestFixture::kNodeIndex ? 3 : 2;
  const auto inserted = interner.try_emplace(std::string(1, static_cast<char>(96)), 96);
  ASSERT_THAT(inserted.has_value(), Eq(true));
  EXPECT_THAT(inserted->first, Eq(Id(64)));
  EXPECT_THAT(budget.remaining, Eq(0));
  EXPECT_THAT(interner, SizeIs(65));
  EXPECT_THAT(interner.at(Id(0)).value, Eq(32));
  EXPECT_THAT(interner.at(Id(64)).value, Eq(96));
}

}  // namespace
}  // namespace mbo::container::experimental::hamt
