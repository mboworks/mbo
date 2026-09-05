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

// This header implements `Stringify`.
//
#ifndef MBO_TYPES_STRINGIFY_H_
#define MBO_TYPES_STRINGIFY_H_

// IWYU pragma: private, include "mbo/types/extend.h"

#include <concepts>  // IWYU pragma: keep
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "absl/container/btree_map.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "mbo/config/require.h"
#include "mbo/types/internal/extender.h"      // IWYU pragma: keep
#include "mbo/types/internal/struct_names.h"  // IWYU pragma: keep
#include "mbo/types/optional_data_or_ref.h"
#include "mbo/types/optional_ref.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep
#include "mbo/types/tuple_extras.h"

namespace mbo::types {

struct StringifyFieldInfo;

using StringifyFieldInfoString = std::function<std::string_view(const StringifyFieldInfo&)>;

#undef MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
#if !defined(__clang__) || (__clang_major__ >= 19)
# define MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
constexpr bool kLiteralOptionalfunction = true;
#else   // !defined(__clang__) || (__clang_major__ >= 19)
constexpr bool kLiteralOptionalfunction = false;
#endif  // !defined(__clang__) || (__clang_major__ >= 19)
#ifdef MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
static_assert(__is_literal_type(std::optional<std::variant<int, const StringifyFieldInfoString>>));
#endif  // MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION

// Options that control all Stringify behavior.
//
// The objects are very large due to the very detailed ability to control behavior and override
// actual values.
//
// However, the objects support sparness, that is the data is grouped into several aspects that can
// each be unset (nullopt), a const reference or actual materialized data. That enables fast
// creation and copying beyond the size. Nonetheless, the data is large and will impact CPU caches.
// So creating the options upfront and not specializing structs as much as necessary is best.
//
// The implicit constructor create objects that are fully unset, meaning all fields are nullopt.
// The `Stringify` class provides pre-determined named constexpr factories (e.g. example 3).
//
// Construction example 1: Fully unset, which results in all defaults. Here we copy the whole object
// pretty much byte by byte.
//
// ```c++
// StringifyOptions opts;
// ```
//
// Construction example 2: Sparse with only ValueOverrides based on the default ValueOverrides by
// only initializing specific fields. We also leave all other groups unset so they refer to either
// core defaults or context dependent defaults.
//
// ```c++
// StringifyOptions opts{
//   .format{StringifyOptions::Format{
//     .message_prefix = "<",
//     .message_suffix = ">",
//   }},
// };
// ```
//
// Construction example 3: Sparse with only format overrides. Unlike before we first copy the format
// group's defaults from `Stringify::OptionsJson()` then override specifics.
//
// ```c++
// StringifyOptions opts{[]() {
//   StringifyOptions opts = Stringify::OptionsJson();
//   Format& format = opts.format.as_data();
//   format.message_prefix = "<",
//   format.message_suffix = ">\n";
//   return opts;
// }()};
// ```
struct StringifyOptions {
  struct Format {
    // Message options:
    std::string_view message_prefix;
    std::string_view message_suffix;

    std::string_view field_indent;                // Indent for fields.
    std::string_view key_value_separator = ": ";  // Separator between key and value.
    std::string_view field_separator = ", ";      // Separator between two field (in front of field).

    std::string_view pointer_prefix = "*{";   // Prefix for pointer types.
    std::string_view pointer_suffix = "}";    // Suffix for pointer types.
    std::string_view smart_ptr_prefix = "{";  // Prefix for smart pointer types.
    std::string_view smart_ptr_suffix = "}";  // Suffix for smart pointer types.
    std::string_view optional_prefix = "{";   // Prefix for optional types.
    std::string_view optional_suffix = "}";   // Suffix for optional types.

    std::string_view structure_prefix = "{";  // Prefix for object (struct/class) values.
    std::string_view structure_suffix = "}";  // Suffix for object (struct/class) values.
    std::string_view container_prefix = "{";  // Prefix for container values.
    std::string_view container_suffix = "}";  // Suffix for container values.

    std::string_view char_delim = "'";  // If empty, then use numeric value.
    std::string_view string_delim = "\"";
  };

  struct FieldControl {
    // Field options:
    bool suppress : 1 = false;           // Allows complete suppression of the field.
    bool suppress_nullptr : 1 = false;   // Allows complete suppression of nullptr field values.
    bool suppress_nullopt : 1 = false;   // Allows complete suppression of nullopt field values.
    bool suppress_disabled : 1 = false;  // Allows complete suppression of disabled fields.
    std::string_view field_disabled = "{/*MboTypesStringifyDisable*/}";  // Replacement for disabled complex field.
  };

  // Key options:
  enum class KeyMode {
    kNone,  // No keys are shows. Not even `key_value_separator` will be used.
    kNormal,
    kNumericFallback,  // If not key is known use a numeric one from the field index.
  };

  friend std::ostream& operator<<(std::ostream& os, const KeyMode& value) {
    switch (value) {
      case KeyMode::kNone: break;
      case KeyMode::kNormal: return os << "KeyMode::kNormal";
      case KeyMode::kNumericFallback: return os << "KeyMode::kNumericFallback";
    }
    return os << "KeyMode::kNone";
  }

  struct KeyControl {
    KeyMode key_mode{KeyMode::kNormal};
    std::string_view key_prefix = ".";  // Prefix to key names.
    std::string_view key_suffix;        // Suffix to key names.
  };

  struct KeyOverrides {
    // Force name for the key. This can be one of:
    // * A (literal/constexpr compatible) `std::string_view` which will be used as-is.
    // * A (literal/constexpr compatible) `const StringifyFieldInfoString*`.
    // * A (non literal) `std::optional<const StringifyFieldInfoString>`. This is wrapped in a `std::optional` because
    //   otherwise the whole struct cannot be a literal/constexpr.
#ifdef MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
    std::optional<std::variant<std::string_view, const StringifyFieldInfoString*, const StringifyFieldInfoString>>
#else   // MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
    std::optional<std::variant<std::string_view, const StringifyFieldInfoString*>>
#endif  // MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
        key_use_name;
  };

  // Value options:

  enum class EscapeMode {
    kNone,        // Values are printed as is.
    kCEscape,     // Values are C-escaped.
    kCHexEscape,  // Values are C-HEX-escaped.
  };

  friend std::ostream& operator<<(std::ostream& os, const EscapeMode& value) {
    switch (value) {
      case EscapeMode::kNone: break;
      case EscapeMode::kCEscape: return os << "EscapeMode::kCEscape";
      case EscapeMode::kCHexEscape: return os << "EscapeMode::kCHexEscape";
    }
    return os << "EscapeMode::kNone";
  }

  struct ValueControl {
    // If no suitable printing found then this option is controls the behavior:
    // - If `true` (the default), then the value is printed as is.
    // - Otherwise (`false`) all value format control is applied.
    bool other_types_direct = true;

    EscapeMode escape_mode{EscapeMode::kCEscape};

    std::string_view nullptr_t_str = "std::nullptr_t";  // Value for `nullptr_t` types.
    std::string_view nullptr_v_str = "<nullptr>";       // Value for `nullptr` values.
    std::string_view nullopt_str = "std::nullopt";      // Value for `nullopt` values.

    // Max num elements to show.
    std::size_t container_max_len = std::numeric_limits<std::size_t>::max();

    // Maximum length of value (prior to escaping).
    std::string_view::size_type str_max_length{std::string_view::npos};
    std::string_view str_cutoff_suffix = "...";  // Suffix if value gets shortened.
  };

  struct ValueOverrides {
    std::string_view replacement_str;    // Allows "redacted" for string values
    std::string_view replacement_other;  // Allows "redacted" for non string values
  };

  // Special types:

  // Handling of containers whose first type is convertible to a string.
  enum class StrKeyed {
    // No special treatment
    kNormal = 0,

    // Containers with Pairs whose `first` type is a string-like automatically
    // create objects where the keys are the field names. This is useful for
    // JSON. The types are checked with `IsPairFirstStr`.
    kFirstIsName,

