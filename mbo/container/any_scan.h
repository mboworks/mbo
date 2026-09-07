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

#ifndef MBO_CONTAINER_ANY_SCAN_H_
#define MBO_CONTAINER_ANY_SCAN_H_

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/log/absl_check.h"
#include "mbo/types/cases.h"
#include "mbo/types/traits.h"

// NOLINTBEGIN(readability-identifier-naming)

namespace mbo::container {

// NOLINTBEGIN(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)
// Type-erased scan machinery: it walks foreign storage through raw pointers.

// Type `AnyScan` is similar to `std::span`, `std::range` and `absl::Span`. However it works with
// any container that supports `begin()`, `end()` and whose `iterator` (likely the `const_iterator`)
// is a `std::input_iterator`. Type `ConstScan` is similar to `absl::Span<const T>` made from
// `absl::MakeConstSpan`. And while `AnyScan` and `ConstScan` return by reference, `ConvertingScan`
// returns by copy which allows it to convert the values of the container to the specified value.
//
// The Scan types supports pretty much all STL containers including `std::initializer_list`. Beyond
// `begin()` and `end()` the containers must be supported by `std::empty(C)` and `std::size(C)` as
// they provide `begin()` and `end()` calls. Those are only executed when actually called, so if
// they are costly, then that cost only applies if needed.
//
// IMPORTANT: This type is INDEPENDENT of the container type that means it is possible to write
// single functions that can take containers of any container type without needing further templates
// or overloads.
//
// HOWEVER: This type is possibly slower as the aforementioned wrappers, as it need mores abstraction
// layers to accomplish the independence of the container type.
//
// The type `AnyScan` can only be instantiated/created through `MakeAnyScan` or directly from a
// compatible `std::initializer_list` argument. Similarly `ConstScan` can be constructed from a
// `MakeConstScan` call or compatible `std::initializer_list` argument and finally `ConvertingScan`
// can be constructed from `MakeConvertingScan` or compatible `std::initializer_list` argument. This
// prevents hard to find issues where the different types and helper functions are incompatible.
//
// The implementqtion is type-safe and does not use any `reinterpret_cast` or other unnecessary
// tricks.
//
// The actual `AnyScan` type depends on two types:
// - `ValueType` which is derived from the container's value-type.
// - `DifferenceType` which is either `std::ptrdiff_t` or if provided and not compvertible, the
//   container's `difference_type`. This is done because not all containers that are otherwise
//   compatible have a `difference_type` but in order for `AnyScan` itself to be compatible with
//   `std::input_iterator` it has to provide that type. Further, we aim to use a predictable type
//   as much as possible, so that the callers do not need to unnecessarily guess that type.
//
// The more specialized `ConvertingScan` modifies `AnyScan` to accept `MakeConvertingScan` which
// allows type erased scanning using a specified type that can be constructed from the values of a
// compatible container. For instance this allows to scan containers of string-like values with a
// specific string-like container (see below).
//
// Example:
//
// ```
// void Report(AnyScan<std::string> data) {
//   for (auto& [first, second] : data) {
//     Report(std::move(first), std::move(second));
//   }
// }
//
// void User() {
//   std::vector<std::string> data{"foo", "bar"};
//   Report(MakeAnyScan(std::move(data)));
// }
//
// void Other() {
//   std::array<std::string, 2> data{"foo", "bar"};
//   Report(MakeAnyScan(std::move(data)));
// }
// ```
//
// Here the `User` used `std::vector`, but the caller may just switch to `std::list` or like `Other`
// may use a `std::array` or a `std::initializer_list` or whatever container. However, all callers
// must use containers with their `value_type` using `std::string`.
//
// WARNING: When scanning types like `std::set` or `std::map` or similar indexed containers, then it
// is important to know that their keys `value_type` (for set) or `value_type::first_type` (for map)
// is const and thus the type to scan over must be specified accordingly. In many cases it is more
// appropriate to work with `ConstScan` created by `MakeConstScan`. Example with correct `AnyScan`:
//
// ```
// void Foo(const AnyScan<std::pair<const std::string, int>& scan) {}
// std::map<std::string, int> data{{"foo", 25}, {"bar", 42}};
// Foo(MakeAnyScan(data));  // THIS WORKS because `Foo` has its `value_type::first_type` as const.
// ```
//
// NOTE: When working with `std::initializer_list`, then sometimes these can be assigned directly
// to the `AnyScan` without the need to invoke `MakeAnyScan`. However, the types must match
// const correctly. This cannot be fixed as for some types (e.g. std::string) that perform implicit
// conversions it would be an ambiguity to provide both `std::initializer_list<T>` as well as the
// const form `std::initializer_list<const T>` as parameters. This is not the case for `ConstScan`
// or `ConvertingScan`.
//
// Another consideration is whether the callers should convert their container data or whether the
// receiving API should do it. In the examples above the API requires a single entry point taking
// an `AnyScan` and the callers needed to invoke `MakeAnyScan`. The advantage is that all callers
// explicitly know how their containers are handle. That is `AnyScan` and `ConstScan` access by
// reference while `ConvertingScan` uses access by copy-conversion. Also, the receiving API only
// needs to implement a single function that does not need to be inlined (or require predetermined
// types). On th the other hand it is possible to leave the conversion handling completely to the
// receiving API by providing a second converting API function that is inlined, example:
//
// ```
// void Report(AnyScan<std::string> data) {
//   for (auto& [first, second] : data) {
//     Report(first, second);
//   }
// }
//
// template <mbo::types::ContainerHasInputIterator Data>
// void Report(Data&& data) {
//   Report(MakeAnyScan(std::forward<Data>(data)));
// }
//
// void User() {
//   std::vector<std::string> data{"foo", "bar"};
//   Report(std::move(data));
// }
//
// void Other() {
//   std::array<std::string, 2> data{"foo", "bar"};
//   Report(std::move(data));
// }
// ```
template<typename ValueType, typename DifferenceType = std::ptrdiff_t>
class AnyScan;

// A `ConstScan` is similar to an `AnyScan` and an `absl::Span<const T>` but unlike in the case of
// an `AnyScan` the values of a `ConstScan` are always const. That simplifies accepting const keyed
// containers like `std::set` or `std::map`. Example:
//
// ```
// void Foo(const ConstScan<std::string>& scan) {}
// std::set<std::string> data{"foo", "bar"};
// Foo(MakeConstScan(data));  // THIS WORKS because we are using `ConstScan`.
// ```
//
// NOTE: A `ConstScan` is still referencing the results, so it is faster than a `ConvertingScan`.
// However, when working with pairs, then all containers must have their `value_type::first_type`
// const - or it is not possible to mix const key'd containers like `std::map` with non const key'd
// containers (e.g. `std::vector<std::pair<int, int>>` as opposed to `std::map<int, int>` which is
// mixable with `std::vector<std::pair<const int, int>>`).
template<typename ValueType, typename DifferenceType = std::ptrdiff_t>
class ConstScan;

// A `ConvertingScan` is very similar to `AnyScan`, however it allows scanning of any container
// whose `value_types` - as returned by its const_iterator/iterator - can be copy-converted into the
// specified type. This in particular allows scanning containers of any string-like type as either
// `std::string` or `std::string_view`. The emphasis is on COPY as this kind of conversion requires
// return by value. As such the `ConvertingScan`'s iterators do not have an `operator->()`.
//
// The wrappers of this type can only be instantiated/created through `MakeConvertingScan` or
// directly from a `std::initializer_list` argument.
//
// Example:
//
// ```
// void Report(const ConvertingScan<std::pair<std::string_view, std::string_view>>& data) {
//   for (auto it = data.begin(); it != data.end(); ++it) {
//     ReportKV((*it).first, (*it).second);  // Use `(*it)` iteration as `operator->` is missing (1).
//   }
//   // (1) Of course the iteration could just be:
//   for (auto [first, second] : data) {
//     ReportKV(first, second);
//   }
// }
//
// void User() {
//   std::vector<std::string> data{{"foo", "25"}, {"bar", "42"}};
//   Report(MakeConvertingScan(std::move(data)));
// }
// ```
//
// Here the caller used `std::vector<std::pair<std::string, std::string>>`, but the target `Report`
// wants the elements converted to `std::pair<std::string_view, std::string_view>`, so this had to
// be a `ConvertingScan` that handles the type difference.
//
// Note: The `ConvertingScan` is less restrict about const and reference than `AnyScan` and
// `ConstScan` as it works on copies.
template<typename ValueType, typename DifferenceType = std::ptrdiff_t>
class ConvertingScan;

namespace container_internal {

enum class ScanMode { kAny, kConst, kConverting };

template<typename ValueType, typename DifferenceType = std::ptrdiff_t, ScanMode S = ScanMode::kAny>
class AnyScanImpl;

template<typename Container>
concept AcceptableContainer =
    ::mbo::types::ContainerHasInputIterator<Container> || ::mbo::types::IsInitializerList<Container>;

template<
    AcceptableContainer Container,
    ScanMode ScanModeVal,
    bool IsMoveOnly = std::movable<Container> && !::mbo::types::IsInitializerList<std::remove_cvref_t<Container>>>
class MakeAnyScanData {
 public:
  template<typename V, typename D, ScanMode S>
  friend class AnyScanImpl;

