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

// This header contains extenders to be used with `mbo::types::extend`.
//
// The following extenders are available:
//
// * Printable (default)
//
//   * Provides method `std::string ToString()` which generates a string
//     representation of the fields of the extended class as `"{value, value}"`.
//   * This is a default extender.
//   * Requires `AbslStringify`
//
// * Streamable
//
//   * Provides an `std:::ostream& operator<<` that uses the same format as
//     `Printable`.
//   * This is a default extender.
//   * Requires `AbslStringify`
//
// * Comparable
//
//   * Implements all comparison operators by means of `<=>` referring to the
//     `<=>` operator for the temporary `std::tuple` copies.
//   * Since the comparison is implemented with templated left/right parameters,
//     it might be necessary to provide concrete implementations for operators
//     `==` and/or `<` for types that have no known conversion or comparison.
//
// * AbslStringify (default)
//
//   * Provides: `friend void AbslStringify(Sink&, const Type& t)`
//   * Provides: protected `void OStreamFields(std::ostream& os, const StringifyOptions&) const`
//   * Enables use of the struct with `absl::Format` library.
//   * Required by: `Printable`, `Streamable`.
//
// * AbslHashable
//
//   * Make the extended type hashable.
//
// * Default:
//
//    * Printable, Streamable, Comparable, AbslHashable, AbslStringify.
//
//  * NoPrint:
//
//    * Comparable, AbslHashable, AbslStringify.
//
#ifndef MBO_TYPES_EXTENDER_H_
#define MBO_TYPES_EXTENDER_H_

// IWYU pragma: private, include "mbo/types/extend.h"

#include <concepts>  // IWYU pragma: keep
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

#include "absl/hash/hash.h"                   // IWYU pragma: keep
#include "mbo/types/internal/extender.h"      // IWYU pragma: export
#include "mbo/types/internal/struct_names.h"  // IWYU pragma: keep
#include "mbo/types/stringify.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep
#include "mbo/types/tstring.h"
#include "mbo/types/tuple_extras.h"

namespace mbo::types {

// Simplify generation of an 'Extender'. Instead of writing the whole template
// struct of struct with all necessary provisions, it is enough to provide the
// struct for the actual implementation. E.g.:
//
// ```c++
// template <typename ExtenderBase>
// struct MyExtenderImpl : ExtenderBase {
//   using Type = typename ExtenderBase::Type;  // Important!
//   // Implementation
// };
//
// using MyExtender = mbo::types::MakeExtender<"MyExtender"_ts, MyExtenderImpl>;
// ```
//
// NOTE: The implementation must be a `struct`, it cannot be a `class`.
//
// NOTE: If the implementation requires another Extender to be a base, then
// the `RequiredExtender` template parameter can be set to that Extender (not
// its implementation). As a result, the required Extender must be listed prior
// to this Extender in the `Extend` template parameter list.
// The final `MyExtender` can be a class or struct and it can be constructed
// with a 'using' directive. However, generating a class or struct is preferable
// as it provides better compiler error message in the event of requirement
// violations.
//
// The following example demonstrates construction of extender `MyExtender` that
// depends on `Printable`.
//
// ```c++
//   template <typename ExtenderBase>
//   struct MyExtenderImpl : ExtenderBase {
//     // Implementation
//   };
//
//   struct MyExtender final : mbo::types::MakeExtender<
//       "MyExtender"_ts,
//       MyExtenderImpl,
//       mbo::extender::Printable>;
// ```
template<::mbo::types::tstring ExtenderNameT, template<typename> typename ImplT, typename RequiredExtenderT = void>
struct MakeExtender {
  using RequiredExtender = RequiredExtenderT;

  static constexpr std::string_view GetExtenderName() { return decltype(ExtenderNameT)::str(); }

 private:
  // This friend is necessary to keep symbol visibility in check. But it has to
  // be either 'struct' or 'class' and thus results in `ImplT` being a struct.
  template<typename T, typename Extender>
  friend struct ::mbo::types::types_internal::UseExtender;