    // Such containers can also be stringified in alphabetical order.
    kFirstIsNameOrdered,
  };

  struct Special {
    StrKeyed str_keyed = StrKeyed::kNormal;

    // For all other cases of pairs, their names can be overwritten.
    std::optional<std::pair<std::string_view, std::string_view>> pair_keys{{"first", "second"}};
  };

  OptionalDataOrConstRef<Format> format;
  OptionalDataOrConstRef<FieldControl> field_control;
  OptionalDataOrConstRef<KeyControl> key_control;
  OptionalDataOrConstRef<KeyOverrides> key_overrides;
  OptionalDataOrConstRef<ValueControl> value_control;
  OptionalDataOrConstRef<ValueOverrides> value_overrides;
  OptionalDataOrConstRef<Special> special;

  template<typename T>
  requires IsSameAsAnyOf<T, Format, FieldControl, KeyControl, KeyOverrides, ValueControl, ValueOverrides, Special>
  constexpr const auto& Access() const noexcept {
    if constexpr (std::same_as<T, Format>) {
      return format;
    } else if constexpr (std::same_as<T, FieldControl>) {
      return field_control;
    } else if constexpr (std::same_as<T, KeyControl>) {
      return key_control;
    } else if constexpr (std::same_as<T, KeyOverrides>) {
      return key_overrides;
    } else if constexpr (std::same_as<T, ValueControl>) {
      return value_control;
    } else if constexpr (std::same_as<T, ValueOverrides>) {
      return value_overrides;
    } else if constexpr (std::same_as<T, Special>) {
      return special;
    }
  }

  template<typename T>
  requires IsSameAsAnyOf<T, Format, FieldControl, KeyControl, KeyOverrides, ValueControl, ValueOverrides, Special>
  static constexpr std::string_view TypeName() noexcept {
    if constexpr (std::same_as<T, Format>) {
      return "Format";
    } else if constexpr (std::same_as<T, FieldControl>) {
      return "FieldControl";
    } else if constexpr (std::same_as<T, KeyControl>) {
      return "KeyControl";
    } else if constexpr (std::same_as<T, KeyOverrides>) {
      return "KeyOverrides";
    } else if constexpr (std::same_as<T, ValueControl>) {
      return "ValueControl";
    } else if constexpr (std::same_as<T, ValueOverrides>) {
      return "ValueOverrides";
    } else if constexpr (std::same_as<T, Special>) {
      return "Special";
    }
  }

  std::string DebugStr() const;

  constexpr bool AllDataSet() const noexcept {
    return format && field_control && key_control && key_overrides && value_control && value_overrides && special;
  }

  constexpr explicit operator bool() const noexcept { return AllDataSet(); }

  template<typename Opts, typename Func, typename... Args>
  requires(std::same_as<std::remove_const_t<Opts>, StringifyOptions>)
  static constexpr bool ApplyAll(Opts& opts, const Func& func, const Args&... args) {
    return func(opts.format, args...) && func(opts.field_control, args...) && func(opts.key_control, args...)
           && func(opts.key_overrides, args...) && func(opts.value_control, args...)
           && func(opts.value_overrides, args...) && func(opts.special, args...);
  }

  static constexpr StringifyOptions WithAllData(StringifyOptions&& options, const StringifyOptions& defaults = {}) {
    ApplyAll(options, [&defaults]<typename T>(T& v) constexpr {
      v.as_data(defaults.Access<typename T::value_type>().get({}));
      return true;
    });
    MBO_CONFIG_REQUIRE_DEBUG(options, "Not all data set.");
    return std::move(options);
  }

  static constexpr StringifyOptions WithAllRefs(StringifyOptions&& options, const StringifyOptions& defaults = {}) {
    ApplyAll(options, [&defaults]<typename T>(T& v) constexpr {
      if (!v.has_value()) {
        v.set_ref(defaults.Access<typename T::value_type>().get({}));
      }
      return true;
    });
    MBO_CONFIG_REQUIRE_DEBUG(options, "Not all data set.");
    return std::move(options);
  }

  static constexpr StringifyOptions WithAllRefs(
      const StringifyOptions& options,
      const StringifyOptions& defaults = {}) {
    return WithAllRefs(StringifyOptions{options}, defaults);
  }

  static_assert(__is_literal_type(Format));
  static_assert(__is_literal_type(FieldControl));
  static_assert(__is_literal_type(KeyControl));
  static_assert(__is_literal_type(KeyOverrides));
  static_assert(__is_literal_type(ValueControl));
  static_assert(__is_literal_type(ValueOverrides));
  static_assert(__is_literal_type(Special));
};

#if defined(__clang__) || defined(__GNUC__)
static_assert(sizeof(StringifyOptions) <= 700);  // NOLINT(*-magic-numbers)
static_assert(__is_literal_type(StringifyOptions));
#endif  // defined(__clang__) || defined(__GNUC__)

struct StringifyFieldOptions {
  constexpr ~StringifyFieldOptions() noexcept = default;

  template<typename U>
  StringifyFieldOptions(U&&) = delete;  // NOLINT(*-missing-std-forward,*-forwarding-reference-overload)
  template<typename U, typename V>
  StringifyFieldOptions(U&&, V&&) = delete;  // NOLINT(*-missing-std-forward,*-forwarding-reference-overload)

  // NOLINTNEXTLINE(*-explicit-*): Implicit conversion in this direction is pretty intended.
  constexpr StringifyFieldOptions(const StringifyOptions& both) : outer(both), inner(both) {}

  // NOLINTNEXTLINE(*-swappable-parameters)
  constexpr StringifyFieldOptions(const StringifyOptions& outer, const StringifyOptions& inner) noexcept
      : outer(outer), inner(inner) {}

  constexpr StringifyFieldOptions(const StringifyFieldOptions& other) noexcept = default;
  constexpr StringifyFieldOptions& operator=(const StringifyFieldOptions& other) = delete;
  constexpr StringifyFieldOptions(StringifyFieldOptions&& other) noexcept = default;
  constexpr StringifyFieldOptions& operator=(StringifyFieldOptions&& other) = delete;

  std::string DebugStr() const;

  constexpr bool AllDataSet() const noexcept { return outer.AllDataSet() && inner.AllDataSet(); }

  constexpr explicit operator bool() const noexcept { return AllDataSet(); }

  constexpr StringifyFieldOptions ToInner() const noexcept { return StringifyFieldOptions{inner, inner}; }

  template<typename T = StringifyOptions>
  constexpr const auto& Outer() const noexcept {
    if constexpr (std::same_as<T, StringifyOptions>) {
      return outer;
    } else {
      return outer.Access<T>().value();
    }
  }

  template<typename T = StringifyOptions>
  constexpr const auto& Inner() const noexcept {
    if constexpr (std::same_as<T, StringifyOptions>) {
      return inner;
    } else {
      return inner.Access<T>().value();
    }
  }