  static constexpr ScanMode kScanMode = ScanModeVal;

  MakeAnyScanData() = delete;

  constexpr explicit MakeAnyScanData(Container container) noexcept
      : container_(std::make_shared<Container>(std::move(container))) {}

  constexpr bool empty() const noexcept { return std::empty(*container_); }

  constexpr std::size_t size() const noexcept { return std::size(*container_); }

 private:
  constexpr Container& container() const noexcept { return *container_; }

  const std::shared_ptr<Container> container_;
};

template<AcceptableContainer Container, ScanMode ScanModeVal>
class MakeAnyScanData<Container, ScanModeVal, false> {
 public:
  template<typename V, typename D, ScanMode S>
  friend class AnyScanImpl;

  static constexpr ScanMode kScanMode = ScanModeVal;

  MakeAnyScanData() = delete;

  constexpr explicit MakeAnyScanData(const Container& container) noexcept : container_(container) {}

  constexpr bool empty() const noexcept { return std::empty(container_); }

  constexpr std::size_t size() const noexcept { return std::size(container_); }

 private:
  constexpr const Container& container() const noexcept { return container_; }

  const Container& container_;
};

template<typename ValueType, typename DifferenceType, ScanMode ScanModeVal>
class AnyScanImpl {
 public:
  using difference_type = DifferenceType;
  using element_type = std::remove_cvref_t<ValueType>;
  using value_type = ValueType;
  using pointer = ValueType*;
  using const_pointer = const ValueType*;
  using reference = ValueType&;
  using const_reference = const ValueType&;

