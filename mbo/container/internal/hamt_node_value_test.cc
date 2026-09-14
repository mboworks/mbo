// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_value.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_source_domain.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::NotNull;
using Source = mbo::memory::InlineBlockSource<512>;
using Domain = HamtSourceDomain<Source>;
using Payload = HamtNodeValue<int, Source>;

struct HamtNodeValueTest : ::testing::Test {};

struct NonMovableValue final {
  explicit NonMovableValue(int& destructions) noexcept : destructions(&destructions) {}

  NonMovableValue(const NonMovableValue&) = delete;
  NonMovableValue& operator=(const NonMovableValue&) = delete;
  NonMovableValue(NonMovableValue&&) = delete;
  NonMovableValue& operator=(NonMovableValue&&) = delete;

  ~NonMovableValue() { ++*destructions; }

  int* destructions;
};

TEST_F(HamtNodeValueTest, NonMovableValuesAreConstructedInPlaceAndDestroyedExactlyOnce) {
  using ImmobilePayload = HamtNodeValue<NonMovableValue, Source>;
  int destructions = 0;
  {
    auto domain_result = Domain::TryCreate();
    ASSERT_THAT(domain_result.has_value(), Eq(true));
    const auto domain = domain_result.value_or(Domain{});
    auto created = ImmobilePayload::TryCreate(domain, destructions);
    ASSERT_THAT(created.has_value(), Eq(true));
    ImmobilePayload first = std::move(created).value_or(ImmobilePayload{});
    ImmobilePayload second = first;
    ImmobilePayload moved = std::move(first);
    EXPECT_THAT(first.get(), Eq(nullptr));
    EXPECT_THAT(second.get(), Eq(moved.get()));
    EXPECT_THAT(destructions, Eq(0));
  }
  EXPECT_THAT(destructions, Eq(1));
}

enum class InvalidBlock { kNull, kSmall, kUnaligned, kInsufficientAlignment };

struct InvalidBlockState final {
  alignas(std::max_align_t) std::array<std::byte, 512> storage{};
  InvalidBlock kind = InvalidBlock::kNull;
  mbo::memory::MemoryBlock returned;
  mbo::memory::MemoryBlock released;
  int releases = 0;
};

struct InvalidBlockSource final {
  explicit InvalidBlockSource(InvalidBlockState& state) noexcept : state(&state) {}

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    state->returned = {.data = state->storage.data(), .size = size, .alignment = alignment};
    switch (state->kind) {
      case InvalidBlock::kNull: state->returned.data = nullptr; break;
      case InvalidBlock::kSmall: state->returned.size = size - 1; break;
      case InvalidBlock::kUnaligned: state->returned.data = std::addressof(state->storage.at(1)); break;
      case InvalidBlock::kInsufficientAlignment: state->returned.alignment = 1; break;
    }
    return state->returned;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    state->released = block;
    ++state->releases;
  }

  InvalidBlockState* state;
};

TEST_F(HamtNodeValueTest, InvalidReturnedStorageIsReleasedWithItsOriginalMetadata) {
  using InvalidDomain = HamtSourceDomain<InvalidBlockSource>;
  using InvalidPayload = HamtNodeValue<int, InvalidBlockSource>;
  constexpr auto kKinds = std::to_array<InvalidBlock>(
      {InvalidBlock::kNull, InvalidBlock::kSmall, InvalidBlock::kUnaligned, InvalidBlock::kInsufficientAlignment});
  for (const auto kind : kKinds) {
    InvalidBlockState state;
    state.kind = kind;
    auto domain_result = InvalidDomain::TryCreate(state);
    ASSERT_THAT(domain_result.has_value(), Eq(true));
    const auto domain = domain_result.value_or(InvalidDomain{});
    EXPECT_THAT(InvalidPayload::TryCreate(domain, 42).has_value(), Eq(false));
    EXPECT_THAT(state.releases, Eq(1));
    EXPECT_THAT(state.released.data, Eq(state.returned.data));
    EXPECT_THAT(state.released.size, Eq(state.returned.size));
    EXPECT_THAT(state.released.alignment, Eq(state.returned.alignment));
  }
}

