// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_source_domain.h"

#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

struct HamtSourceDomainTest : ::testing::Test {};

struct ObservedSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  explicit ObservedSource(int& destruction_count) noexcept : destructions(destruction_count) {}

  ObservedSource(const ObservedSource&) = delete;
  ObservedSource& operator=(const ObservedSource&) = delete;
  ObservedSource(ObservedSource&&) = delete;
  ObservedSource& operator=(ObservedSource&&) = delete;

  ~ObservedSource() { ++destructions; }

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

  int& destructions;
};

static_assert(std::is_nothrow_copy_constructible_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_move_constructible_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_copy_assignable_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_move_assignable_v<HamtSourceDomain<ObservedSource>>);

TEST_F(HamtSourceDomainTest, SourceIsDestroyedExactlyOnceAfterItsLastHandle) {
  using Domain = HamtSourceDomain<ObservedSource>;
  int destructions = 0;
  {
    auto first = Domain::TryCreate(destructions).value_or(Domain{});
    ASSERT_THAT(first.get(), NotNull());
    Domain last = first;
    first = Domain{};
    EXPECT_THAT(destructions, Eq(0));
    Domain replacement;
    replacement = std::move(last);
    EXPECT_THAT(last.get(), IsNull());
    EXPECT_THAT(destructions, Eq(0));
  }
  EXPECT_THAT(destructions, Eq(1));
}

TEST_F(HamtSourceDomainTest, CopiesAndMovesKeepNonmovableSourceAtTheSameAddress) {
  using Domain = HamtSourceDomain<mbo::memory::InlineBlockSource<128>>;
  auto first = Domain::TryCreate().value_or(Domain{});
  ASSERT_THAT(first.get(), NotNull());
  auto* const source = first.get();
  Domain copy = first;
  Domain moved = std::move(first);
  EXPECT_THAT(first.get(), IsNull());
  EXPECT_THAT(copy.get(), Eq(source));
  EXPECT_THAT(moved.get(), Eq(source));
  moved = Domain{};
  EXPECT_THAT(moved.get(), IsNull());
  const auto block = copy.get()->TryAcquire(16, 1);
  ASSERT_THAT(block.has_value(), Eq(true));
  copy.get()->Release(block.value());
}

TEST_F(HamtSourceDomainTest, EmptyHandlesAndSelfAssignmentRemainValid) {
  using Domain = HamtSourceDomain<mbo::memory::NewDeleteBlockSource>;
  Domain empty;
  Domain copied = empty;
  EXPECT_THAT(copied.get(), IsNull());
  auto domain = Domain::TryCreate().value_or(Domain{});
  ASSERT_THAT(domain.get(), NotNull());
  auto* const source = domain.get();
  const auto& alias = domain;
  domain = alias;
  EXPECT_THAT(domain.get(), Eq(source));
  auto& move_alias = domain;
  domain = std::move(move_alias);
  EXPECT_THAT(domain.get(), Eq(source));
  swap(domain, empty);
  EXPECT_THAT(domain.get(), IsNull());
  EXPECT_THAT(empty.get(), Eq(source));
}

}  // namespace
}  // namespace mbo::container::container_internal