  AnyScanImpl() = delete;

 private:
  static constexpr ScanMode kScanMode = ScanModeVal;
  static constexpr bool kAccessByRef = kScanMode != ScanMode::kConverting;

  using AccessType = std::conditional_t<kAccessByRef, ValueType&, ValueType>;

  using FuncMore = std::function<bool()>;
  using FuncCurr = std::function<AccessType()>;
  using FuncNext = std::function<void()>;

  struct IterFuncs {
    FuncMore more;
    FuncCurr curr;
    FuncNext next;
  };

  using FuncIter = std::function<IterFuncs()>;
  using FuncZero = std::function<bool()>;
  using FuncSize = std::function<std::size_t()>;

  struct AccessFuncs {
    FuncIter iter;
    FuncZero empty;
    FuncSize size;
  };

  using FuncClone = std::function<std::shared_ptr<AccessFuncs>()>;

  template<typename Pair>
  requires(mbo::types::IsPair<std::remove_cvref_t<Pair>>)
  struct FirstType {
    using RawPair = std::remove_cvref_t<Pair>;
    using RawFirst = RawPair::first_type;
    using type = mbo::types::Cases<
        mbo::types::IfThen<std::is_const_v<Pair> && std::is_reference_v<Pair>, const RawFirst&>,
        mbo::types::IfThen<std::is_const_v<Pair>, const RawFirst>,
        mbo::types::IfThen<std::is_reference_v<Pair>, RawFirst&>,
        mbo::types::IfElse<RawFirst>>;
  };