  const StringifyOptions& outer;
  const StringifyOptions& inner;
};

static_assert(sizeof(StringifyFieldOptions) == 2 * sizeof(void*));

// Type used by
struct StringifyFieldInfo {
  const StringifyFieldOptions options;  // BY VALUE
  std::size_t idx;
  std::string_view name;
};

// Control for overall use a Stringify. This controls root options for messages.
// Note that `root_prefix` and `root_suffix` are streamed without `root_indent`.
// This means in particular, that is the first line of the output should already be indented, then
// `root_prefix` should end in `root_indent`.
struct StringifyRootOptions {
  std::string_view root_prefix;  // First line prefix - not using root_indent - before message_prefix.
  std::string_view root_suffix;  // Last line suffix - not using root_indent - after message_suffix.
  std::string_view root_indent;
};

template<typename T>
concept IsFieldNameContainer = requires(const T& field_names) {
  { std::size(field_names) } -> std::equality_comparable_with<std::size_t>;
  { std::data(field_names)[1] } -> std::convertible_to<std::string_view>;
};

// Whether Stringify should suppress field names.
template<typename T>
concept HasMboTypesStringifyDoNotPrintFieldNames =
    requires { typename std::remove_cvref_t<T>::MboTypesStringifyDoNotPrintFieldNames; };

// This breaks `MboTypesStringifyOptions` lookup within this namespace.
void MboTypesStringifyOptions();  // Has no implementation!

// Identify presence for a `Stringify` extender API extension point `MboTypesStringifyOptions`.
//
// That extension point can be used to perform fine grained control of field printing/streaming.
//
// NOTE: Unlike `MboTypesStringifyFieldNames` this extension point cannot deal with
// `std::string_view` members referencing temp objects such as `std::string` since life-time
// extension CANNOT be applied as we always go via the actual `std::strig_view` fields.
//
// The return value can be one of:
// * `FieldOptions`
// * `const FieldOptions&`
// * `StringifyOptions`
// * `const StringifyOptions&`
//
// Internally we always use `FieldOptions` by reference. If the function returns by value, then the
// object will be cached locally. If `StringifyOptions` are provided then they will be extended to
// `FieldOptions` as both `outer` and `inner`.
//
// The `inner` component is used for complex fields. This allows to format say a container itself
// different from its values.
//
// The function receives the following parameters:
// * `const T&`: The object itself - so this is an ADL solution.
// * `const StringifyFieldInfo&`: Information about options to use and the field they apply to:
//    * The `const FieldOptions& options`: the current (possibly root) options.
//    * The `std::size_t`:                 the index of the field.
//    * The `std::string_view`:            the name of the field.
//
// The implementation must therefore match one of the following signatures:
//
// ```
// friend const FieldOptions& MboTypesStringifyOptions(const T&, const const StringifyFieldInfo&)
// friend FieldOptions MboTypesStringifyOptions(const T&, const StringifyFieldInfo&)
// friend const StringifyOptions& MboTypesStringifyOptions(const T&, const const StringifyFieldInfo&)
// friend StringifyOptions MboTypesStringifyOptions(const T&, const StringifyFieldInfo&)
// ```
//
// If the return type is a const reference and `AllDataSet` returns true, then the value is taken
// as-is and further stringify operations will use that value. Otherwise, the returned object will
// be expanded by `WithAllRefs` and cached.
//
// Note that caching is fairly expensive since both supported object types are large.
template<typename T>
concept IsValidMboTypesStringifyOptionsResultType =
    IsSameAsAnyOf<T, StringifyOptions, const StringifyOptions&, StringifyFieldOptions, const StringifyFieldOptions&>;

template<typename T>
concept HasMboTypesStringifyOptions = requires(const T& v, const StringifyFieldInfo& opts) {
  { MboTypesStringifyOptions(v, opts) } -> IsValidMboTypesStringifyOptionsResultType;
};

namespace types_internal {

template<typename T>
concept BadMboTypesStringifyOptions_1 = requires(T arg1) {
  { MboTypesStringifyOptions(arg1) };
};

template<typename T>
concept BadMboTypesStringifyOptions_2 = requires(T arg1, AnyOtherType<StringifyFieldInfo> arg2) {
  { MboTypesStringifyOptions(arg1, arg2) };
};

template<typename T>
concept BadMboTypesStringifyOptions_3 = requires(T arg1, AnyType arg2, AnyType arg3) {
  { MboTypesStringifyOptions(arg1, arg2, arg3) };
};

template<typename T>
concept BadMboTypesStringifyOptions_4 = requires(T arg1, AnyType arg2, AnyType arg3, AnyType arg4) {
  { MboTypesStringifyOptions(arg1, arg2, arg3, arg4) };
};

template<typename T>
concept BadMboTypesStringifyOptions_5 = requires(T arg1, AnyType arg2, AnyType arg3, AnyType arg4, AnyType arg5) {
  { MboTypesStringifyOptions(arg1, arg2, arg3, arg4, arg5) };
};

template<typename T>
concept BadMboTypesStringifyOptions_6 =
    requires(T arg1, AnyType arg2, AnyType arg3, AnyType arg4, AnyType arg5, AnyType arg6) {
      { MboTypesStringifyOptions(arg1, arg2, arg3, arg4, arg5, arg6) };
    };

}  // namespace types_internal

// Concept that identifies bad `MboTypesStringifyOptions` signatures.
template<typename T>
concept BadMboTypesStringifyOptions = (  //
    types_internal::BadMboTypesStringifyOptions_1<T> || types_internal::BadMboTypesStringifyOptions_2<T>
    || types_internal::BadMboTypesStringifyOptions_3<T> || types_internal::BadMboTypesStringifyOptions_4<T>
    || types_internal::BadMboTypesStringifyOptions_5<T>);

// This breaks `MboTypesStringifyFieldNames` lookup within this namespace.
void MboTypesStringifyFieldNames();  // Has no implementation!

// Extension API point that disables printing for tagged types.
//
// Use this sparingly because it actually completely prevents any output from
// such types. If a field of a type tagged this way receives `StringifyOptions`
// with `field_suppress_disabled` set true, then there will be no output for the
// field at all. Otherwise the field name will be shown with a special marker
// `/* MboTypesStringifyDisable */`.
//
// If `Stringify` is used on a type tagged this way and the default options have
// `field_suppress_disabled` then there for consistency there will be no output
// at all.
//
// NOTE: The presence of a type named `MboTypesStringifyDisable` does not by
// itself enable `Stringify` for that type. However it can break any automatic
// call-chain and be used in parallel to `MboTypesStringifySupport` or CRTP
// types using `mbo::types::Extend`.
template<typename T>
concept HasMboTypesStringifyDisable = requires { typename T::MboTypesStringifyDisable; };

// Identify presence for `AbslStringify` extender API extension point
// `MboTypesStringifyFieldNames`.
//
// That extension point can be used to provide field names in cases where the
// compiler does not provide or where they should be different when printed.
//
// The function must comply with the following requirements:
// * The return value must be usable in:
//   * `std::size(result)`, and
//   * `std::data(result)`.
// * The values must be convertible to a `std::string_view`.
// * Temp containers of temp `std::string` values are admissible since life-time
//   extension is being applied.
template<typename T>
concept HasMboTypesStringifyFieldNames =
    requires(const T& v) { requires IsFieldNameContainer<decltype(MboTypesStringifyFieldNames(v))>; };

// This breaks `MboStringifyValueAccess` lookup within this namespace.
void MboTypesStringifyValueAccess();  // Has no implementation!

// Some types function as a value holder, they should not be handled by `Stringify` directly.
// Instead they will use ADL API extension point `MboTypesStringifyValueAccess`.
// When implemented, then the value returned will be used instead. That value can be any type that
// can be handled as either a Stringify supported value-type or container-type.
template<typename T>
concept HasMboTypesStringifyValueAccess = !IsVariant<T> && requires(const T& v, const StringifyFieldOptions& options) {
  { MboTypesStringifyValueAccess(v, options) };  // The return value does not matter.
};

// Whether Stringify should automatically take affect if `T` is used as a sub field in a Stringify invocation.
// Otherwise the subfield needs its own support for printing (e.g. Abseil stringify support).
//
// Note that this takes precedence and thus disables Abseil stringify support in `Stringify`.
//
// This gets triggered by:
// * Presence of a type named `MboTypesStringifySupport`,
// * `T` qualifies for `HasMboTypesStringifyDoNotPrintFieldNames`,
// * `T` qualifies for `HasMboTypesStringifyFieldNames`, or
// * `T` qualified for `HasMboTypesStringifyOptions`,
template<typename T>
concept HasMboTypesStringifySupport =               //
    HasMboTypesStringifyDoNotPrintFieldNames<T> ||  // type `MboTypesStringifyDoNotPrintFieldNames`
    HasMboTypesStringifyFieldNames<T> ||            // function `MboTypesStringifyFieldNames`
    HasMboTypesStringifyOptions<T> ||               // function `MboTypesStringifyOptions`
    HasMboTypesStringifyValueAccess<T> ||           // function `MboTypesStringifyValueAccess`
    requires { typename std::remove_cvref_t<T>::MboTypesStringifySupport; };

enum class StringifyNameHandling {
  kOverwrite = 0,  // Use the provided names to override automatically determined names.
  kVerify = 1,     // Verify that the provided name matches the determined if possible.
};

template<typename T, typename ObjectType = types_internal::AnyType>
concept CanProduceStringifyOptions = std::is_assignable_v<
    StringifyOptions,
    std::invoke_result_t<T, ObjectType, std::size_t, std::string_view, const StringifyOptions&>>;

// Concept that verifies whether `T` can be used to construct (or assign to) an
// `StringifyOptions`.
//
// The actual `ObjectType` might not be evaluated immediately. This happens when
// an adapter does not know the type yet if it applies type-erasure techniques
// like `WithFieldNames` does. However, all concrete calls will verify their
// factual object type.
//
// Read more about producing `StringifyOptions` in the documentation for
// `MboTypesStringifyOptions` and `HasMboTypesStringifyOptions`.
template<typename T, typename ObjectType = types_internal::AnyType>
concept IsOrCanProduceStringifyOptions =
    std::is_convertible_v<T, StringifyOptions> || CanProduceStringifyOptions<T, ObjectType>;

// Concept that verifies whether `T` has a static `MboTypesStringifyConvert`
// member function callable as:
//     `MboTypesStringifyConvert(const T& obj, std::size_t idx, const V& val)`
// Where `obj` is the owning object, `idx` is the field index and `val` is the
// actual field value.
template<typename T, typename V>
concept HasMboTypesStringifyConvert = requires {
  { T::MboTypesStringifyConvert(std::size_t{}, std::declval<const T&>(), std::declval<const V&>()) };
};

// Class `Stringify` implements the conversion of any aggregate into a string.
class Stringify {
 public:
  enum class OutputMode {
    kDefault,
    kCpp,
    kCppPretty,
    kJson,
    kJsonLine,
    kJsonPretty,
  };

