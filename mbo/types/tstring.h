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

// Implements type `tstring` a compile time string-literal type, e.g. `"42"_ts`.

#ifndef MBO_TYPES_TSTRING_H_
#define MBO_TYPES_TSTRING_H_

#include <array>
#include <cstddef>  // IWYU pragma: keep
#include <functional>
#include <string_view>

#include "mbo/hash/hash.h"
#include "mbo/strings/contains.h"

namespace mbo::types {

// NOLINTBEGIN(readability-identifier-naming,readability-avoid-unconditional-preprocessor-if)

// NOLINTBEGIN(*-avoid-unchecked-container-access,*-constant-array-index): compile-time string type -
// its subscripts are constant-evaluated or bounded by the length template
// parameter, and an out-of-range index is a compile error, not UB.

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpedantic"
#endif  // defined(__clang__)

// Implements type `tstring` a compile time string-literal type.
//
// It supports template-string-literal types using '_ts' notation.
//
// ```
//   using mbo::types::tstring;
//   using mbo::types::operator""_ts;
//
//   static constexptr auto kName = "First"_ts;
//   static constexptr auto kLast = "Last"_ts;
//
//   static constexpr std::string_view str = kName.str();
//
//   static constexpr auto kFull = kName + " "_ts + kLast;
//
//   static_assert(sizeof(kFull), 1);   // C++ objects need at least 1 byte.
//   static_assert(kFull.size() == 9);  // 'First'=5 + ' '=1 + 'Last'=4, no '\0'
// ```
//
// This works great when compiling on Clang, GCC or a derived compiler. For
// MSVC and other compilers a C++20 compatible version is used that creates a
// helper type that provides an otherwise unnecessary conversion operator form
// `const char(&)[]`.
//
// In absence of the GNU extension, function `make_tstring` or the macro
// `MBO_MAKE_TSTRING` can be used. The former requires a constexr variable
// due to C++20 limitations. The latter takes a literal string.
//
// If instantiated, the size of the generated types is 1 (c++ minimum size).
// The types do not have non-static member fields.
//
// The static size (assuming .data segment) is (length+1) and the data is zero
// terminated but the zero byte is not ever used by the type nor is it reflected
// in any of its accessors (e.g.. `size` or `str`). However, zeros can be part
// of a `tstring` if created as such.
//
// The type can be accessed as a `std::string_view` using static function `str`.
//
// Instances can be compared against anything that can be compared against a
// `std::string_view`. They can be (explicitly) converted into `std::string_view`.
//
// All access functions are `static constexpr`. Using them may require disabling
// a lint warning ("readability-static-accessed-through-instance") or access
// through the type: `decltype(my_tstring_var)::size()`.
//
// The type has a `std::hash` specialization that does **not** match either
// `std::hash<std::string>` nor `std::hash<std::string_view>` because that is
// not available as constexpr (though could be implemented as such if needed).
// The type also has an integration with Abseil hashing which even requires
// that the hash of this type is different than that of `std::string_view`.
// Finally, the hash value used is directly available as `Hash`, but you may not
// rely on the exact values (and they may vary from version to version or with
// every compilation).
template<char... chars>
struct tstring final {
  using value_type = const char;
  using traits_type = std::char_traits<char>;
  // allocator_type	allocator<char>
  using reference = const char&;
  using const_reference = const char&;
  using pointer = const char*;
  using const_pointer = const char*;
  using iterator = const char*;
  using const_iterator = const char*;
  using reverse_iterator = std::string_view::const_reverse_iterator;
  using const_reverse_iterator = std::string_view::const_reverse_iterator;
  using difference_type = ptrdiff_t;
  using size_type = std::size_t;

  static constexpr size_type npos = std::string_view::npos;

  // Access to the underlying data as a `std::string_view`.
  // LCOV_MERGE_FUNC_LINE: repeated for every compile-time string value.
  static constexpr std::string_view str() noexcept { return {data.data(), sizeof...(chars)}; }

