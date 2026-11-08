// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mbo/types/typed_view.h"

#include <concepts>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::types {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsTrue;

// NOLINTBEGIN(*-magic-numbers)

struct TypedViewTest : ::testing::Test {};

TEST_F(TypedViewTest, ExposesTheUnderlyingViewsValueType) {
  // The point of the type: a view whose value_type is spelled out rather than
  // buried in whatever `views::transform` deduced.
  const std::vector<int> data{1, 2, 3};
  const auto view = TypedView(std::views::all(data));
  static_assert(std::same_as<decltype(view)::value_type, int>);
  static_assert(std::same_as<decltype(view)::difference_type, std::ptrdiff_t>);
}

TEST_F(TypedViewTest, IteratesTheUnderlyingView) {
  const std::vector<int> data{1, 2, 3};
  const auto view = TypedView(std::views::all(data));
  EXPECT_THAT(view, ElementsAre(1, 2, 3));
}

TEST_F(TypedViewTest, IteratesAnEmptyView) {
  const std::vector<int> data;
  const auto view = TypedView(std::views::all(data));
  EXPECT_THAT(view, IsEmpty());
  EXPECT_THAT(view.begin() == view.end(), IsTrue());
}

TEST_F(TypedViewTest, WrapsATransformedView) {
  const std::vector<int> data{1, 2, 3};
  const auto view = TypedView(std::views::transform(data, [](int val) { return val * 2; }));
  EXPECT_THAT(view, ElementsAre(2, 4, 6));
  static_assert(std::same_as<decltype(view)::value_type, int>);
}

TEST_F(TypedViewTest, CarriesANonTrivialValueType) {
  const std::vector<std::string> data{"a", "bb"};
  const auto view = TypedView(std::views::all(data));
  static_assert(std::same_as<decltype(view)::value_type, std::string>);
  EXPECT_THAT(view, ElementsAre("a", "bb"));
}

TEST_F(TypedViewTest, IsARange) {
  // It derives from `view_interface`, so range-based algorithms must accept it.
  const std::vector<int> data{1, 2, 3};
  auto view = TypedView(std::views::all(data));
  static_assert(std::ranges::range<decltype(view)>);
  EXPECT_THAT(std::ranges::distance(view), 3);
}

TEST_F(TypedViewTest, HasNoDefaultConstructor) {
  // Declared `= delete`: a TypedView without a view would have nothing to iterate.
  static_assert(!std::is_default_constructible_v<TypedView<std::ranges::ref_view<const std::vector<int>>>>);
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::types
