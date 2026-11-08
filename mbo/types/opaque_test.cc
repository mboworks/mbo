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

#include "mbo/types/opaque.h"

#include <list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::types {

struct StringWrap;  // Forward declared!

namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

struct OpaqueTest : ::testing::Test {};

template<typename T>
struct OpaqueContainerTest : ::testing::Test {};

template<typename Container = std::vector<std::string>>
struct Data {
  using ContainerType = Container;

  Data() : data(std::make_unique<ContainerType>()) {}

  std::unique_ptr<ContainerType> data;

  ContainerType& GetData() { return *data; }

  const ContainerType& GetData() const { return *data; }
};

template<typename Container>
struct Proxy : ContainerProxy<Data<Container>, Container, &Data<Container>::GetData, &Data<Container>::GetData> {};

// Access through the proxy means accessing the `std::vector` that is wrapped in a `std::unique_ptr`.
using ProxyVectorString = Proxy<std::vector<std::string>>;

// Access works the same way if the container is a `std::list` since vector and list both support the functions that
// the test below needs.
using ProxyListString = Proxy<std::list<std::string>>;

// A vector of string can be used in a std::unique_ptr just fine. We provide this so we guarantee that the opaque types
// behave identical.
using OpaqueVectorString = OpaqueContainer<std::vector<std::string>>;

// The use of the `StringWrap` as the `value_type` for the `std::vector` prevents the container from working as the
// `element_type` in a `std::unique_ptr`. That is because the `StringWrap` type is forward declared and hence when the
// `std::unique_ptr` gets declared, it fails to see the required destructor; or more precisely the delete operation
// of the `std::unique_ptr` would trigger the deletion of the `std::vector` which in turn would trigger the deletion of
// all elements. But those are not fully defined, and so their size is unknown and so they cannot be deleted. However,
// the template `OpaqueContainer` works around this by forward declaring the deletor and then providing it through the
// macro `MBO_TYPES_OPAQUE_HOOKS(std::vector<StringWrap>)`.
using OpaqueVectorStringWrap = OpaqueContainer<std::vector<StringWrap>>;

using OpaqueListStringWrap = OpaqueContainer<std::list<StringWrap>>;

using MyTypes = ::testing::Types<
    std::vector<std::string>,
    ProxyVectorString,
    ProxyListString,
    OpaqueVectorString,
    OpaqueVectorStringWrap,
    OpaqueListStringWrap>;

class MyTypeNames {
 public:
  template<typename T>
  // GoogleTest stores generated type names through vector<string>::push_back.
  // NOLINTNEXTLINE(modernize-use-string-view)
  static std::string GetName(int /*unused*/) {
    if constexpr (std::is_same<T, std::vector<std::string>>()) {
      return "VectorOfString";
    } else if constexpr (std::is_same<T, ProxyVectorString>()) {
      return "ProxyVectorString";
    } else if constexpr (std::is_same<T, ProxyListString>()) {
      return "ProxyListString";
    } else if constexpr (std::is_same<T, OpaqueVectorString>()) {
      return "OpaqueVectorString";
    } else if constexpr (std::is_same<T, OpaqueVectorStringWrap>()) {
      return "OpaqueVectorStringWrap";
    } else {
      return "Unknown";
    }
  }
};

TYPED_TEST_SUITE(OpaqueContainerTest, MyTypes, MyTypeNames);

TYPED_TEST(OpaqueContainerTest, Test) {
  TypeParam data;
  EXPECT_THAT(data, IsEmpty());
  EXPECT_THAT(data, SizeIs(0));
  data.push_back("1");
  EXPECT_THAT(data.front(), "1");
  EXPECT_THAT(data.back(), "1");
  data.push_back("2");
  EXPECT_THAT(data.front(), "1");
  EXPECT_THAT(data.back(), "2");
  EXPECT_THAT(data, Not(IsEmpty()));
  EXPECT_THAT(data, SizeIs(2));
  EXPECT_THAT(data, ElementsAre("1", "2"));
  data.pop_back();
  EXPECT_THAT(data, Not(IsEmpty()));
  EXPECT_THAT(data, SizeIs(1));
  EXPECT_THAT(data, ElementsAre("1"));
}

}  // namespace

struct StringWrap : std::string {
  using std::string::string;
};

}  // namespace mbo::types

namespace std {
/* NOLINTBEGIN(cert-dcl58-cpp,cppcoreguidelines-owning-memory) */
MBO_TYPES_OPAQUE_HOOKS(std::vector<std::string>);
MBO_TYPES_OPAQUE_HOOKS(std::vector<mbo::types::StringWrap>);
MBO_TYPES_OPAQUE_HOOKS(std::list<mbo::types::StringWrap>);
/* NOLINTEND(cert-dcl58-cpp,cppcoreguidelines-owning-memory) */
}  // namespace std