  static constexpr const char* c_str() noexcept { return data.data(); }

  // Identity comparison - purely performed as a compile-time, type check.
  //
  // This is different from the comparison operators that are performed on
  // the actual string data at run-time or the in/equality operators that are
  // performed on run-time or compile-time base on the compared to type.
  template<typename Other>
  // LCOV_MERGE_FUNC_LINE: repeated for every compared compile-time string type.
  static constexpr bool is(const Other& /* other */) noexcept {
    return std::is_same_v<tstring, Other>;
  }

  // String based comparison (run-time), uses `std::string_view::compare`.
  static constexpr auto compare(std::string_view other) noexcept { return str().compare(other); }

  static constexpr size_type size() noexcept { return num_chars; }

  static constexpr size_type max_size() noexcept { return num_chars; }

  static constexpr size_type length() noexcept { return num_chars; }

  static constexpr bool empty() noexcept { return num_chars == 0; }

  static constexpr iterator begin() noexcept { return str().cbegin(); }

  static constexpr iterator end() noexcept { return str().cend(); }

  static constexpr const_iterator cbegin() noexcept { return str().cbegin(); }

  static constexpr const_iterator cend() noexcept { return str().cend(); }

  static constexpr reverse_iterator rbegin() noexcept { return str().crbegin(); }

  static constexpr reverse_iterator rend() noexcept { return str().crend(); }

  static constexpr const_reverse_iterator crbegin() noexcept { return str().crbegin(); }

  static constexpr const_reverse_iterator crend() noexcept { return str().crend(); }

  static constexpr const_reference front() noexcept { return str().front(); }

  // Access to the last character. This is undefined if `empty() == true'.
  static constexpr const_reference back() noexcept { return str().back(); }

  // Computes (at compile-time) the tstring type for the sub string starting at
  // `pos` with at most `count` chars. If the result would exceed the last
  // position, then the result is capped to the end of this `tstring`.
  //
  // For instance:
  //
  // ```c++
  //   ("0123"_ts).substr<1, 5>() == "123"_ts
  // ```
  //
  // If `count == tstring::npos`, then the result is the substring starting at
  // `pos` running through the remainder of this `tstring`.
  //
  // That means if `count` >= `size()`, then the result is `""_ts`.
  //
  // Further, if `pos` >= `size())` also return `""_ts`. This is different from
  // `std::string_view::substr` which would throw an exception on `pos` overrun.
  //
  // Since C++20 does not allow function parameters to be identified as compile
  // time constants, this function takes the values for `pos` and `count` as
  // template arguments.
  //
  // If the parameter `pos` or `count` cannot be provided at compile-time, then
  // the run-time alternative `str_substr(pos, count)` has to be used instead.
  template<size_type pos = 0, size_type count = npos>
  // LCOV_MERGE_FUNC_LINE: repeated for every compile-time position and count.
  static constexpr auto substr() noexcept {
    if constexpr (pos == 0 && (count == npos || count >= num_chars)) {
      return tstring{};  // this type
    } else if constexpr (pos >= num_chars || count == 0) {
      return tstring<>{};  // empty
    } else {
      constexpr const size_type result_len =
          count == npos ? num_chars - pos : (num_chars - pos < count ? num_chars - pos : count);
      if constexpr (result_len == 0) {
        return tstring<>{};  // empty
      } else {
        // LCOV_MERGE_FUNC_LINE: repeated for every generated index sequence.
        return [&]<size_type... Is>(std::index_sequence<Is...>) constexpr noexcept -> tstring<data[Is + pos]...> {
          return {};
        }(std::make_index_sequence<result_len>{});
      }
    }
  }

  // Run-time substring computation which returns a `std::string_view` as
  // opposed to a `tstring` type.
  //
  // Unlike `std::string_view::substr` but like `substr` this method also does
  // not throw an exception in case `pos >= size` and rather returns an empty
  // string view.
  static constexpr std::string_view substr(size_type pos, size_type count = npos) noexcept {
    return pos >= size() ? "" : str().substr(pos, count);
  }

