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

#ifndef MBO_TESTING_MATCHERS_H_
#define MBO_TESTING_MATCHERS_H_

#include <concepts>  // IWYU pragma: keep
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock-matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/indent.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep

namespace mbo::testing {

inline auto IsNullopt() {
  return ::testing::Eq(std::nullopt);
}

namespace testing_internal {

// Element-equality used inside `IsElementOf`. Defers to raw `operator==` when the two sides are
// directly comparable; otherwise falls back to a component-wise comparison for `std::pair`. The
// pair fallback exists because C++20 made `std::pair::operator==` same-types-only at the standard
// level (libc++ accepts heterogeneous comparison via implicit pair conversion, libstdc++ does
// not), and we want `EXPECT_THAT(std::make_pair("a", "b"), IsElementOf(list<pair<string,
// string>>))` to compile on both.
template<typename L, typename R>
concept HasEqualityWith = requires(const L& lhs, const R& rhs) {
  { lhs == rhs } -> std::convertible_to<bool>;
};

template<typename T>
struct IsStdPair : std::false_type {};

template<typename A, typename B>
struct IsStdPair<std::pair<A, B>> : std::true_type {};

template<typename L, typename R>
// LCOV_MERGE_FUNC_LINE: repeated for every matched element type.
constexpr bool ElementEqual(const L& lhs, const R& rhs) {
  if constexpr (HasEqualityWith<L, R>) {
    return lhs == rhs;
  } else if constexpr (IsStdPair<std::remove_cvref_t<L>>::value && IsStdPair<std::remove_cvref_t<R>>::value) {
    return ElementEqual(lhs.first, rhs.first) && ElementEqual(lhs.second, rhs.second);
  } else {
    static_assert(HasEqualityWith<L, R>, "IsElementOf: element types are not equality-comparable");
    return false;
  }
}

// Implementation of the polymorphic `IsElementOf` matcher. Each call to `EXPECT_THAT(value, ...)`
// instantiates an Impl<Value> via the conversion operator; the comparison inside `MatchAndExplain`
// uses `ElementEqual` (raw `operator==`, with a pair-decomposition fallback) so the subject does
// not need to share `value_type` with the container.
//
// `positive_lead` / `negative_lead` are the descriptive prefixes used in failure messages, e.g.
// "is element of {1, 2}" / "is not element of {1, 2}". `IsKeyOf` and `IsValueOf` substitute "is a
// key of" / "is a value of" so the message reflects the projection that was applied.
template<typename Container>
class IsElementOfMatcher {
 public:
  IsElementOfMatcher(Container container, std::string_view positive_lead, std::string_view negative_lead)
      : container_(std::move(container)), positive_lead_(positive_lead), negative_lead_(negative_lead) {}

  template<typename Value>
  operator ::testing::Matcher<Value>() const {  // NOLINT(*-explicit-constructor,hicpp-explicit-conversions)
    return ::testing::Matcher<Value>(new Impl<const Value&>(container_, positive_lead_, negative_lead_));
  }

  template<typename Value>
  class Impl : public ::testing::MatcherInterface<Value> {
   public:
    Impl(Container container, std::string_view positive_lead, std::string_view negative_lead)
        : container_(std::move(container)), positive_lead_(positive_lead), negative_lead_(negative_lead) {}

    // LCOV_MERGE_FUNC_LINE: repeated for every matched container type.
    void DescribeTo(::std::ostream* os) const override { DescribeContents(*os, positive_lead_); }

    // LCOV_MERGE_FUNC_LINE: repeated for every matched container type.
    void DescribeNegationTo(::std::ostream* os) const override { DescribeContents(*os, negative_lead_); }

    bool MatchAndExplain(Value value, ::testing::MatchResultListener* listener) const override {
      std::size_t index = 0;
      for (const auto& element : container_) {
        if (ElementEqual(value, element)) {
          if (listener->IsInterested()) {
            *listener << "which equals element #" << index << " (" << ::testing::PrintToString(element) << ")";
          }
          return true;
        }
        ++index;
      }
      return false;
    }

   private:
    // LCOV_MERGE_FUNC_LINE: repeated for every matched container type.
    void DescribeContents(::std::ostream& os, std::string_view lead) const {
      os << lead << " {";
      bool first = true;
      for (const auto& element : container_) {
        if (!first) {
          os << ", ";
        }
        first = false;
        os << ::testing::PrintToString(element);
      }
      os << "}";
    }

    const Container container_;
    const std::string_view positive_lead_;
    const std::string_view negative_lead_;
  };

