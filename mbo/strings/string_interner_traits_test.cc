// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner_traits.h"

#include <map>
#include <string>
#include <string_view>
#include <unordered_set>

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {

struct StringInternerTraitsTest : ::testing::Test {};

TEST_F(StringInternerTraitsTest, RecognizesStableAndUnknownContainers) {
  static_assert(kHasStableKeyAddresses<std::map<std::string, int>>);
  static_assert(kHasStableKeyAddresses<std::unordered_set<std::string>>);
  static_assert(kHasStableKeyAddresses<absl::node_hash_set<std::string>>);
  static_assert(!kHasStableKeyAddresses<absl::flat_hash_set<std::string>>);
  SUCCEED();
}

TEST_F(StringInternerTraitsTest, ExtractsMapAndSetKeys) {
  using Map = std::map<std::string_view, int>;
  using Set = std::unordered_set<std::string_view>;
  static_assert(std::same_as<StringInternerKey<Map>, std::string_view>);
  static_assert(std::same_as<StringInternerKey<Set>, std::string_view>);
  SUCCEED();
}

}  // namespace
}  // namespace mbo::strings