  template<typename SrcValueTypeT, typename DstAccessType>
  static constexpr void CheckCompatible() noexcept {
    using RawSrc = std::remove_cvref_t<SrcValueTypeT>;
    using RawDst = std::remove_cvref_t<DstAccessType>;
    if constexpr (::mbo::types::IsPair<RawSrc> && ::mbo::types::IsPair<RawDst>) {
      using SrcFirstType = FirstType<SrcValueTypeT>::type;
      using DstFirstType = FirstType<DstAccessType>::type;
      static_assert(
          std::convertible_to<SrcFirstType, DstFirstType>,
          "A common reason for this to fail is scanning over `std::map` and similar indexed containers "
          "without specifying the scan type's `first_type` as const.");
    }
    static_assert(std::convertible_to<SrcValueTypeT, DstAccessType>);
  }

  template<typename V, typename D>
  friend class ::mbo::container::AnyScan;

  template<typename V, typename D>
  friend class ::mbo::container::ConstScan;

  template<typename V, typename D>
  friend class ::mbo::container::ConvertingScan;

  template<typename C>
  using ContainerIterator = std::conditional_t<
      std::is_const_v<C>,
      typename std::remove_cvref_t<C>::const_iterator,
      typename std::remove_cvref_t<C>::iterator>;

  template<typename C>
  static auto MakeSharedIterator(C& container) {
    using Iterator = ContainerIterator<C>;
    return std::shared_ptr<Iterator>(new Iterator(container.begin()));
  }

  // NOLINTBEGIN(bugprone-forwarding-reference-overload)

  // For MakAnyScan / MakeConstScan
  template<AcceptableContainer Container>
  requires kAccessByRef
  explicit AnyScanImpl(const MakeAnyScanData<Container, kScanMode>& data)
      : funcs_{
            .iter =
                [data = data] {
                  auto pos = MakeSharedIterator(data.container());
                  return IterFuncs{
                      .more = [data = data, pos = pos]() -> bool { return *pos != data.container().end(); },
                      .curr = [&it = *pos]() -> AccessType { return *it; },
                      .next = [&it = *pos] { ++it; },
                  };
                },
            .empty = [data = data] { return data.empty(); },
            .size = [data = data] { return data.size(); },
        } {}

  // For MakConvertingScan
  template<AcceptableContainer Container>
  requires(
      !kAccessByRef  // This is the ConvertingScan constructor
      && types::ConstructibleFrom<AccessType, ::mbo::types::ContainerConstIteratorValueType<Container>>
      && types::ConstructibleFrom<value_type, AccessType>)
  explicit AnyScanImpl(const MakeAnyScanData<Container, kScanMode>& data)
      : funcs_{
            // NOTE: data must be copied here!
            .iter =
                [data = data] {
                  auto pos = MakeSharedIterator(data.container());
                  return IterFuncs{
                      .more = [data = data, pos = pos]() -> bool { return *pos != data.container().end(); },
                      .curr = [&it = *pos]() -> AccessType { return AccessType(*it); },
                      .next = [&it = *pos] { ++it; },
                  };
                },
            .empty = [data = data] { return data.empty(); },
            .size = [data = data] { return data.size(); },
        } {}

  // NOLINTEND(bugprone-forwarding-reference-overload)

 public:
  // Iterator equality deliberately does not retain or compare the originating scan. This keeps the
  // type-erased iterator lightweight, with the following consequences:
  //
  // * A default-constructed iterator is exhausted and compares equal to every other exhausted
  //   iterator, including the end iterator of a different scan.
  // * Two non-exhausted reference iterators compare equal when their current element addresses are
  //   equal. This is only an approximation because a container may expose the same element more
  //   than once.
  // * Non-exhausted converting iterators cannot be compared by address and compare unequal unless
  //   they are the same iterator object.
  //
  // As with standard iterators, do not compare iterators from unrelated ranges to determine range
  // identity.
  template<typename ItElementType, typename ItValueType, typename ItPointer, typename ItReference>
  class iterator_impl {
   public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = AnyScanImpl::difference_type;
    using element_type = ItElementType;
    using value_type = ItValueType;
    using pointer = ItPointer;
    using reference = ItReference;

