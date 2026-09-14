// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/container_string_index.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct ContainerStringIndexTest : ::testing::Test {};

TEST_F(ContainerStringIndexTest, AbseilFlatMapUsesTheSameAdapterWithoutExposingIterators) {
  ContainerStringIndex<StringId<>, absl::flat_hash_map<std::string_view, StringId<>>> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  EXPECT_THAT(index.try_insert("a", StringId<>(1)), Optional(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find("missing").has_value(), Eq(false));
}

TEST_F(ContainerStringIndexTest, AbseilNodeMapUsesTheSameAdapterWithoutExposingIterators) {
  ContainerStringIndex<StringId<>, absl::node_hash_map<std::string_view, StringId<>>> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  EXPECT_THAT(index.try_insert("a", StringId<>(1)), Optional(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find("missing").has_value(), Eq(false));
}

TEST_F(ContainerStringIndexTest, StandardMapAdapterRetainsOriginalIdsOnDuplicates) {
  ContainerStringIndex<> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  EXPECT_THAT(index.try_insert("a", StringId<>(1)), Optional(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find("missing").has_value(), Eq(false));
}

TEST_F(ContainerStringIndexTest, StandardMapCompositionSupportsCapturedParentPrefixes) {
  using Interner = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, ContainerStringIndex<>>;
  Interner root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  Interner child(&root);
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later").has_value(), Eq(false));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  EXPECT_THAT(child.rfind("root"), Optional(StringId<>(0)));
}
}  // namespace
}  // namespace mbo::strings