  template<typename ExtenderOrActualType>
  struct Impl : ImplT<ExtenderOrActualType> {
    using Type = ExtenderOrActualType::Type;
  };
};

template<typename ExtenderBase>
struct AbslStringify_ : ExtenderBase {  // NOLINT(readability-identifier-naming)
  using Type = ExtenderBase::Type;

  template<typename Other>
  friend struct AbslStringify_;

  template<typename Sink>
  friend void AbslStringify(Sink& sink, const Type& value) {
    std::ostringstream os;
    value.OStreamFields(os);
    sink.Append(os.str());
  }

 protected:
  // Stream the type to `os` with control via `field_options`.
  void OStreamFields(std::ostream& os, const StringifyOptions& default_options = Stringify::OptionsDefault()) const {
    OStreamFieldsStatic(os, static_cast<const Type&>(*this), default_options);
  }

  static void OStreamFieldsStatic(std::ostream& os, const Type& value, const StringifyOptions& default_options) {
    Stringify(default_options).Stream(os, value);
  }
};

template<typename ExtenderBase>
struct AbslHashable_ : ExtenderBase {  // NOLINT(readability-identifier-naming)
 private:
  using T = ExtenderBase::Type;

 public:
  template<typename H>
  friend H AbslHashValue(H hash, const T& obj) noexcept {
    return H::combine(std::move(hash), obj.ToTuple());
  }
};

template<typename ExtenderBase>
struct Comparable_ : ExtenderBase {  // NOLINT(readability-identifier-naming)
 private:
  using T = ExtenderBase::Type;

 public:
  // Define operator `<=>` on `const T&` since that is what defines the
  // comparisons for the intended type `T`. So we cannot simply default the
  // operator.
  // Also provide backing `==` and `<` operators.
  // Further this has to be consistent so all provided operators are on type 'T'
  // and implemented as friends. Otherwise this would trigger warning: ISO C++20
  // considers use of overloaded operator '==' (with operand types 'const
  // TestComparable' and 'const TestComparable') to be ambiguous despite there
  // being a unique best viable function [-Wambiguous-reversed-operator].
  // The core problem stems from synthesized operators interfering with finding
  // the best candidate. Also see:
  // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2468r2.html
  friend auto operator<=>(const T& lhs, const T& rhs) { return lhs.ToTuple() <=> rhs.ToTuple(); }

  friend bool operator==(const T& lhs, const T& rhs) { return lhs.ToTuple() == rhs.ToTuple(); }

  friend bool operator<(const T& lhs, const T& rhs) { return lhs.ToTuple() < rhs.ToTuple(); }

  // Define operator `<=>` on `const T&` and another type.
  template<typename Other, typename = decltype(StructToTuple(std::declval<const Other&>()))>
  auto operator<=>(const Other& other) const {
    return this->ToTuple() <=> StructToTuple(other);
  }

  template<IsTuple Other>
  auto operator<=>(const Other& other) const {
    return this->ToTuple() <=> other;
  }

  template<typename Other, typename = decltype(StructToTuple(std::declval<const Other&>()))>
  bool operator==(const Other& other) const {
    return this->ToTuple() == StructToTuple(other);
  }

  template<IsTuple Other>
  bool operator==(const Other& other) const {
    return this->ToTuple() == other;
  }

  template<typename Other, typename = decltype(StructToTuple(std::declval<const Other&>()))>
  bool operator<(const Other& other) const {
    return this->ToTuple() < StructToTuple(other);
  }

  template<IsTuple Other>
  bool operator<(const Other& other) const {
    return this->ToTuple() < other;
  }
};

template<typename ExtenderBase>
struct Printable_ : ExtenderBase {  // NOLINT(readability-identifier-naming)
 public:
  using T = ExtenderBase::Type;

  // Produce a string based on control via `field_options`.
  //
  // Parameter `field_options` must be an instance of `StringifyOptions` or
  // something that can produce those. It defaults to `MboTypesStringifyOptions`
  // if available and otherwise the default `StringifyOptions` will be used.
  std::string ToString(const StringifyOptions& default_options = Stringify::OptionsDefault()) const {
    std::ostringstream os;
    this->OStreamFields(os, default_options);
    return os.str();
  }