    iterator_impl()
        : funcs_{
              .more = [] { return false; },
              .curr = nullptr,
              .next = [] {},
          } {}

    explicit iterator_impl(const AccessFuncs& funcs) : funcs_(funcs.iter()) {}

    reference operator*() const noexcept
    requires kAccessByRef
    {
      // We check both the presence of `funcs_` as well as calling the actual `more` function.
      // That means we bypass any protection an iterator may have, but we can make this function
      // `noexcept` assuming the iterator is noexcept for access. On the other hand we expect that
      // out of bounds access may actually raise. So we effectively side step such exceptions.
      // CHECK failures abort before GCC can flush coverage counters, so only the successful guard
      // path can appear in LCOV. The existing iterator tests exercise that path.
      ABSL_CHECK_NE(funcs_.curr, nullptr);  // NOLINT(*-missing-default-case) LCOV_EXCL_BR_LINE
      ABSL_CHECK(funcs_.more());            // LCOV_EXCL_BR_LINE
      return funcs_.curr();
    }

    value_type operator*() const noexcept
    requires(!kAccessByRef)
    {
      // CHECK failures abort before GCC can flush coverage counters, so only the successful guard
      // path can appear in LCOV. The existing iterator tests exercise that path.
      ABSL_CHECK_NE(funcs_.curr, nullptr);  // NOLINT(*-missing-default-case) LCOV_EXCL_BR_LINE
      ABSL_CHECK(funcs_.more());            // LCOV_EXCL_BR_LINE
      return value_type(funcs_.curr());
    }

    const_pointer operator->() const noexcept
    requires kAccessByRef
    {
      return funcs_.curr != nullptr && funcs_.more() ? &funcs_.curr() : nullptr;
    }

    iterator_impl& operator++() noexcept {
      funcs_.next();
      return *this;
    }

    iterator_impl operator++(int) noexcept {  // NOLINT(cert-dcl21-cpp)
      iterator_impl result = *this;
      funcs_.next();
      return result;
    }

    template<typename OE, typename OV, typename OP, typename OR>
    bool operator==(const iterator_impl<OE, OV, OP, OR>& other) const noexcept {
      if (this == &other) {
        return true;
      }
      const bool l_end = !funcs_.more();
      const bool o_end = !other.funcs_.more();
      // No need to also check `funcs_.curr != nullptr`, as then `more` would have returned false.
      if (l_end || o_end) {
        return l_end == o_end;
      }
      if constexpr (kAccessByRef) {
        // Approximate equal address means equal iterator. That is not always correct as a container
        // might have the same element reference twice.
        return &funcs_.curr() == &other.funcs_.curr();
      } else {
        // Cannot take address of rvalue temp. Comparing actual values would be incorrect.
        return false;
      }
    }

    template<typename OE, typename OV, typename OP, typename OR>
    bool operator!=(const iterator_impl<OE, OV, OP, OR>& other) const noexcept {
      return !(*this == other);
    }

   private:
    IterFuncs funcs_;  // Cannot be const.
  };

  using iterator = iterator_impl<element_type, value_type, pointer, reference>;
  using const_iterator = iterator_impl<element_type, const value_type, const_pointer, const_reference>;

  iterator begin() noexcept { return iterator(funcs_); }

  iterator end() noexcept { return iterator(); }

  const_iterator begin() const noexcept { return const_iterator(funcs_); }

  const_iterator end() const noexcept { return const_iterator(); }

  const_iterator cbegin() const noexcept { return const_iterator(funcs_); }

  const_iterator cend() const noexcept { return const_iterator(); }

  bool empty() const noexcept { return funcs_.empty(); }