 private:
  const Container container_;
  const std::string_view positive_lead_;
  const std::string_view negative_lead_;
};

}  // namespace testing_internal

// Matcher that asserts the value-under-test equals at least one element of `container`.
//
// This is the element-on-the-left orientation of `::testing::Contains`: the value is the subject
// and the container is the search space. Comparison uses raw `operator==` (not `Eq` parameterized
// on the container's `value_type`), so the subject does not need to be converted to the element
// type by the caller. A string-literal subject can be tested against a `std::vector<std::string>`,
// a `std::string_view` subject against a `std::map<std::string, ...>` key set, etc.
//
//   std::vector<std::string> allowed = {"a", "b"};
//   EXPECT_THAT("a", IsElementOf(allowed));                    // const char[]
//   EXPECT_THAT(std::string_view{"a"}, IsElementOf(allowed));  // string_view
//   EXPECT_THAT(2, IsElementOf({1, 2, 3}));                    // initializer list
//
// The container is taken by value so temporaries and initializer lists are safe.
template<typename Container>
inline testing_internal::IsElementOfMatcher<Container> IsElementOf(Container container) {
  return testing_internal::IsElementOfMatcher<Container>(std::move(container), "is element of", "is not element of");
}

template<typename T>
inline testing_internal::IsElementOfMatcher<std::vector<T>> IsElementOf(std::initializer_list<T> values) {
  return testing_internal::IsElementOfMatcher<std::vector<T>>(
      std::vector<T>(values), "is element of", "is not element of");
}

namespace testing_internal {

// Returns the keys of an associative container as a `std::vector<key_type>` in iteration order.
// Internal: used to feed `IsKeyOf` (and other element-oriented matchers composed via
// `IsElementOf`). Not exposed at namespace scope because the natural calling convention puts
// the projection on the right of `EXPECT_THAT`, inside another matcher.
template<typename Map>
inline std::vector<typename Map::key_type> AllKeys(const Map& map) {
  std::vector<typename Map::key_type> keys;
  keys.reserve(map.size());
  for (const auto& kv : map) {
    keys.push_back(kv.first);
  }
  return keys;
}

// Returns the mapped values of an associative container as a `std::vector<mapped_type>` in
// iteration order. Internal: see `AllKeys` above.
template<typename Map>
inline std::vector<typename Map::mapped_type> AllValues(const Map& map) {
  std::vector<typename Map::mapped_type> values;
  values.reserve(map.size());
  for (const auto& kv : map) {
    values.push_back(kv.second);
  }
  return values;
}

}  // namespace testing_internal

// Matcher that asserts the value-under-test equals at least one key of `map`.
//
//   std::map<int, std::string> map = {{1, "a"}, {2, "b"}};
//   EXPECT_THAT(key, IsKeyOf(map));
template<typename Map>
inline auto IsKeyOf(const Map& map) {
  auto keys = testing_internal::AllKeys(map);
  using KeysContainer = decltype(keys);
  return testing_internal::IsElementOfMatcher<KeysContainer>(std::move(keys), "is a key of", "is not a key of");
}

// Matcher that asserts the value-under-test equals at least one mapped value of `map`.
//
//   std::map<int, std::string> map = {{1, "a"}, {2, "b"}};
//   EXPECT_THAT(value, IsValueOf(map));
template<typename Map>
inline auto IsValueOf(const Map& map) {
  auto values = testing_internal::AllValues(map);
  using ValuesContainer = decltype(values);
  return testing_internal::IsElementOfMatcher<ValuesContainer>(std::move(values), "is a value of", "is not a value of");
}

namespace testing_internal {

// Implements the polymorphic `CapacityIs` matcher, which
// can be used as a Matcher<T> as long as T is either a container that defines
// capacity() (e.g. std::vector or std::string), or a C-style string.
template<typename CapacityMatcher>
class CapacityIsMatcher {
 public:
  explicit CapacityIsMatcher(const CapacityMatcher& capacity_matcher) : capacity_matcher_(capacity_matcher) {}

  template<typename Container>
  operator ::testing::Matcher<Container>() const {  // NOLINT(*-explicit-constructor,hicpp-explicit-conversions)
    return ::testing::Matcher<Container>(new Impl<const Container&>(capacity_matcher_));
  }

  template<typename Container>
  class Impl : public ::testing::MatcherInterface<Container> {
   public:
    using CapacityType = decltype(std::declval<Container>().capacity());

    explicit Impl(const CapacityMatcher& capacity_matcher)
        : capacity_matcher_(::testing::MatcherCast<CapacityType>(capacity_matcher)) {}

