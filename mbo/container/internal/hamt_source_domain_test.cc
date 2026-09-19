// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_source_domain.h"

#include <array>
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

struct InvalidControlSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t, std::size_t) noexcept { return returned; }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    released = block;
    ++releases;
  }

  mbo::memory::MemoryBlock returned;
  mbo::memory::MemoryBlock released;
  int releases = 0;
};

TEST_F(HamtSourceDomainTest, InvalidControlBlocksAreReturnedBeforeConstruction) {
  alignas(std::max_align_t) std::array<std::byte, 512> bytes;
  InvalidControlSource storage;
  storage.returned = {.data = bytes.data(), .size = 1, .alignment = alignof(std::max_align_t)};
  EXPECT_THAT(HamtSourceDomain<mbo::memory::NewDeleteBlockSource>::TryCreateIn(storage).has_value(), Eq(false));
  EXPECT_THAT(storage.released, Eq(storage.returned));
  storage.returned = {.data = nullptr, .size = bytes.size(), .alignment = alignof(std::max_align_t)};
  EXPECT_THAT(HamtSourceDomain<mbo::memory::NewDeleteBlockSource>::TryCreateIn(storage).has_value(), Eq(false));
  storage.returned = {.data = bytes.data(), .size = bytes.size(), .alignment = 1};
  EXPECT_THAT(HamtSourceDomain<mbo::memory::NewDeleteBlockSource>::TryCreateIn(storage).has_value(), Eq(false));
  storage.returned = {.data = bytes.data() + 1, .size = bytes.size() - 1, .alignment = alignof(std::max_align_t)};
  EXPECT_THAT(HamtSourceDomain<mbo::memory::NewDeleteBlockSource>::TryCreateIn(storage).has_value(), Eq(false));
  EXPECT_THAT(storage.released, Eq(storage.returned));
  EXPECT_THAT(storage.releases, Eq(4));
}

TEST_F(HamtSourceDomainTest, CallerOwnedControlStorageIsReleasedOnlyByLastSnapshot) {
  mbo::memory::InlineBlockSource<4'096> storage;
  using Domain = HamtSourceDomain<mbo::memory::InlineBlockSource<1>>;
  auto first = Domain::TryCreateIn(storage);
  ASSERT_THAT(first.has_value(), Eq(true));
  auto snapshot = first;
  first.reset();
  EXPECT_THAT(Domain::TryCreateIn(storage).has_value(), Eq(false));
  snapshot.reset();
  EXPECT_THAT(Domain::TryCreateIn(storage).has_value(), Eq(true));
}

struct ObservedSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  explicit ObservedSource(int& destruction_count) noexcept : destructions(destruction_count) {}

  ObservedSource(int& destruction_count, int& construction_count) noexcept : destructions(destruction_count) {
    ++construction_count;
  }

  ObservedSource(const ObservedSource&) = delete;
  ObservedSource& operator=(const ObservedSource&) = delete;
  ObservedSource(ObservedSource&&) = delete;
  ObservedSource& operator=(ObservedSource&&) = delete;

  ~ObservedSource() { ++destructions; }

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  static std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

  int& destructions;
};

static_assert(std::is_nothrow_copy_constructible_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_move_constructible_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_copy_assignable_v<HamtSourceDomain<ObservedSource>>);
static_assert(std::is_nothrow_move_assignable_v<HamtSourceDomain<ObservedSource>>);

TEST_F(HamtSourceDomainTest, ExhaustedControlStorageDoesNotConstructOrDestroyNodeSource) {
  mbo::memory::InlineBlockSource<1> storage;
  int constructions = 0;
  int destructions = 0;
  EXPECT_THAT(
      HamtSourceDomain<ObservedSource>::TryCreateIn(storage, destructions, constructions).has_value(), Eq(false));
  EXPECT_THAT(constructions, Eq(0));
  EXPECT_THAT(destructions, Eq(0));
}

struct ObservedControlStorage final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    destructions_at_release = destructions;
    ++releases;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  int& destructions;
  int destructions_at_release = 0;
  int releases = 0;
};

TEST_F(HamtSourceDomainTest, LastHandleDestroysNodeSourceBeforeReleasingControlStorage) {
  int destructions = 0;
  ObservedControlStorage storage{.destructions = destructions};
  auto first = HamtSourceDomain<ObservedSource>::TryCreateIn(storage, destructions);
  ASSERT_THAT(first.has_value(), Eq(true));
  auto last = first;
  first.reset();
  EXPECT_THAT(storage.releases, Eq(0));
  EXPECT_THAT(destructions, Eq(0));
  last.reset();
  EXPECT_THAT(storage.releases, Eq(1));
  EXPECT_THAT(storage.destructions_at_release, Eq(1));
  EXPECT_THAT(destructions, Eq(1));
}

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
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from handle contract under test.
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
  const Domain copy = first;
  Domain moved = std::move(first);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from handle contract under test.
  EXPECT_THAT(first.get(), IsNull());
  EXPECT_THAT(copy.get(), Eq(source));
  EXPECT_THAT(moved.get(), Eq(source));
  moved = Domain{};
  EXPECT_THAT(moved.get(), IsNull());
  const auto block = copy.get()->TryAcquire(16, 1);
  ASSERT_THAT(block.has_value(), Eq(true));
  copy.get()->Release(block.value_or(mbo::memory::MemoryBlock{}));
}

TEST_F(HamtSourceDomainTest, EmptyHandlesAndSelfAssignmentRemainValid) {
  using Domain = HamtSourceDomain<mbo::memory::NewDeleteBlockSource>;
  Domain empty;
  const Domain copied = empty;
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