  // Checks (at compile-time) whether this type starts with `tstring<Other...>`.
  template<char... Other>
  static constexpr bool starts_with(tstring<Other...> /* other */) noexcept {
    return sizeof...(Other) <= num_chars && substr<0, sizeof...(Other)>() == tstring<Other...>();
  }

  // Checks (at compile time) whether this type ends with `tstring<Other...>`.
  template<char... Other>
  static constexpr bool ends_with(tstring<Other...> /* other */) noexcept {
    return sizeof...(Other) <= num_chars && substr<num_chars - sizeof...(Other)>() == tstring<Other...>();
  }

  // Returns (at compile time) the position of `tstring<Other...>` in this
  // `tstring` or `tstring::npos`.
  template<char... Other>
  static constexpr size_type find(tstring<Other...> /* other */) noexcept {
    constexpr size_type olen = sizeof...(Other);
    if constexpr (olen == 0) {
      return 0;
    } else if constexpr (olen > num_chars) {
      return npos;
    } else {
      using OStr = tstring<Other...>;
      // The tests cover every semantic outcome; GCC expands the fold into one
      // branch pair per character and template instantiation.
      return [&]<size_type... Is>(std::index_sequence<Is...>) constexpr noexcept {
        size_type pos = 0;
        return ((++pos, OStr::is(substr<Is, olen>())) || ...) ? pos - 1 : npos;  // LCOV_EXCL_BR_LINE
      }(std::make_index_sequence<num_chars - olen + 1>{});
    }
  }

  // Returns (at compile time) the last position of `tstring<Other...>` in this
  // `tstring` or `testing::npos`.
  template<char... Other>
  static constexpr size_type rfind(tstring<Other...> /* other */) noexcept {
    constexpr size_type olen = sizeof...(Other);
    if constexpr (olen == 0) {
      return num_chars;
    } else if constexpr (olen > num_chars) {
      return npos;
    } else {
      using OStr = tstring<Other...>;
      constexpr size_type len = num_chars - olen;
      return [&]<size_type... I>(std::index_sequence<I...>) constexpr noexcept {
        size_type pos = num_chars - olen + 1;
        return ((--pos, OStr::is(substr<len - I, olen>())) || ...) ? pos : npos;  // LCOV_EXCL_BR_LINE
      }(std::make_index_sequence<len + 1>{});
    }
  }

  // Checks (at compile time) whether `tstring<Other...>` is contained in this `tstring`.
  template<char... Other>
  static constexpr bool contains(tstring<Other...> /* other */) noexcept {
    constexpr size_type other_len = sizeof...(Other);
    if constexpr (other_len == 0) {
      return true;
    } else if constexpr (other_len > num_chars) {
      return false;
    } else {
      // This could use `find` but the local implementation highlights that
      // no instance of `tstring<Other...>` is being used.
      return [&]<size_type... Is>(std::index_sequence<Is...>) constexpr noexcept {
        return (tstring<Other...>::is(substr<Is, other_len>()) || ...);  // LCOV_EXCL_BR_LINE
      }(std::make_index_sequence<num_chars - other_len + 1>{});
    }
  }

  // Check for presence of one character of a character-set.
  static constexpr size_type find_first_of(char chr, size_type pos = 0) noexcept {
    for (; pos < num_chars; ++pos) {
      if (data[pos] == chr) {
        return pos;
      }
    }
    return npos;
  }

  static constexpr size_type find_first_of(std::string_view charset, size_type pos = 0) noexcept {
    for (; pos < num_chars; ++pos) {
      if (mbo::strings::Contains(charset, data[pos])) {
        return pos;
      }
    }
    return npos;
  }

  // Check for presence of one character of a character-set from the back.
  static constexpr size_type find_last_of(char chr, size_type pos = npos) noexcept {
    if constexpr (num_chars == 0) {
      return npos;
    }
    if (pos >= num_chars) {
      pos = num_chars - 1;
    }
    while (true) {
      if (data[pos] == chr) {
        return pos;
      }
      if (pos-- == 0) {
        return npos;
      }
    }
    return npos;
  }