TEST_F(HamtNodeValueTest, SharedPayloadKeepsItsSourceAndAddressAfterTheOriginalOwnersDisappear) {
  Payload retained;
  const int* address = nullptr;
  {
    auto domain_result = Domain::TryCreate();
    ASSERT_THAT(domain_result.has_value(), Eq(true));
    Domain domain = domain_result.value_or(Domain{});
    auto created = Payload::TryCreate(domain, 42);
    ASSERT_THAT(created.has_value(), Eq(true));
    Payload original = created.value_or(Payload{});
    retained = original;
    address = original.get();
    EXPECT_THAT(original.is_unique(), Eq(false));
    EXPECT_THAT(Payload::TryCreate(domain, 99).has_value(), Eq(false));
  }
  ASSERT_THAT(retained.get(), NotNull());
  EXPECT_THAT(retained.get(), Eq(address));
  EXPECT_THAT(*retained.get(), Eq(42));
  EXPECT_THAT(retained.is_unique(), Eq(true));
  Payload moved = std::move(retained);
  EXPECT_THAT(retained.get(), Eq(nullptr));
  EXPECT_THAT(moved.get(), Eq(address));
}

TEST_F(HamtNodeValueTest, EmptyDomainAndInsufficientStorageReportFailure) {
  EXPECT_THAT(Payload::TryCreate(Domain{}, 42).has_value(), Eq(false));
  using SmallSource = mbo::memory::InlineBlockSource<1>;
  using SmallDomain = HamtSourceDomain<SmallSource>;
  auto domain_result = SmallDomain::TryCreate();
  ASSERT_THAT(domain_result.has_value(), Eq(true));
  const auto domain = domain_result.value_or(SmallDomain{});
  EXPECT_THAT((HamtNodeValue<int, SmallSource>::TryCreate(domain, 42).has_value()), Eq(false));
}

TEST_F(HamtNodeValueTest, TheLastPayloadOwnerReturnsStorageForReuse) {
  auto domain_result = Domain::TryCreate();
  ASSERT_THAT(domain_result.has_value(), Eq(true));
  const auto domain = domain_result.value_or(Domain{});
  {
    auto created = Payload::TryCreate(domain, 42);
    ASSERT_THAT(created.has_value(), Eq(true));
    Payload retained = created.value_or(Payload{});
    created.reset();
    EXPECT_THAT(Payload::TryCreate(domain, 99).has_value(), Eq(false));
    EXPECT_THAT(retained.is_unique(), Eq(true));
  }
  auto reused = Payload::TryCreate(domain, 99);
  ASSERT_THAT(reused.has_value(), Eq(true));
  Payload value = std::move(reused).value_or(Payload{});
  ASSERT_THAT(value.get(), NotNull());
  EXPECT_THAT(*value.get(), Eq(99));
}

TEST_F(HamtNodeValueTest, SharedMutableAccessReportsExhaustionAndUniqueAccessKeepsTheAddress) {
  auto domain_result = Domain::TryCreate();
  ASSERT_THAT(domain_result.has_value(), Eq(true));
  const auto domain = domain_result.value_or(Domain{});
  auto created = Payload::TryCreate(domain, 42);
  ASSERT_THAT(created.has_value(), Eq(true));
  Payload current = std::move(created).value_or(Payload{});
  Payload snapshot = current;
  const auto* const address = current.get();
  EXPECT_THAT(current.try_get_mutable().has_value(), Eq(false));
  EXPECT_THAT(current.get(), Eq(address));
  snapshot = Payload{};
  auto* const mutable_value = current.try_get_mutable().value_or(nullptr);
  ASSERT_THAT(mutable_value, NotNull());
  EXPECT_THAT(mutable_value, Eq(address));
  *mutable_value = 99;
  EXPECT_THAT(*current.get(), Eq(99));
}

TEST_F(HamtNodeValueTest, SharedMutableAccessCopiesOnlyTheEditedPayload) {
  using HeapSource = mbo::memory::NewDeleteBlockSource;
  using HeapDomain = HamtSourceDomain<HeapSource>;
  using HeapPayload = HamtNodeValue<int, HeapSource>;
  auto domain_result = HeapDomain::TryCreate();
  ASSERT_THAT(domain_result.has_value(), Eq(true));
  const auto domain = domain_result.value_or(HeapDomain{});
  auto created = HeapPayload::TryCreate(domain, 42);
  ASSERT_THAT(created.has_value(), Eq(true));
  HeapPayload current = std::move(created).value_or(HeapPayload{});
  const HeapPayload snapshot = current;
  auto* const value = current.try_get_mutable().value_or(nullptr);
  ASSERT_THAT(value, NotNull());
  EXPECT_THAT(value == snapshot.get(), Eq(false));
  *value = 99;
  EXPECT_THAT(*snapshot.get(), Eq(42));
  EXPECT_THAT(*current.get(), Eq(99));
  HeapPayload empty;
  EXPECT_THAT(empty.try_get_mutable().has_value(), Eq(true));
  EXPECT_THAT(empty.try_get_mutable().value_or(nullptr), Eq(nullptr));
}

}  // namespace
}  // namespace mbo::container::container_internal