    // The description behavior is covered by CapacityIsDescriptions. GCC emits
    // another identical function for every matched container type, so counting
    // those specializations would measure type usage rather than source behavior.
    void DescribeTo(::std::ostream* os) const override {  // LCOV_EXCL_FUNC_LINE
      *os << "has a capacity that ";
      capacity_matcher_.DescribeTo(os);
    }

    void DescribeNegationTo(::std::ostream* os) const override {  // LCOV_EXCL_FUNC_LINE
      *os << "has a capacity that ";
      capacity_matcher_.DescribeNegationTo(os);
    }

    bool MatchAndExplain(Container container, ::testing::MatchResultListener* listener) const override {
      const CapacityType capacity = container.capacity();
      ::testing::StringMatchResultListener capacity_listener;
      const bool result = capacity_matcher_.MatchAndExplain(capacity, &capacity_listener);
      *listener << "whose capacity " << capacity << (result ? " matches" : " doesn't match");
      ::testing::internal::PrintIfNotEmpty(capacity_listener.str(), listener->stream());
      return result;
    }

   private:
    const ::testing::Matcher<CapacityType> capacity_matcher_;
  };

 private:
  const CapacityMatcher capacity_matcher_;
};

}  // namespace testing_internal

// Returns a matcher that matches the container capacity. The container must support both capacity() and size_type which
// all STL-like containers provide. Note that the parameter 'capacity' can be a value of type size_type as well as
// matcher. For instance:
//   EXPECT_THAT(container, CapacityIs(2));     // Checks container has a capacity for 2 elements.
//   EXPECT_THAT(container, CapacityIs(Le(2));  // Checks container has a capacity for at most 2.
template<typename SizeMatcher>
inline testing_internal::CapacityIsMatcher<SizeMatcher> CapacityIs(const SizeMatcher& size_matcher) {
  return testing_internal::CapacityIsMatcher<SizeMatcher>(size_matcher);
}

namespace testing_internal {

template<typename Transformer, typename ContainerMatcher>
class WhenTransformedByMatcher {
 public:
  WhenTransformedByMatcher(const Transformer& transformer, const ContainerMatcher& container_matcher)
      : transformer_(transformer), container_matcher_(container_matcher) {}

  template<typename Container>
  requires(
      std::is_invocable_v<Transformer, typename std::remove_cvref_t<Container>::value_type>
      && !std::same_as<void, std::invoke_result_t<Transformer, typename std::remove_cvref_t<Container>::value_type>>)
  operator ::testing::Matcher<Container>() const {  // NOLINT(*-explicit-*)
    return ::testing::Matcher<Container>(new Impl<const Container&>(transformer_, container_matcher_));
  }

 private:
  template<typename Container>
  class Impl : public ::testing::MatcherInterface<Container> {
   public:
    using ContainerType = std::remove_cvref_t<Container>;
    using TransformedType = std::invoke_result_t<Transformer, typename ContainerType::value_type>;
    using TransformedContainerType = std::vector<TransformedType>;
    using MatcherType = ::testing::Matcher<TransformedContainerType>;

    explicit Impl(const Transformer& transformer, const ContainerMatcher& container_matcher)
        : transformer_(transformer),
          container_matcher_(::testing::MatcherCast<TransformedContainerType>(container_matcher)) {}

    // LCOV_MERGE_FUNC_LINE: repeated for every transformer specialization.
    void DescribeTo(::std::ostream* os) const override {
      *os << "when transformed ";
      container_matcher_.DescribeTo(os);
    }

    // LCOV_MERGE_FUNC_LINE: repeated for every transformer specialization.
    void DescribeNegationTo(::std::ostream* os) const override {
      *os << "when transformed ";
      container_matcher_.DescribeNegationTo(os);
    }

    bool MatchAndExplain(const Container& container, ::testing::MatchResultListener* listener) const override {
      TransformedContainerType transformed;
      transformed.reserve(container.size());
      for (const auto& v : container) {
        transformed.emplace_back(transformer_(v));
      }
      ::testing::StringMatchResultListener container_listener;
      const bool result = container_matcher_.MatchAndExplain(transformed, &container_listener);
      *listener << "which (when transformed";
      constexpr std::size_t kShowMaxElements = 0;
      if constexpr (kShowMaxElements > 0) {
        *listener << " is {";
        std::size_t index = 0;
        for (const auto& v : transformed) {
          if (index++ > 0) {
            *listener << ", ";
          }
          *listener << ::testing::PrintToString(v);
          if (index >= kShowMaxElements) {
            *listener << ", ...";
            break;
          }
        }
        *listener << "}";
      }
      *listener << ") " << (result ? "matches" : "doesn't match");
      ::testing::internal::PrintIfNotEmpty(container_listener.str(), listener->stream());
      return result;
    }