  std::size_t size() const noexcept { return funcs_.size(); }

 private:
  AccessFuncs funcs_;
};

}  // namespace container_internal

template<typename ValueType, typename DifferenceType>
class AnyScan : public container_internal::AnyScanImpl<ValueType, DifferenceType, container_internal::ScanMode::kAny> {
 private:
  using AnyScanImpl = container_internal::AnyScanImpl<ValueType, DifferenceType, container_internal::ScanMode::kAny>;

 public:
  AnyScan(const std::initializer_list<ValueType>& data)
      : AnyScanImpl(
            container_internal::MakeAnyScanData<std::initializer_list<ValueType>, container_internal::ScanMode::kAny>(
                data)) {}

  template<::mbo::types::ContainerHasInputIterator Container>
  requires(types::ConstructibleFrom<ValueType, typename std::remove_cvref_t<Container>::value_type>)
  AnyScan(  // NOLINT(*-explicit-constructor,*-explicit-conversions)
      const container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kAny>& data)
      : AnyScanImpl(data) {}

  using AnyScanImpl::begin;
  using AnyScanImpl::end;
};

template<typename ValueType, typename DifferenceType>
class ConstScan
    : public container_internal::AnyScanImpl<const ValueType, DifferenceType, container_internal::ScanMode::kConst> {
 private:
  using AnyScanImpl =
      container_internal::AnyScanImpl<const ValueType, DifferenceType, container_internal::ScanMode::kConst>;

 public:
  ConstScan(const std::initializer_list<ValueType>& data)
      : AnyScanImpl(
            container_internal::MakeAnyScanData<std::initializer_list<ValueType>, container_internal::ScanMode::kConst>(
                data)) {}

  template<container_internal::AcceptableContainer Container>
  requires(types::ConstructibleFrom<const ValueType, typename std::remove_cvref_t<Container>::value_type>)
  ConstScan(  // NOLINT(*-explicit-constructor,*-explicit-conversions)
      const container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kConst>& data)
      : AnyScanImpl(data) {}

  using AnyScanImpl::begin;
  using AnyScanImpl::end;
};

template<typename ValueType, typename DifferenceType>
class ConvertingScan
    : public container_internal::AnyScanImpl<ValueType, DifferenceType, container_internal::ScanMode::kConverting> {
 private:
  using AnyScanImpl =
      container_internal::AnyScanImpl<ValueType, DifferenceType, container_internal::ScanMode::kConverting>;

 public:
  ConvertingScan(const std::initializer_list<ValueType>& data)
      : AnyScanImpl(
            container_internal::
                MakeAnyScanData<std::initializer_list<ValueType>, container_internal::ScanMode::kConverting>(data)) {}

  template<container_internal::AcceptableContainer Container>
  requires(types::ConstructibleFrom<ValueType, typename std::remove_cvref_t<Container>::value_type>)
  ConvertingScan(  // NOLINT(*-explicit-constructor,*-explicit-conversions)
      const container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kConverting>& data)
      : AnyScanImpl(data) {}

  using AnyScanImpl::begin;
  using AnyScanImpl::end;
};

// Creates adapters that can be received by `AnyScan` parameters.
template<container_internal::AcceptableContainer Container>
auto MakeAnyScan(Container&& container) noexcept {
  return container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kAny>(
      std::forward<Container>(container));
}

// Creates adapters that can be received by `ConstScan` parameters.
template<container_internal::AcceptableContainer Container>
auto MakeConstScan(Container&& container) noexcept {
  return container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kConst>(
      std::forward<Container>(container));
}

// Creates adapters that can be received by `ConvertingScan` parameters.
template<container_internal::AcceptableContainer Container>
auto MakeConvertingScan(Container&& container) noexcept {
  return container_internal::MakeAnyScanData<Container, container_internal::ScanMode::kConverting>(
      std::forward<Container>(container));
}

// NOLINTEND(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)

}  // namespace mbo::container

// NOLINTEND(readability-identifier-naming)

#endif  // MBO_CONTAINER_ANY_SCAN_H_