  std::string ToJsonString() const { return ToString(Stringify::OptionsJson()); }
};

template<typename ExtenderBase>
struct Streamable_ : ExtenderBase {  // NOLINT(readability-identifier-naming)
 public:
  friend std::ostream& operator<<(std::ostream& os, const ExtenderBase::Type& v) {
    v.OStreamFields(os);
    return os;
  }
};

namespace extender {

// Extender that injects functionality to make an `Extend`ed type work with
// abseil format/print functions (see
// [AbslStringify](https://abseil.io/docs/cpp/guides/format#abslstringify)).
//
// This default Extender is automatically available through `mb::types::Extend`
// and `mbo::types::NoPrint`.
//
// If the compiler and the structure support `__buildtin_dump_struct` (e.g. if
// compiled with Clang), then this automatically supports field names. However,
// this does not work with `union`s. Further, providing field names can be
// suppressed by providing typename `MboTypesStringifyDoNotPrintFieldNames`, e.g.:
//   `using MboTypesStringifyDoNotPrintFieldNames = void;`
// Once presnet, the compiler will no longer generate code to fetch field names.
//
// Note that even if field names are disabled using the above type injection,
// supporting field names is still manually possible, and thus independent of
// compiler support if necessary using `StringifyOptions`, see below.
//
// Field names are automatically provided if the active compiler supports this
// (e.g. Clang). In absence (or to override them) the field names can be provided
// using the ADL extension point `void MboTypesStringifyFieldNames(const Type&)`.
//
// The implementation allows for complex formatting control by implementing the
// ADL fiend method  `StringifyOptions(self, field_index, field_name)`
// which must return `struct mbo::types::StringifyOptions`. That struct
// contains the full documentation. While the function must technically be static
// its first parameter is the object itself. Not that the correct type for
// the first parameter `self` without explicit object parameter support is `Type` as provided by
// `Extend`.
//
// Example:
// ```c++
// struct TestType : mbo::types::Extend<TestType> {
//   int number;
//
//   friend auto MboTypesStringifyFieldNames(const TestType& /* unused */) {
//     return std::array<std::string_view, 1>({"number"});
//   }
//
//   friend mbo::types::StringifyOptions MboTypesStringifyOptions(
//       const TestType& /* unused object */,
//       const StringifyFieldInfo& /* unused field */) {
//     return {
//       .value_max_length = 42,
//     };
//   }
// };
// ```
struct AbslStringify final : MakeExtender<"AbslStringify"_ts, AbslStringify_> {};

// Extender that injects functionality to make an `Extend`ed type work with
// abseil hashing (and also `std::hash`).
//
// Example:
//
// ```c++
// struct Name : mbo::types::Extend<Name> {
//   std::string first;
//   std::string last;
// };
//
// void demo() {
//   const Name name{.first = "first", .last = "last"};
//   const std::size_t hash = absl::HashOf(name);
//   const std::size_t std_hash = std::hash<Name>{}(name);
//   static_assert(hash == std_hash, "Must be the same");
// }
// ```
//
// This default Extender is automatically available through `mb::types::Extend`
// and `mbo::types::NoPrint`.
struct AbslHashable final : MakeExtender<"AbslHashable"_ts, AbslHashable_> {};

// Extender that injects functionality to make an `Extend`ed type comparable.
// All comparators will be injected: `<=>`, `==`, `!=`, `<`, `<=`, `>`, `>=`.
//
// This default Extender is automatically available through `mb::types::Extend`
// and `mbo::types::NoPrint`.
struct Comparable final : MakeExtender<"Comparable"_ts, Comparable_> {};

// Extender that injects functionality to make an `Extend`ed type add a
// `ToString` function which can be used to convert a type into a `std::string`.
//
// The `ToString` function takes an optional arg `field_options` of type
// `StringifyOptions`. This allows for instance to generate Json formatting:
//
// ```c++
// struct MyType : mbo::types::Extend<MyType> {
//   int one = 25;
//   int two = 42;
//
//   friend auto MboTypesStringifyFieldNames(const MyType& /* unused */) {
//     return std::array<std::string_view, 2>{"one", "two"};
//   }
// };
//
// const TestStruct value;
//
// value.ToString(Stringify::OptionsJson()));
// ```
//
// This default Extender is automatically available through `mb::types::Extend`.
struct Printable final : MakeExtender<"Printable"_ts, Printable_, AbslStringify> {};

// Extender that injects functionality to make an `Extend`ed type streamable.
// This allows the type to be used directly with `std::ostream`s.
//
// This default Extender is automatically available through `mb::types::Extend`.
struct Streamable final : MakeExtender<"Streamable"_ts, Streamable_, AbslStringify> {};

namespace extender_internal {
// Temporarily unused: This would allow to simplify the default wrapper combos (e.g. `Default`).
// However it also massively increases their typename lengths. Use:
//  template<typename ExtenderOrActualType>
//  using Impl : Expand<ExtenderOrActualType, ExtenderTuple>;
#if MBO_TYPES_EXTENDER_USE_EXPAND
template<typename... T>
struct Expand;

template<typename Base>
struct Expand<Base, std::tuple<>> : Base {
  using Type = Base::Type;
};

template<typename Base, typename T>
struct Expand<Base, std::tuple<T>> : ::mbo::types::types_internal::UseExtender<Base, T>::type {
  using Type = Base::Type;
};

template<typename Base, typename T, typename... U>
struct Expand<Base, std::tuple<T, U...>>
    : ::mbo::types::types_internal::UseExtender<Expand<Base, std::tuple<U...>>, T>::type {};
#endif
}  // namespace extender_internal

// The default extender is a wrapper around:
// * Streamable
// * Printable
// * Comparable
// * AbslHashable
// * AbslStringify
// In theory this could simply be written as a tuple alias (see below). However,
// that would create much longer types names that are very hard to read. So we
// use a short hand instead.
//
// ```c++
// using Default = std::tuple<AbslHashable, AbslStringify, Comparable, Printable, Streamable>;
// ```
struct Default final {
  using RequiredExtender = void;
  using ExtenderTuple = std::tuple<AbslHashable, AbslStringify, Comparable, Printable, Streamable>;