   private:
    const Transformer& transformer_;
    const MatcherType container_matcher_;
  };

  const Transformer& transformer_;
  const ContainerMatcher& container_matcher_;
};

}  // namespace testing_internal

// Matcher that allows to compare containers after transforming them. This sometimes allows for much
// more concise comparisons where a golden expectation is already available that only differs in a
// simple transformation. The matcher transformation must convert the elements of the container arg
// from their `value_type` to the matchers value type.
//
// That means that a `std::vector` of `int` can be compared to a `std::set` of `std::string`, if the
// transformer takes `int` as an argument and returns `std::string` as its result.
//
// Example:
// ```
// std::vector<int> numbers{1, 2, 3};
// std::set<std::string> strs{"1", "2", "3"};
// EXPECT_THAT(numbers, WhenTransformedBy([](int v) { return absl::StrCat(v); }, ElementsAre(strs)));
// ```
//
// More practical: Instead of using `Key` for every value in `ElementsAre` when checking a mapped
// container's keys, you can transform the input to just return the keys and then compare:
// ```
//  const std::map<int, int> map{{1, 2}, {3, 4}};
//  EXPECT_THAT(
//      map,
//      WhenTransformedBy([](const std::pair<int, int>& v) { return v.first; }), ElementsAre(1, 3));
// ```
//
// The internal comparison will always be performed on a `std::vector` whose elements are the result
// type of the `transformer`. This means that the resultign elements are exactly in the order of the
// default iteration of the argument. The matcher adapter `WhenSorted` or unordered versions of the
// standard container matchers work as expected.
template<typename Transformer, typename ContainerMatcher>
inline auto WhenTransformedBy(const Transformer& transformer, const ContainerMatcher& container_matcher) {
  return testing_internal::WhenTransformedByMatcher(transformer, container_matcher);
}

namespace testing_internal {

class EqualsTextMatcher : public ::testing::MatcherInterface<std::string_view> {
 public:
  explicit EqualsTextMatcher(std::string_view text) : text_(text) {}

  void DescribeTo(::std::ostream* os) const override { *os << "matches text"; }

  void DescribeNegationTo(::std::ostream* os) const override { *os << "does not match text"; }

  bool MatchAndExplain(std::string_view other, ::testing::MatchResultListener* listener) const override {
    if (!options_.drop_indent && text_ == other) {
      return true;
    }
    const auto wants_lines = [&] -> std::vector<std::string> {
      if (options_.drop_indent) {
        *listener << "[" << mbo::strings::DropIndent(text_) << "]\n";
        return absl::StrSplit(mbo::strings::DropIndent(text_), '\n');
      }
      return absl::StrSplit(text_, '\n');
    }();
    const std::vector<std::string> other_lines = absl::StrSplit(other, '\n');
    if (options_.drop_indent && wants_lines == other_lines) {
      return true;
    }
    *listener << "Text difference:\n"
              << ::testing::internal::edit_distance::CreateUnifiedDiff(wants_lines, other_lines);
    return false;
  }

  void SetDropIndent(bool drop_indent = true) { options_.drop_indent = drop_indent; }

 private:
  struct Options {
    bool drop_indent = false;
  };

  const std::string text_;
  Options options_;
};

}  // namespace testing_internal

using PolymorphicEqualsTextMatcher = ::testing::PolymorphicMatcher<testing_internal::EqualsTextMatcher>;

// Matcher that compares text line by line using unified diff.
// Can be wrapped with `WithDropIndent`.
inline PolymorphicEqualsTextMatcher EqualsText(std::string_view text) {
  return ::testing::MakePolymorphicMatcher(testing_internal::EqualsTextMatcher(text));
}

// Matcher that compares text line by line using unified diff.
// Can be wrapped with `WithDropIndent`.
template<std::size_t N>
inline PolymorphicEqualsTextMatcher EqualsText(const char text[N]) {  // NOLINT(*-avoid-c-arrays)
  return ::testing::MakePolymorphicMatcher(testing_internal::EqualsTextMatcher(text));
}

// Modifies `EqualsText` so that the provided `text` has its indents stripped, see `mbo::strings::DropIndent`.
// The value argument is kepts as is. If its indent should be dropped than that must be done manually, e.g.:
// ```c++
// EXPECT_THAT(DropIndent(text), WithDropIndent(EqualsText(golden)));
// ```
inline PolymorphicEqualsTextMatcher WithDropIndent(PolymorphicEqualsTextMatcher&& matcher) {
  matcher.mutable_impl().SetDropIndent();
  return std::move(matcher);
}

}  // namespace mbo::testing

#endif  // MBO_TESTING_MATCHERS_H_
