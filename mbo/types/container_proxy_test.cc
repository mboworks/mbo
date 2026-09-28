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

#include "mbo/types/container_proxy.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/circular_buffer.h"
#include "mbo/container/limited_vector.h"

namespace mbo::types {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Pointee;

// NOLINTBEGIN(*-magic-numbers)

struct ContainerProxyTest : ::testing::Test {};

// NOTE: the header documents `ContainerProxy<std::unique_ptr<C>>` working "directly"
// off the defaults. It does not compile: the defaults want
// `Container& (T::*)()`, while `unique_ptr::operator*` is const-qualified
// (`T& operator*() const`), so it can never bind the mutable slot. Everything below
// therefore goes through explicit accessors.

// The documented member case: a struct holding a container, reached through named
// accessors rather than `operator*`.
template<typename C>
struct Holder {
  using element_type = C;

  C data;

  C& GetData() { return data; }

  const C& GetData() const { return data; }
};

using HeldVec = ContainerProxy<
    Holder<std::vector<int>>,
    std::vector<int>,
    &Holder<std::vector<int>>::GetData,
    &Holder<std::vector<int>>::GetData>;

template<typename C>
using Held = ContainerProxy<Holder<C>, C, &Holder<C>::GetData, &Holder<C>::GetData>;

template<typename C>
concept HasPopBackValue = requires(C& container) { container.pop_back_value(); };

template<typename C>
concept HasPopFrontValue = requires(C& container) { container.pop_front_value(); };

using HeldLimited = Held<mbo::container::LimitedVector<int, 4>>;
using HeldCircular = Held<mbo::container::experimental::CircularBuffer<std::unique_ptr<int>>>;

static_assert(!HasPopBackValue<HeldVec>);
static_assert(!HasPopFrontValue<HeldVec>);
static_assert(HasPopBackValue<HeldLimited>);
static_assert(!HasPopFrontValue<HeldLimited>);
static_assert(HasPopBackValue<HeldCircular>);
static_assert(HasPopFrontValue<HeldCircular>);

TEST_F(ContainerProxyTest, ForwardsValuePopsForAvailableEndpoints) {
  HeldLimited limited;
  limited.push_back(1);
  limited.push_back(2);
  EXPECT_THAT(limited.pop_back_value(), Eq(2));
  EXPECT_THAT(limited, ElementsAre(1));

  HeldCircular circular;
  circular.push_front(std::make_unique<int>(1));
  circular.push_back(std::make_unique<int>(2));
  circular.push_back(std::make_unique<int>(3));
  EXPECT_THAT(circular.pop_front_value(), Pointee(Eq(1)));
  EXPECT_THAT(circular.pop_back_value(), Pointee(Eq(3)));
  circular.pop_front();
  EXPECT_THAT(circular, IsEmpty());
}

TEST_F(ContainerProxyTest, ForwardsIterationToAMemberViaAccessors) {
  const HeldVec proxy{{.data = {1, 2, 3}}};
  EXPECT_THAT(proxy, ElementsAre(1, 2, 3));
}

TEST_F(ContainerProxyTest, ExposesTheContainersTypeAliases) {
  static_assert(std::same_as<HeldVec::value_type, int>);
  static_assert(std::same_as<HeldVec::size_type, std::vector<int>::size_type>);
}

TEST_F(ContainerProxyTest, ForwardsConstIteration) {
  const HeldVec proxy{{.data = {1, 2}}};
  EXPECT_THAT(proxy.begin() != proxy.end(), IsTrue());
  EXPECT_THAT(*proxy.begin(), 1);
  EXPECT_THAT(*proxy.cbegin(), 1);
}

TEST_F(ContainerProxyTest, ForwardsReverseIteration) {
  HeldVec proxy{{.data = {1, 2, 3}}};
  EXPECT_THAT(*proxy.rbegin(), 3);
  EXPECT_THAT(*proxy.crbegin(), 3);
}

TEST_F(ContainerProxyTest, MutationThroughTheProxyReachesTheContainer) {
  HeldVec proxy{{.data = {1, 2, 3}}};
  *proxy.begin() = 42;
  EXPECT_THAT(proxy, ElementsAre(42, 2, 3));
  EXPECT_THAT(proxy.data, ElementsAre(42, 2, 3)) << "the underlying member really changed";
}

TEST_F(ContainerProxyTest, HandlesAnEmptyContainer) {
  HeldVec proxy{{.data = {}}};
  EXPECT_THAT(proxy, IsEmpty());
  EXPECT_THAT(proxy.begin() == proxy.end(), IsTrue());
}

TEST_F(ContainerProxyTest, WorksWithANonTrivialValueType) {
  using HeldStr = ContainerProxy<
      Holder<std::vector<std::string>>, std::vector<std::string>, &Holder<std::vector<std::string>>::GetData,
      &Holder<std::vector<std::string>>::GetData>;
  const HeldStr proxy{{.data = {"a", "bb"}}};
  static_assert(std::same_as<HeldStr::value_type, std::string>);
  EXPECT_THAT(proxy, ElementsAre("a", "bb"));
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::types