  static constexpr std::string_view GetExtenderName() { return decltype("Default"_ts)::str(); }

 private:
  // This friend is necessary to keep symbol visibility in check. But it has to
  // be either 'struct' or 'class' and thus results in `ImplT` being a struct.
  template<typename T, typename Extender>
  friend struct ::mbo::types::types_internal::UseExtender;

  // NOTE: The list of wrapped extenders needs to be in sync with `ExtenderTuple`.

  template<typename ExtenderOrActualType>
  struct Impl : Streamable_<Printable_<Comparable_<AbslHashable_<AbslStringify_<ExtenderOrActualType>>>>> {
    using Type = ExtenderOrActualType::Type;
  };
};

// This default extender is a wrapper around:
// * NOT Streamable
// * NOT Printable
// * Comparable
// * AbslHashable
// * AbslStringify
//
// In theory this could simply be written as a tuple alias (see below). However,
// that would create much longer types names that are very hard to read. So we
// use a short hand instead.
//
// ```c++
// using NoPrint = std::tuple<AbslHashable, AbslStringify, Comparable>;
// ```
struct NoPrint final {
  using RequiredExtender = void;
  using ExtenderTuple = std::tuple<AbslHashable, AbslStringify, Comparable>;

  static constexpr std::string_view GetExtenderName() { return decltype("NoPrint"_ts)::str(); }

 private:
  // This friend is necessary to keep symbol visibility in check. But it has to
  // be either 'struct' or 'class' and thus results in `ImplT` being a struct.
  template<typename T, typename Extender>
  friend struct ::mbo::types::types_internal::UseExtender;

  // NOTE: The list of wrapped extenders needs to be in sync with `ExtenderTuple`.

  template<typename ExtenderOrActualType>
  struct Impl : Comparable_<AbslHashable_<AbslStringify_<ExtenderOrActualType>>> {
    using Type = ExtenderOrActualType::Type;
  };
};

}  // namespace extender

// using namespace extender;

}  // namespace mbo::types

// Add a convenience namespace to clearly isolate just the extenders.
namespace mbo::extender {
using namespace ::mbo::types::extender;  // NOLINT(google-build-using-namespace)
}  // namespace mbo::extender

#endif  // MBO_TYPES_EXTENDER_H_