  static constexpr size_type find_last_of(std::string_view charset, size_type pos = npos) noexcept {
    if constexpr (num_chars == 0) {
      return npos;
    }
    if (pos >= num_chars) {
      pos = num_chars - 1;
    }
    while (true) {
      if (mbo::strings::Contains(charset, data[pos])) {
        return pos;
      }
      if (pos-- == 0) {
        return npos;
      }
    }
    return npos;
  }

  // Only the default constructor is offered - no other constructor is useful.
  constexpr tstring() noexcept = default;

  // Explicit conversion to `std::string_view`. This is explicit as the type is
  // supposed to mostly be used as a type.
  explicit constexpr operator std::string_view() const noexcept { return str(); }

  // Operator `==` is provided once and will be synthesized into `!=`.
  // When possible the operator performs type comparison. That is, if `Other` is
  // a `tstring` type, then the comparison is a compile-time no-op. Otherwise
  // the operation is a run-time string comparison.
  //
  // The inequality operators `<`, `<=`, `>` and `>=` are not implementable as
  // friends since that would interfere with the operator `==` here and create
  // redefinition issues if two `tstring` types are being compared.
  template<typename Other>
  // LCOV_MERGE_FUNC_LINE: repeated for every compared string type.
  constexpr bool operator==(const Other& other) const noexcept {
    if constexpr (std::is_same_v<tstring, Other>) {
      return true;
    } else if constexpr (std::equality_comparable_with<std::string_view, Other>) {
      return str() == other;
    } else {
      return false;
    }
  }

  // Operator `<=>` for `tstring` types implements all other inequality
  // operators.
  //
  // The operation is performed at run-time.
  template<char... Other>
  constexpr auto operator<=>(tstring<Other...> /* other */) const noexcept {
    return str().compare(tstring<Other...>::str()) <=> 0;
  }

  // Comparison operators for all other types that can be compared against a
  // `std::string_view`.
  //
  // The operation is performed at run-time.
  template<typename Other>
  requires requires(Other other) { std::string_view().compare(other); }
  constexpr auto operator<=>(const Other& other) const noexcept {
    return str().compare(other) <=> 0;
  }

  // LCOV_MERGE_FUNC_LINE: repeated for every compile-time string value.
  friend std::ostream& operator<<(std::ostream& os, tstring /* value */) { return os << tstring::str(); }

  // Concatenation.
  //
  // TODO(helly25): With static call operator support this could maybe become a static operator.
  template<char... Other>
  constexpr auto operator+(tstring<Other...> /* other */) const noexcept {
    return tstring<chars..., Other...>();
  }

  // Returns the value of `GetHash64(str())` - so as if this was a `std::string` or `std::string_view`.
  static constexpr uint64_t StringHash() { return ::mbo::hash::GetHash64(str()); }

  // Returns the typed hash, so different from `StringHash` above.
  static constexpr uint64_t Hash() {
    constexpr uint64_t kTStringHashSeed = 0x423325fe9b234a3fULL;
    return StringHash() ^ kTStringHashSeed;
  }

  template<typename H>
  friend H AbslHashValue(H hash, const tstring& /*t_str*/) {
    return H::combine(std::move(hash), Hash());
  }