  friend constexpr std::ostream& operator<<(std::ostream& os, const OutputMode& value) {
    switch (value) {
      case OutputMode::kDefault: break;
      case OutputMode::kCpp: return os << "OutpuMode::kCpp";
      case OutputMode::kCppPretty: return os << "OutpuMode::kCppPretty";
      case OutputMode::kJson: return os << "OutpuMode::kJson";
      case OutputMode::kJsonLine: return os << "OutpuMode::kJsonLine";
      case OutputMode::kJsonPretty: return os << "OutpuMode::kJsonPretty";
    }
    return os << "OutpuMode::kDefault";
  }

  static constexpr const StringifyOptions& OptionsAs(OutputMode mode) noexcept {
    switch (mode) {
      case OutputMode::kDefault: break;
      case OutputMode::kCpp: return OptionsCpp();
      case OutputMode::kCppPretty: return OptionsCppPretty();
      case OutputMode::kJson: return OptionsJson();
      case OutputMode::kJsonLine: return OptionsJsonLine();
      case OutputMode::kJsonPretty: return OptionsJsonPretty();
    }
    return OptionsDefault();
  }

  // Arbitrary default value.
  static constexpr const StringifyOptions& OptionsDefault() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsDefaults.AllDataSet(), "Not all data set: ") << kOptionsDefaults.DebugStr();
    return kOptionsDefaults;
  }

  // Formatting control that disables all fields.
  static constexpr const StringifyOptions& OptionsDisabled() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsDisabled.AllDataSet(), "Not all data set: ") << kOptionsDisabled.DebugStr();
    return kOptionsDisabled;
  }

  // Formatting control that mostly produces (very dense) C++ code.
  static constexpr const StringifyOptions& OptionsCpp() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsCpp.AllDataSet(), "Not all data set: ") << kOptionsCpp.DebugStr();
    return kOptionsCpp;
  }

  // Formatting control that mostly produces multi-line formatted C++ code.
  static constexpr const StringifyOptions& OptionsCppPretty() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsCppPretty.AllDataSet(), "Not all data set: ") << kOptionsCppPretty.DebugStr();
    return kOptionsCppPretty;
  }

  // Formatting control that mostly produces (very dense) JSON data (new-line terminated).
  //
  // NOTE: JSON data requires field names. So unless Clang is used only with
  // types that support field names, they must be provided by an extension API
  // point: `MboTypesStringifyFieldNames` or `MboTypesStringifyOptions`, the
  // latter possibly with the `StringifyWithFieldNames` adapter. Alternatively,
  // numeric field names will be generated as a last resort.
  static constexpr const StringifyOptions& OptionsJson() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsJson.AllDataSet(), "Not all data set: ") << kOptionsJson.DebugStr();
    return kOptionsJson;
  }

  // Formatting control that mostly produces single-line formatted JSON data (new-line terminated).
  static constexpr const StringifyOptions& OptionsJsonLine() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsJsonLine.AllDataSet(), "Not all data set: ") << kOptionsJsonLine.DebugStr();
    return kOptionsJsonLine;
  }

  // Formatting control that mostly produces multi-line formatted JSON data (new-line terminated).
  static constexpr const StringifyOptions& OptionsJsonPretty() noexcept {
    MBO_CONFIG_REQUIRE(kOptionsJsonPretty.AllDataSet(), "Not all data set: ") << kOptionsJsonPretty.DebugStr();
    return kOptionsJsonPretty;
  }

  // Prevent passing in temporaries to prevent dangling issues.
  static Stringify AsCpp(const StringifyRootOptions&&) = delete;
  static Stringify AsCppPretty(const StringifyRootOptions&&) = delete;
  static Stringify AsJson(const StringifyRootOptions&&) = delete;
  static Stringify AsJsonLine(const StringifyRootOptions&&) = delete;
  static Stringify AsJsonPretty(const StringifyRootOptions&&) = delete;

  static constexpr Stringify AsCpp(const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept {
    return Stringify(OptionsCpp(), root_options);
  }

  static constexpr Stringify AsCppPretty(const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept {
    return Stringify(OptionsCppPretty(), root_options);
  }

  static constexpr Stringify AsJson(const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept {
    return Stringify(OptionsJson(), root_options);
  }

  static constexpr Stringify AsJsonLine(const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept {
    return Stringify(OptionsJsonLine(), root_options);
  }

  static constexpr Stringify AsJsonPretty(const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept {
    return Stringify(OptionsJsonPretty(), root_options);
  }

  explicit Stringify(const StringifyOptions&&) = delete;
  explicit Stringify(const StringifyOptions&&, const StringifyRootOptions&&) = delete;
  explicit Stringify(const StringifyOptions&, const StringifyRootOptions&&) = delete;
  explicit Stringify(const StringifyOptions&&, const StringifyRootOptions&) = delete;
  explicit Stringify(OutputMode, StringifyRootOptions&&) = delete;  // NOLINT(*)

  explicit constexpr Stringify(
      const StringifyOptions& default_options = OptionsDefault(),
      const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept
      : default_root_options_(root_options), default_field_options_(default_options) {
    MBO_CONFIG_REQUIRE(default_field_options_.AllDataSet(), "Not all data set: ") << default_field_options_.DebugStr();
  }

  explicit constexpr Stringify(
      OutputMode output_mode,
      const StringifyRootOptions& root_options = kRootOptionDefaults) noexcept
      : Stringify(OptionsAs(output_mode), root_options) {}

  template<typename T>
  requires(IsAggregate<T> || IsEmptyType<T> || IsStringKeyedContainer<T>)
  std::string ToString(const T& value, OptionalRef<const StringifyRootOptions> opt_root_options = {}) const {
    std::ostringstream os;
    Stream<T>(os, value, opt_root_options);
    return os.str();
  }

  template<typename T>
  requires(IsAggregate<T> || IsEmptyType<T> || IsStringKeyedContainer<T>)
  void Stream(std::ostream& out, const T& value, OptionalRef<const StringifyRootOptions> opt_root_options = {}) const {
    const StringifyRootOptions& root_options = opt_root_options ? *opt_root_options : default_root_options_;
    OStream os(!default_field_options_.outer.format.get({}).field_indent.empty(), out, root_options);
    os << root_options.root_prefix;
    os << default_field_options_.outer.format.get({}).message_prefix;
    StreamImpl(os, default_field_options_, value);
    os << default_field_options_.outer.format.get({}).message_suffix;
    os << root_options.root_suffix;
  }

  const StringifyRootOptions& DebugDefaultRootOptions() const noexcept { return default_root_options_; }

  const StringifyFieldOptions& DebugDefaultFieldOptions() const noexcept { return default_field_options_; }

 private:
  using SO = StringifyOptions;
  using SFO = StringifyFieldOptions;

  class OStream {
   public:
    ~OStream() noexcept = default;
    OStream() = delete;
    OStream(const OStream&) = delete;
    OStream& operator=(const OStream&) = delete;
    OStream(OStream&&) = delete;
    OStream& operator=(OStream&&) = delete;

    OStream(bool enable_indent, std::ostream& os, const StringifyRootOptions& root_options)
        : enable_indent_(enable_indent), os_(os) {
      RootIndent(root_options);
    }

    template<typename T>
    OStream& operator<<(const T& v) {
      os_ << v;
      return *this;
    }

    void IncContainer(const StringifyOptions::Format& format) {
      os_ << format.container_prefix;
      level_.push_back(format.field_indent);
      enable_indent_ = !format.field_indent.empty();
    }

    void DecContainer(const StringifyOptions::Format& format) {
      level_.pop_back();
      StreamIndent();
      os_ << format.container_suffix;
      enable_indent_ = level_.empty() || !level_.back().empty();
    }

    void IncStruct(const StringifyOptions::Format& format) {
      os_ << format.structure_prefix;
      level_.push_back(format.field_indent);
      enable_indent_ = !format.field_indent.empty();
    }

    void DecStruct(const StringifyOptions::Format& format) {
      level_.pop_back();
      StreamIndent();
      os_ << format.structure_suffix;
      enable_indent_ = level_.empty() || !level_.back().empty();
    }

    void StreamIndent() {
      if (!enable_indent_) {
        return;
      }
      os_ << "\n";
      for (const std::string_view level : level_) {
        os_ << level;
      }
    }

   private:
    void RootIndent(const StringifyRootOptions& root_options) {
      if (!root_options.root_indent.empty()) {
        level_.push_back(root_options.root_indent);
      }
    }

    bool enable_indent_ = true;
    std::ostream& os_;
    std::vector<std::string_view> level_;
  };

  template<IsAggregate T>
  requires(!IsEmptyType<T>)
  void StreamImpl(OStream& os, const StringifyFieldOptions& options, const T& value) const {
    // It is not allowed to deny field name printing but provide filed names.
    static_assert(!(HasMboTypesStringifyDoNotPrintFieldNames<T> && HasMboTypesStringifyFieldNames<T>));
    StreamFieldsImpl<T>(os, options, value);
  }

  template<IsEmptyType T>
  void StreamImpl(OStream& os, const StringifyFieldOptions& options, const T& value) const {
    StreamFieldsImpl<T>(os, options, value);
  }

  template<IsStringKeyedContainer T>
  void StreamImpl(OStream& os, const StringifyFieldOptions& options, const T& value) const {
    StreamValue(os, options, value, true);
  }

  template<typename T>
  requires(HasMboTypesStringifyDisable<T>)
  void StreamFieldsImpl(
      OStream& os,
      const StringifyFieldOptions& options,
      const T& /*value*/,
      bool /*allow_field_names*/ = !HasMboTypesStringifyDoNotPrintFieldNames<T>) const {
    const SO::FieldControl& field_control = *options.outer.field_control;
    if (!field_control.suppress_disabled) {
      os << field_control.field_disabled;
    }
    // Otherwise no output! Streaming `"{}"` would also be possible, but not streaming anything is
    // is inline with what field streaming does.
  }

  template<typename T, typename V>
  requires(HasMboTypesStringifyConvert<T, V>)
  static constexpr auto TsValue(std::size_t field_index, const T& object, const V& value) {
    return T::MboTypesStringifyConvert(field_index, object, value);
  }

  template<typename T, typename V>
  requires(!HasMboTypesStringifyConvert<T, V>)
  static constexpr const V& TsValue(std::size_t /*field_index*/, const T& /*object*/, const V& value) {
    return value;  // NOLINT(bugprone-return-const-ref-from-parameter)
  }

  template<typename T>
  requires(!HasMboTypesStringifyDisable<T>)
  void StreamFieldsImpl(
      OStream& os,
      const StringifyFieldOptions& options,
      const T& value,
      bool use_field_names = !HasMboTypesStringifyDoNotPrintFieldNames<T>) const {
    use_field_names |= options.outer.key_control->key_mode == StringifyOptions::KeyMode::kNumericFallback;
    bool use_sep = false;
    const auto& field_names = GetFieldNames(value);
    std::apply(
        [&](const auto&... fields) {
          const SO::Format& format = *options.outer.format;
          os.IncStruct(format);
          // `idx` is incremented by the fold below, which expands to nothing for a
          // zero-field struct - and it is those instantiations that suggest `const`.
          // NOLINTNEXTLINE(misc-const-correctness)
          std::size_t idx{0};
          // NOLINTNEXTLINE(readability-suspicious-call-argument)
          ((StreamField(os, options, use_sep, value, TsValue<T>(idx, value, fields), idx, field_names, use_field_names),
            ++idx),
           ...);
          os.DecStruct(format);
        },
        [&value]() {
          if constexpr (types_internal::IsExtended<T>) {
            return [&value] { return value.ToTuple(); };
          } else if constexpr (IsPair<T> || IsTuple<T>) {
            return [&value]() -> const T& {
              return value;  // works for pair and tuple.
            };
          } else {
            return [&value] { return StructToTuple(value); };
          }
        }()());
  }

  template<typename T>
  static auto GetFieldNames(const T& value) {
    if constexpr (HasMboTypesStringifyDoNotPrintFieldNames<T>) {
      return std::array<std::string_view, 0>{};
    } else if constexpr (HasMboTypesStringifyFieldNames<T>) {
      return MboTypesStringifyFieldNames(value);
    } else {
      // Works for both constexpr and static/const aka compile-time and run-time cases.
      return types_internal::GetFieldNames<T>();
    }
  }

  enum class SpecialFieldValue {
    kNoSuppress = 0,
    kNormal,
    kIsNullptr,
    kIsNullopt,
    kStringifyDisabled,
  };

  template<typename Field>
  // LCOV_MERGE_FUNC_LINE: repeated for every field type.
  static SpecialFieldValue IsSpecial(const Field& field) {
    using RawField = std::remove_cvref_t<Field>;
    (void)field;
    if constexpr (std::is_pointer_v<RawField> || IsSmartPtr<RawField>) {
      return field == nullptr ? SpecialFieldValue::kIsNullptr : SpecialFieldValue::kNoSuppress;
    } else if constexpr (std::is_same_v<RawField, std::nullptr_t>) {
      return SpecialFieldValue::kIsNullptr;
    } else if constexpr (IsOptional<RawField>) {
      return field == std::nullopt ? SpecialFieldValue::kIsNullopt : SpecialFieldValue::kNoSuppress;
    } else if constexpr (HasMboTypesStringifyDisable<RawField>) {
      return SpecialFieldValue::kStringifyDisabled;
    }
    return SpecialFieldValue::kNormal;
  }

  template<typename T, typename Field, typename FieldNames>
  // LCOV_MERGE_FUNC_LINE: repeated for every aggregate and field type.
  void StreamField(
      OStream& os,
      const StringifyFieldOptions& outer_options,
      bool& use_seperator,
      const T& value,
      const Field& field,
      std::size_t idx,
      const FieldNames& field_names,
      bool allow_field_names) const {
    std::string_view field_name;
    if (allow_field_names && idx < std::size(field_names)) {
      field_name = std::data(field_names)[idx];
    }
    static_assert(!BadMboTypesStringifyOptions<T>, "Provided `BadMboTypesStringifyOptions` has a bad signature.");
    // Custom can be very expensive on the stack, so only include that call-path if necessary.
    if constexpr (HasMboTypesStringifyOptions<T>) {
      StreamFieldCustom(
          os, outer_options, use_seperator, field, idx, field_name, allow_field_names,
          MboTypesStringifyOptions(value, {.options = outer_options, .idx = idx, .name = field_name}));
    } else {
      // No customization
      const StringifyFieldInfo field_info{.options{default_field_options_}, .idx = idx, .name = field_name};
      StreamFieldDo(os, outer_options, field_info, use_seperator, field, allow_field_names);
    }
  }

  template<typename Field, typename CustomT>
  void StreamFieldCustom(
      OStream& os,
      const StringifyFieldOptions& outer_options,
      bool& use_seperator,
      const Field& field,
      std::size_t idx,
      std::string_view field_name,
      bool allow_field_names,
      CustomT&& custom) const {
    // The custom handling is expensive only if we have to cache the data. So hand that over to specific functions.
    if constexpr (std::same_as<std::remove_cvref_t<CustomT>, StringifyFieldOptions>) {
      if constexpr (std::is_reference_v<CustomT>) {
        if (custom.AllDataSet()) {
          // No caching needed for `custom` since we use the reference as is.
          const StringifyFieldInfo field_info{.options{std::forward<CustomT>(custom)}, .idx = idx, .name = field_name};
          StreamFieldDo(os, outer_options, field_info, use_seperator, field, allow_field_names);
          return;
        }
      }
      if (&custom.outer == &custom.inner) {
        StreamFieldCustomCache(  // Using StringifyOptions
            os, outer_options, custom.outer, use_seperator, field, idx, field_name, allow_field_names);
      } else {
        StreamFieldCustomCache(  // Using StringifyFieldOptions
            os, outer_options, std::forward<CustomT>(custom), use_seperator, field, idx, field_name, allow_field_names);
      }
    } else {
      if constexpr (std::is_reference_v<CustomT>) {
        if (custom.AllDataSet()) {
          // No caching needed for `custom` since we use the reference as is.
          const StringifyFieldInfo field_info{.options{custom}, .idx = idx, .name = field_name};
          StreamFieldDo(os, outer_options, field_info, use_seperator, field, allow_field_names);
          return;
        }
      }
      StreamFieldCustomCache(
          os, outer_options, std::forward<CustomT>(custom), use_seperator, field, idx, field_name, allow_field_names);
    }
  }

  template<typename Field>
  // LCOV_MERGE_FUNC_LINE: repeated for every field type.
  void StreamFieldCustomCache(
      OStream& os,
      const StringifyFieldOptions& outer_options,
      const StringifyFieldOptions& custom,
      bool& use_seperator,
      const Field& field,
      std::size_t idx,
      std::string_view field_name,
      bool allow_field_names) const {
    // VERY EXPENSIVE: close to 2k on the stack
    const StringifyOptions inner_outer = StringifyOptions::WithAllRefs(custom.outer, outer_options.inner);
    const StringifyOptions inner_inner = StringifyOptions::WithAllRefs(custom.inner, outer_options.inner);
    const StringifyFieldInfo field_info{.options{inner_outer, inner_inner}, .idx = idx, .name = field_name};
    StreamFieldDo(os, outer_options, field_info, use_seperator, field, allow_field_names);
  }

  template<typename Field, std::same_as<StringifyOptions> CustomT>
  void StreamFieldCustomCache(
      OStream& os,
      const StringifyFieldOptions& outer_options,
      CustomT&& custom,
      bool& use_seperator,
      const Field& field,
      std::size_t idx,
      std::string_view field_name,
      bool allow_field_names) const {
    // VERY EXPENSIVE: close to 1k on the stack
    const StringifyOptions cached{SO::WithAllRefs(std::forward<CustomT>(custom), outer_options.inner)};
    const StringifyFieldInfo field_info{.options{cached}, .idx = idx, .name = field_name};
    StreamFieldDo(os, outer_options, field_info, use_seperator, field, allow_field_names);
  }

  template<typename Field>
  // LCOV_MERGE_FUNC_LINE: repeated for every field type.
  void StreamFieldDo(
      OStream& os,
      const StringifyFieldOptions& outer_options,
      const StringifyFieldInfo& field_info,
      bool& use_seperator,
      const Field& field,
      bool allow_field_names) const {
    const SpecialFieldValue is_special = IsSpecial(field);
    if (!StreamFieldKeyEnabled(field_info.options.outer, is_special)) {
      return;
    }
    if (use_seperator) {
      os << outer_options.outer.format->field_separator;
    }
    use_seperator = true;
    os.StreamIndent();
    if (allow_field_names) {
      StreamFieldName(os, field_info);
    }
    StreamValue(os, field_info.options, field, allow_field_names);
  }

  static bool StreamFieldKeyEnabled(const StringifyOptions& options, SpecialFieldValue is_special) {
    switch (is_special) {
      case SpecialFieldValue::kNoSuppress: break;
      case SpecialFieldValue::kNormal:
        if (options.field_control->suppress) {
          return false;
        }
        break;
      case SpecialFieldValue::kIsNullptr:
        if (options.field_control->suppress_nullptr) {
          return false;
        }
        break;
      case SpecialFieldValue::kIsNullopt:
        if (options.field_control->suppress_nullopt) {
          return false;
        }
        break;
      case SpecialFieldValue::kStringifyDisabled:
        if (options.field_control->suppress_disabled) {
          return false;
        }
        break;
    }
    return true;
  }

  static void StreamFieldName(OStream& os, const StringifyFieldInfo& field, bool allow_key_override = true) {
    const SO::Format& format = *field.options.outer.format;
    const SO::KeyControl& key_control = *field.options.outer.key_control;
    switch (key_control.key_mode) {
      case StringifyOptions::KeyMode::kNone: return;
      case StringifyOptions::KeyMode::kNormal:
      case StringifyOptions::KeyMode::kNumericFallback: break;
    }
    std::string_view field_name;
    if (allow_key_override && field.options.outer.key_overrides->key_use_name.has_value()) {
      const auto& key_use_name = *field.options.outer.key_overrides->key_use_name;
      if (std::holds_alternative<std::string_view>(key_use_name)) {
        field_name = std::get<std::string_view>(key_use_name);
      } else if (std::holds_alternative<const StringifyFieldInfoString*>(key_use_name)) {
        const auto* func = std::get<const StringifyFieldInfoString*>(key_use_name);
        if (func != nullptr && *func) {
          field_name = (*func)(field);
        }
#ifdef MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
      } else if (std::holds_alternative<const StringifyFieldInfoString>(key_use_name)) {
        const auto& func = std::get<const StringifyFieldInfoString>(key_use_name);
        if (func) {
          field_name = func(field);
        }
#endif  // MBO_TYPES_STRINGIFY_LITERAL_OPTIONAL_FUNCTION
      } else {
        ABSL_LOG(FATAL) << "Bad field name override: variant type not handled.";
      }
    }
    if (field_name.empty()) {
      field_name = field.name;
    }
    if (!field_name.empty()) {
      os << key_control.key_prefix << field_name << key_control.key_suffix << format.key_value_separator;
    } else if (key_control.key_mode == StringifyOptions::KeyMode::kNumericFallback) {
      os << key_control.key_prefix << field.idx << key_control.key_suffix << format.key_value_separator;
    }
  }

  template<IsStringKeyedContainer C>
  void StreamValue(OStream& os, const StringifyFieldOptions& options, const C& vs, bool allow_field_names) const {
    switch (options.outer.special->str_keyed) {
      case SO::StrKeyed::kNormal: break;
      case SO::StrKeyed::kFirstIsName:
        // Each pair element of the container `vs` is an element whose key is the `first` member and
        // whose value is the `second` member.
        StreamStringKeyedContainer(os, options, vs, allow_field_names);
        return;
      case SO::StrKeyed::kFirstIsNameOrdered: {
        absl::btree_map<std::string_view, std::reference_wrapper<const typename C::value_type::second_type>> ordered;
        std::size_t index = 0;
        for (const auto& [key, val] : vs) {
          if (++index > options.outer.value_control->container_max_len) {
            break;
          }
          ordered.emplace(key, val);
        }
        StreamStringKeyedContainer(os, options, ordered, allow_field_names);
        return;
      }
    }
    StreamContainer(os, options, vs, allow_field_names);
  }

  template<IsStringKeyedContainer C>
  // LCOV_MERGE_FUNC_LINE: repeated for every supported container type.
  void StreamStringKeyedContainer(
      OStream& os,
      const StringifyFieldOptions& options,
      const C& vs,
      bool allow_field_names) const {
    const SO::Format& format = *options.outer.format;
    os.IncStruct(format);
    std::string_view sep;
    std::size_t index = 0;
    for (const auto& v : vs) {
      if (index >= options.outer.value_control->container_max_len) {
        break;
      }
      os << sep;
      os.StreamIndent();
      sep = options.outer.format->field_separator;
      if (allow_field_names) {
        const StringifyFieldInfo field_info{.options = options, .idx = index, .name = v.first};
        StreamFieldName(os, field_info, /*allow_key_override=*/false);
      }
      StreamValue(os, options.ToInner(), v.second, allow_field_names);
      ++index;
    }
    os.DecStruct(format);
  }

  template<typename C>
  requires(
      ::mbo::types::ContainerIsForwardIteratable<C>
      && !mbo::types::IsPairFirstStr<std::remove_cvref_t<typename C::value_type>>
      && !std::convertible_to<C, std::string_view> && !HasMboTypesStringifyValueAccess<C>)
  void StreamValue(OStream& os, const StringifyFieldOptions& options, const C& vs, bool allow_field_names) const {
    StreamContainer(os, options, vs, allow_field_names);
  }

  template<typename C>
  requires(ContainerIsForwardIteratable<C>)
  // LCOV_MERGE_FUNC_LINE: repeated for every supported container type.
  void StreamContainer(OStream& os, const StringifyFieldOptions& options, const C& vs, bool allow_field_names) const {
    const SO::Format& format = *options.outer.format;
    os.IncContainer(format);
    std::string_view sep;
    std::size_t index = 0;
    for (const auto& v : vs) {
      if (++index > options.outer.value_control->container_max_len) {
        break;
      }
      os << sep;
      os.StreamIndent();
      sep = options.outer.format->field_separator;
      StreamValue(os, options.ToInner(), v, allow_field_names);
    }
    os.DecContainer(format);
  }

  template<IsVariant Variant>
  void StreamValue(OStream& os, const StringifyFieldOptions& options, const Variant& value, bool /*allow_field_names*/)
      const {
    std::visit(
        // LCOV_MERGE_FUNC_LINE: repeated for every variant alternative.
        [&]<typename ArgT>(const ArgT& arg) {
          if constexpr (IsAggregate<ArgT> || IsStringKeyedContainer<ArgT>) {
            StreamImpl<ArgT>(os, options, arg);
          } else {
            StreamValue(os, options, arg, false);
          }
        },
        value);
  }

  template<HasMboTypesStringifyValueAccess T>
  requires(!IsStringKeyedContainer<T>)
  void StreamValue(OStream& os, const StringifyFieldOptions& options, const T& value, bool /*allow_field_names*/)
      const {
    StreamValue(os, options, MboTypesStringifyValueAccess(value, options), false);
  }

  template<typename T>
  static constexpr bool kUseStringify = types_internal::IsExtended<T> || HasMboTypesStringifySupport<T>
                                        || (IsAggregate<T> && !absl::HasAbslStringify<T>::value);

  // LCOV_MERGE_FUNC_LINE: repeated for every supported value type.
  template<typename V>
  void StreamValue(  // NOLINT(readability-function-cognitive-complexity)
      OStream& os,
      const StringifyFieldOptions& options,
      const V& v,
      bool allow_field_names) const {
    using RawV = std::remove_cvref_t<V>;
    // IMPORTANT: ALL if-clauses must be `if constexpr`.
    if constexpr (HasMboTypesStringifyDisable<RawV>) {
      os << options.outer.field_control->field_disabled;
    } else if constexpr (kUseStringify<RawV>) {
      StreamImpl(os, options, v);
    } else if constexpr (std::same_as<RawV, std::nullopt_t>) {
      os << options.outer.value_control->nullopt_str;
    } else if constexpr (std::is_same_v<RawV, std::nullptr_t>) {
      os << options.outer.value_control->nullptr_t_str;
    } else if constexpr (IsSmartPtr<RawV>) {
      StreamValueSmartPtr(os, options, v, allow_field_names);
    } else if constexpr (std::is_pointer_v<RawV>) {
      StreamValuePointer(os, options, v, allow_field_names);
    } else if constexpr (IsReferenceWrapper<RawV>) {
      StreamValue(os, options, v.get(), allow_field_names);
    } else if constexpr (IsOptional<RawV>) {
      StreamValueOptional(os, options, v, allow_field_names);
    } else if constexpr (IsOptionalDataOrRef<RawV> || IsOptionalRef<RawV>) {
      if (v.has_value()) {
        StreamValue(os, options, *v, allow_field_names);
      } else {
        StreamValue(os, options, std::nullopt, allow_field_names);
      }
    } else if constexpr (std::same_as<RawV, StringifyFieldInfoString>) {
      StreamValue(os, options, "std::function", allow_field_names);
    } else if constexpr (IsPair<RawV>) {
      StreamValuePair(os, options, v, allow_field_names);
    } else if constexpr (std::is_same_v<RawV, char> || std::is_same_v<RawV, unsigned char>) {
      if (options.outer.format->char_delim.empty()) {
        os << absl::StreamFormat("%v", int(v));
      } else {
        os << options.outer.format->char_delim;
        if (v == '\'') {
          StreamValueStr(os, options.outer, "\\'");
        } else {
          const std::string_view vv{reinterpret_cast<const char*>(&v), 1};  // NOLINT(*-type-reinterpret-cast)
          StreamValueStr(os, options.outer, vv);
        }
        os << options.outer.format->char_delim;
      }
    } else if constexpr (std::is_arithmetic_v<RawV>) {
      if (options.outer.value_overrides->replacement_other.empty()) {
        os << absl::StreamFormat("%v", v);
      } else {
        os << options.outer.value_overrides->replacement_other;
      }
    } else if constexpr (
        std::same_as<RawV, std::string_view> || std::same_as<V, std::string>
        || (std::is_convertible_v<V, std::string_view> && !absl::HasAbslStringify<V>::value)) {
      // Do not attempt to invoke string conversion for AbslStringify supported types as that breaks
      // this very implementation.
      os << options.outer.format->string_delim;
      StreamValueStr(os, options.outer, v);
      os << options.outer.format->string_delim;
    } else {  // NOTE WHEN EXTENDING: Must always use `else if constexpr`
      StreamValueFallback(os, options.outer, v);
    }
  }

  template<IsPair V>
  // LCOV_MERGE_FUNC_LINE: repeated for every pair type.
  void StreamValuePair(OStream& os, const StringifyFieldOptions& options, const V& v, bool allow_field_names) const {
    using RawV = std::remove_cvref_t<V>;
    if constexpr (!HasMboTypesStringifyFieldNames<RawV> && !HasMboTypesStringifyOptions<RawV>) {
      std::array<std::string_view, 2> field_names;
      if (options.outer.special->pair_keys.has_value()) {
        std::get<0>(field_names) = options.outer.special->pair_keys->first;
        std::get<1>(field_names) = options.outer.special->pair_keys->second;
      } else {
        std::get<0>(field_names) = "first";
        std::get<1>(field_names) = "second";
      }
      bool use_seperator = false;
      os.IncStruct(*options.outer.format);
      StreamField(os, options.ToInner(), use_seperator, v, v.first, 0, field_names, allow_field_names);
      StreamField(os, options.ToInner(), use_seperator, v, v.second, 1, field_names, allow_field_names);
      os.DecStruct(*options.outer.format);
    } else {
      StreamFieldsImpl(os, options, v, allow_field_names);
    }
  }

  template<typename V>
  void StreamValueSmartPtr(OStream& os, const StringifyFieldOptions& options, const V& v, bool allow_field_names)
      const {
    if (v) {
      os << options.outer.format->smart_ptr_prefix;
      StreamValue(os, options.ToInner(), *v, allow_field_names);
      os << options.outer.format->smart_ptr_suffix;
    } else {
      os << options.outer.value_control->nullptr_v_str;
    }
  }

  template<typename V>
  void StreamValuePointer(OStream& os, const StringifyFieldOptions& options, const V& v, bool allow_field_names) const {
    if (v) {
      os << options.outer.format->pointer_prefix;
      StreamValue(os, options.ToInner(), *v, allow_field_names);
      os << options.outer.format->pointer_suffix;
    } else {
      os << options.outer.value_control->nullptr_v_str;
    }
  }

  template<typename V>
  void StreamValueOptional(OStream& os, const StringifyFieldOptions& options, const V& v, bool allow_field_names)
      const {
    if (v.has_value()) {
      os << options.outer.format->optional_prefix;
      StreamValue(os, options.ToInner(), *v, allow_field_names);
      os << options.outer.format->optional_suffix;
    } else {
      os << options.outer.value_control->nullopt_str;
    }
  }

  template<typename V>
  static void StreamValueFallback(OStream& os, const StringifyOptions& options, const V& v) {
    if (options.value_control->other_types_direct) {
      if (options.value_overrides->replacement_other.empty()) {
        os << absl::StreamFormat("%v", v);
      } else {
        os << options.value_overrides->replacement_other;
      }
    } else {
      const std::string vv = absl::StrFormat("%v", v);
      StreamValueStr(os, options, vv);
    }
  }

  static void StreamValueStr(OStream& os, const StringifyOptions& options, std::string_view v) {
    if (!options.value_overrides->replacement_str.empty()) {
      os << options.value_overrides->replacement_str;
      return;
    }
    std::string_view vvv{v};
    if (options.value_control->str_max_length != std::string_view::npos) {
      vvv = vvv.substr(0, options.value_control->str_max_length);
    }
    switch (options.value_control->escape_mode) {
      case StringifyOptions::EscapeMode::kNone: os << vvv; break;
      case StringifyOptions::EscapeMode::kCEscape: os << absl::CEscape(vvv); break;
      case StringifyOptions::EscapeMode::kCHexEscape: os << absl::CHexEscape(vvv); break;
    }
    if (vvv.length() < v.length()) {
      os << options.value_control->str_cutoff_suffix;
    }
  }

  static constexpr const StringifyOptions kOptionsDefaults{StringifyOptions::WithAllData({})};
  static constexpr const StringifyOptions kOptionsDisabled = StringifyOptions::WithAllData({
      .field_control{StringifyOptions::FieldControl{
          .suppress = true,
      }},
  });

  static constexpr const StringifyOptions kOptionsCpp = StringifyOptions::WithAllData({
      .format{StringifyOptions::Format{
          .message_suffix = "",
          .field_indent = "",
          .key_value_separator = " = ",
          .field_separator = ", ",
          .pointer_prefix = "",
          .pointer_suffix = "",
      }},
      .field_control{StringifyOptions::FieldControl{
          .field_disabled = "{/*MboTypesStringifyDisable*/}",
      }},
      .key_control{StringifyOptions::KeyControl{
          .key_prefix = ".",
      }},
      .value_control{StringifyOptions::ValueControl{
          .nullptr_t_str = "nullptr",
          .nullptr_v_str = "nullptr",
      }},
  });

  static constexpr const StringifyOptions kOptionsCppPretty = []() constexpr {
    StringifyOptions opts = kOptionsCpp;
    StringifyOptions::Format& format = opts.format.as_data();
    format.message_suffix = "\n";
    format.field_indent = "  ";
    format.field_separator = ",";
    return opts;
  }();

  static constexpr StringifyOptions kOptionsJson = StringifyOptions::WithAllData({
      .format{StringifyOptions::Format{
          .message_suffix = "\n",
          .field_indent = "",
          .key_value_separator = ":",
          .field_separator = ",",
          .pointer_prefix = "",
          .pointer_suffix = "",
          .smart_ptr_prefix = "",
          .smart_ptr_suffix = "",
          .optional_prefix = "",
          .optional_suffix = "",
          .container_prefix = "[",
          .container_suffix = "]",
          .char_delim = "\"",
      }},
      .field_control{StringifyOptions::FieldControl{
          .suppress_nullptr = true,
          .suppress_nullopt = true,
          .suppress_disabled = true,
      }},
      .key_control{StringifyOptions::KeyControl{
          .key_mode = StringifyOptions::KeyMode::kNumericFallback,
          .key_prefix = "\"",
          .key_suffix = "\"",
      }},
      .value_control{StringifyOptions::ValueControl{
          .nullptr_t_str = "null",
          .nullptr_v_str = "null",
          .nullopt_str = "null",
      }},
      .special{StringifyOptions::Special{
          .str_keyed = StringifyOptions::StrKeyed::kFirstIsName,
      }},
  });

  static constexpr StringifyOptions kOptionsJsonLine = []() constexpr {
    StringifyOptions opts = kOptionsJson;
    StringifyOptions::Format& format = opts.format.as_data();
    format.message_suffix = "\n";
    format.key_value_separator = ": ";
    format.field_indent = "";
    format.field_separator = ", ";
    return opts;
  }();

  static constexpr StringifyOptions kOptionsJsonPretty = []() constexpr {
    StringifyOptions opts = kOptionsJson;
    StringifyOptions::Format& format = opts.format.as_data();
    format.message_suffix = "\n";
    format.key_value_separator = ": ";
    format.field_indent = "  ";
    format.field_separator = ",";
    StringifyOptions::Special& special = opts.special.as_data();
    special.str_keyed = StringifyOptions::StrKeyed::kFirstIsNameOrdered;
    return opts;
  }();

  static constexpr StringifyRootOptions kRootOptionDefaults = {};

  // THERE CANNOT BE ANY NON CONST MEMBERS!

  const StringifyRootOptions& default_root_options_;
  const StringifyFieldOptions default_field_options_;  // Only stores two references.
};

// Adapter (not Extender) that injects field names into field control.
//
// Parameter `field_options` must be an instance of `StringifyOptions` or
// something that can produce those. It defaults to `MboTypesStringifyOptions`
// if available and otherwise the default `StringifyOptions` will be used.
//
// If `field_names` is true, then the injected field names must match the actual
// field names. Otherwise the provided `field_names` take precedence over
// compile time determined names.
//
// NOTE: If the field names are constants and all that needs to be done is to
// provide field names, then `MboTypesStringifyFieldNames` is a much better API
// extension point.
//
//
// Example:
//
// ```c++
// struct MyType : mbo::types::Extend<MyType> {
//   int one = 25;
//   int two = 42;
//
//   friend StringifyOptions MboTypesStringifyOptions(
//       const MyType& v, const mbo::types::StringifyFieldInfo& field) {
//     return StringifyWithFieldNames({"one", "two"})(v, field);
//   }
// };
// ```
template<IsFieldNameContainer FieldNames = std::initializer_list<std::string_view>>
inline constexpr auto StringifyWithFieldNames(
    const FieldNames& field_names,
    StringifyNameHandling name_handling = StringifyNameHandling::kVerify) {
  // In order to provide overrides, the actual target `options` have to be created.
  // Once those are available, the overrides can be applied.
  return [&field_names, name_handling]<typename T>(const T&, const StringifyFieldInfo& field) -> StringifyOptions {
    if (field.idx >= std::size(field_names)) {
      return field.options.outer;
    }
    StringifyOptions options = field.options.outer;
    std::string_view field_name = std::data(field_names)[field.idx];  // NOLINT(*-pointer-arithmetic)
    options.key_overrides.as_data().key_use_name = field_name;
    if constexpr (types_internal::SupportsFieldNames<T>) {
      if (name_handling == StringifyNameHandling::kVerify) {
        ABSL_CHECK_EQ(field.name, field_name) << "Bad field_name injection for field #" << field.idx;
      }
    }
    return options;
  };
}

}  // namespace mbo::types

#endif  // MBO_TYPES_STRINGIFY_H_