 private:
  static constexpr size_type num_chars = sizeof...(chars);
  static constexpr std::array<char, num_chars + 1> data{chars..., 0};
};

namespace types_internal {

// NOLINTBEGIN(*-avoid-c-arrays)

// Safely identifies the length of the input to the `MBO_MAKE_TSTRING` macro.
template<std::size_t N>
inline constexpr std::size_t tstring_input_len(const char (& /* v */)[N]) noexcept {
  return N ? N - 1 : 0;  // Not sizeof(str) to exclude the \0 AND check N>0.
}

// NOLINTEND(*-avoid-c-arrays)

// Helper struct that can be used to generate `tstring` from `const char*`
// template arguments.
// NOTE: These must be 'external entities' or in other words variables of the
// form `static constexpr char[]`.
template<const char* Str>
class MakeTstringHelper {
 public:
  MakeTstringHelper() = delete;
  ~MakeTstringHelper() = delete;
  MakeTstringHelper(const MakeTstringHelper&) = delete;
  MakeTstringHelper& operator=(const MakeTstringHelper&);
  MakeTstringHelper(MakeTstringHelper&&) = delete;
  MakeTstringHelper& operator=(MakeTstringHelper&&) = delete;

  static constexpr auto tstr() noexcept {
    return []<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept -> tstring<(str())[Is]...> {
      return {};
    }(std::make_index_sequence<size()>{});
  }

 private:
  static constexpr std::string_view str() noexcept { return Str; }

  static constexpr std::size_t size() { return str().size(); }
};

#if !defined(__clang__) && !defined(__GNUC__)
// This helper is necessary for some compilers, e.g. MSVC.
// It is not needed for Clang, GCC or compilers derived from these such as ICC.
// This is not default available as it introduces a type that has a public
// conversion constructor. This cannot be prevented with a friend declaration
// as the type will be initiated by the compiler as a template parameter, and
// therefore declaring the `operator"" _ts` as a friend is not enough.
template<std::size_t N>
struct MakeTstringLiteralHelper {
  MakeTstringLiteralHelper() = delete;

  // NOLINTBEGIN(*-avoid-c-arrays)
  // NOLINTBEGIN(google-explicit-constructor)
  constexpr MakeTstringLiteralHelper(const char (&str)[N]) noexcept
      : MakeTstringLiteralHelper(
            std::string_view{str, Length(N)},
            std::make_integer_sequence<std::size_t, Length(N)>()) {}

  // NOLINTEND(google-explicit-constructor)
  // NOLINTEND(*-avoid-c-arrays)

  ~MakeTstringLiteralHelper() = default;
  MakeTstringLiteralHelper(const MakeTstringLiteralHelper&) = delete;
  MakeTstringLiteralHelper& operator=(const MakeTstringLiteralHelper&) = delete;
  MakeTstringLiteralHelper(MakeTstringLiteralHelper&&) = delete;
  MakeTstringLiteralHelper& operator=(MakeTstringLiteralHelper&&) = delete;

  constexpr std::string_view str() const noexcept { return {data.data(), Length(data.size())}; }

  std::array<char, N> data;

 private:
  static constexpr std::size_t Length(std::size_t len) noexcept { return len > 0 ? len - 1 : len; }

  template<std::size_t... Is>
  constexpr explicit MakeTstringLiteralHelper(
      std::string_view str,
      std::integer_sequence<std::size_t, Is...> /* indices */) noexcept
      : data{str[Is]..., 0} {}
};
#endif

}  // namespace types_internal

#if defined(__clang__) || defined(__GNUC__)
// String literal support for Clang, GCC and derived compilers. Enables:
//   constexpr auto my_constexpr_string = "foo"_ts;
//
// In order to import the string-literal-operator into another namespace use:
//
// ```
// using mbo::types::operator""_ts;
// ```
//
// String literal operator templates are a GNU extension
// [-Wgnu-string-literal-operator-template]
template<typename T, T... chars>
requires std::is_same_v<T, char>
constexpr tstring<chars...> operator""_ts() {
  return {};
}
#else
// String literal support for Clang, GCC and derived compilers. Enables:
//   constexpr auto my_constexpr_string = "foo"_ts;
template<types_internal::MakeTstringLiteralHelper Helper>
constexpr auto operator"" _ts() noexcept {
  return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    return tstring<Helper.str()[Is]...>{};
  }(std::make_index_sequence<Helper.str().size()>{});
}
#endif

// The C++20 standard does not allow static constexpr types string literals.
// Below are various forms of solutions and how they fail.
#if 0  // no matching literal operator for call to 'operator""_ts' with
       // arguments of types 'const char *' and 'unsigned long', and no matching
       // literal operator template
// For an additional helper the issue is in matching the provided parameters
// `const char*, std::size_t` in a way that can be used in a constexpr function.
template<tstring Str>
constexpr auto operator"" _ts() {
    return Str;
}
#endif
#if 0  // template parameter list for literal operator must be either 'char...'
       // or 'typename T, T...'
template<const char* str> constexpr auto operator ""_ts() {
  return types_internal::make_tstring_helper<str>();
}
#endif
#if 0  // No matching literal operator for call to 'operator""_ts' with
       // arguments of types 'const char *' and 'unsigned long', and no matching
       // literal operator template
template<char... c> constexpr tstring<c...> operator ""_ts() {
  return {};
}
#endif
#if 0  // Non-type template argument is not a constant
       // expressionclang(expr_not_cce)
       // Function parameter 'str' with unknown value cannot be used in a
       // constant expression
constexpr auto operator ""_ts(const char* str, std::size_t n) {
  return types_internal::make_tstring_helper<str>::tstr();
}
#endif

// Constructs a `tstring` from `const char*` template arguments.
//
// NOTE: These must be 'external entities' or in other words variables of the
// form `static constexpr char[]`. E.g.:
//
//   static constexpr char myconst[] = "myconst";
//   static constexpr auto mytstr = make_tstring(myconst);
//
// This does create the string value twice and unnecessarily computes the
// string length multiple times.
//
// Prefer creation via the template-string-literal operator:
//   static constexpr auto "myconst"_ts
//
// Unlike the template-string-literal operator and the macro, this function
// does not support zero chars. Instead, the compiler will stop input detection
// at the zero char. Example:
//   static constexpr char mystring[] = "0123\0";
//   decltype(make_tstring(mystring))::size() == 3  // not 4
template<const char* Str>
constexpr auto make_tstring() noexcept {
  return types_internal::MakeTstringHelper<Str>::tstr();
}

#if 0  // The below follows `std::to_array` but cases clang/gcc to crash.
template<std::size_t N>
constexpr auto make_ts(const char(&str)[N]) noexcept {
  if constexpr (N == 0) {
    return tstring<>();
  } else {
    return [str]<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept
            -> tstring<static_cast<char>(str[Is])...> {
      return {};
    }(std::make_index_sequence<N - 1>{});
  }
}
#endif

// Macro form of defining `tstring` type-instances.
// The macro uses helper function `tstring_input_len` that ensures that the
// provided input is a char array (e.g. `const char(&)[N]`). This is done so
// that the length can be computed correctly. As a result the macro may support
// zero chars - as long as the compiler correctly handles length detection.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_MAKE_TSTRING(str)                                                                     \
  []<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept -> tstring<(str)[Is]...> { \
    return {};                                                                                    \
  }(std::make_index_sequence<types_internal::tstring_input_len(str)>{})

#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif  // defined(__clang__)

// NOLINTEND(*-avoid-unchecked-container-access,*-constant-array-index)
// NOLINTEND(readability-identifier-naming,readability-avoid-unconditional-preprocessor-if)

}  // namespace mbo::types

namespace std {

template<char... chars>
struct hash<const mbo::types::tstring<chars...>> {  // NOLINT(cert-dcl58-cpp)

  constexpr std::size_t operator()() const noexcept {
    // This could match the `std::hash<std::string>` and `std::hash<string_view>` hashes.
    // But that would defeat the purpose of being computed at compile time.
    // return std::hash<std::string_view>{}(mbo::types::tstring<chars...>::str());
    return mbo::types::tstring<chars...>::Hash();
  }

  constexpr std::size_t operator()(const mbo::types::tstring<chars...>& /*t_str*/) const noexcept {
    return mbo::types::tstring<chars...>::Hash();
  }
};

}  // namespace std

#endif  // MBO_TYPES_TSTRING_H_
