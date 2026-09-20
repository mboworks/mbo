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

#ifndef MBO_TYPES_STRING_OR_VIEW_H_
#define MBO_TYPES_STRING_OR_VIEW_H_

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#if __has_include(<format>)
# include <format>
#endif
#include <iosfwd>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"

namespace mbo {

// Read-only text that either owns a string or borrows existing storage.
//
// A borrowed string_view or character array must outlive this object and every
// copy of it. Runtime C strings deliberately have no unambiguous constructor:
// callers must select std::string for ownership or std::string_view to borrow.
class StringOrView {
 public:
  // NOLINTBEGIN(readability-identifier-length,readability-identifier-naming): The compatibility API mirrors std.
  using traits_type = std::string_view::traits_type;
  using value_type = std::string_view::value_type;
  using pointer = std::string_view::const_pointer;
  using const_pointer = std::string_view::const_pointer;
  using reference = std::string_view::const_reference;
  using const_reference = std::string_view::const_reference;
  using const_iterator = std::string_view::const_iterator;
  using iterator = const_iterator;
  using const_reverse_iterator = std::string_view::const_reverse_iterator;
  using reverse_iterator = const_reverse_iterator;
  using size_type = std::string_view::size_type;
  using difference_type = std::string_view::difference_type;

  static constexpr size_type npos = std::string_view::npos;

  ~StringOrView() = default;

  constexpr StringOrView() noexcept = default;

  StringOrView(std::string value)  // NOLINT(google-explicit-constructor)
      : value_(std::in_place_type<std::string>, std::move(value)) {}

  constexpr StringOrView(std::string_view value) noexcept  // NOLINT(*-explicit-*)
      : value_(std::in_place_type<std::string_view>, value) {}

  template<std::size_t N>
  // A literal-array reference is the lifetime-safe alternative to an
  // ambiguous runtime `const char*` conversion.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  constexpr StringOrView(const char (&literal)[N]) noexcept  // NOLINT(*-explicit-*)
      : value_(std::in_place_type<std::string_view>, literal, N - 1) {}

  StringOrView(const StringOrView&) = default;
  StringOrView(StringOrView&&) noexcept = default;
  StringOrView& operator=(const StringOrView&) = default;
  StringOrView& operator=(StringOrView&&) noexcept = default;

  [[nodiscard]] constexpr std::string_view view() const noexcept {  // NOLINT(*-identifier-naming)
    if (const auto* const borrowed = std::get_if<std::string_view>(&value_); borrowed != nullptr) {
      return *borrowed;
    }
    return std::get<std::string>(value_);
  }

  [[nodiscard]] constexpr bool owns_string() const noexcept {  // NOLINT(*-identifier-naming)
    return std::holds_alternative<std::string>(value_);
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {  // NOLINT(google-explicit-constructor)
    return view();
  }

  [[nodiscard]] constexpr const_iterator begin() const noexcept { return view().begin(); }

  [[nodiscard]] constexpr const_iterator end() const noexcept { return view().end(); }

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return view().cbegin(); }

  [[nodiscard]] constexpr const_iterator cend() const noexcept { return view().cend(); }

  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return view().rbegin(); }

  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return view().rend(); }

  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return view().crbegin(); }

  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return view().crend(); }

  [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept {
    return view()[pos];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Mirrors std.
  }

  [[nodiscard]] constexpr const_reference at(size_type pos) const { return view().at(pos); }

  [[nodiscard]] constexpr const_reference front() const noexcept { return view().front(); }

  [[nodiscard]] constexpr const_reference back() const noexcept { return view().back(); }

  [[nodiscard]] constexpr const_pointer data() const noexcept { return view().data(); }

  [[nodiscard]] constexpr size_type size() const noexcept { return view().size(); }

  [[nodiscard]] constexpr size_type length() const noexcept { return view().length(); }

  [[nodiscard]] constexpr size_type max_size() const noexcept { return view().max_size(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return view().empty(); }

  constexpr size_type copy(char* dest, size_type count, size_type pos = 0) const {
    return view().copy(dest, count, pos);
  }

  [[nodiscard]] constexpr std::string_view substr(size_type pos = 0, size_type count = npos) const {
    return view().substr(pos, count);
  }

  [[nodiscard]] constexpr std::string_view subview(size_type pos = 0, size_type count = npos) const {
    return view().substr(pos, count);
  }

  [[nodiscard]] constexpr int compare(std::string_view other) const noexcept { return view().compare(other); }

  [[nodiscard]] constexpr int compare(size_type pos, size_type count, std::string_view other) const {
    return view().compare(pos, count, other);
  }

  [[nodiscard]] constexpr int compare(
      size_type pos,
      size_type count,
      std::string_view other,
      size_type other_pos,
      size_type other_count = npos) const {
    return view().compare(pos, count, other, other_pos, other_count);
  }

  [[nodiscard]] constexpr int compare(const char* other) const { return view().compare(other); }

  [[nodiscard]] constexpr int compare(size_type pos, size_type count, const char* other) const {
    return view().compare(pos, count, other);
  }

  [[nodiscard]] constexpr int compare(size_type pos, size_type count, const char* other, size_type other_count) const {
    return view().compare(pos, count, other, other_count);
  }

  [[nodiscard]] constexpr bool starts_with(std::string_view prefix) const noexcept {
    return view().starts_with(prefix);
  }

  [[nodiscard]] constexpr bool starts_with(char prefix) const noexcept { return view().starts_with(prefix); }

  [[nodiscard]] constexpr bool starts_with(const char* prefix) const { return view().starts_with(prefix); }

  [[nodiscard]] constexpr bool ends_with(std::string_view suffix) const noexcept { return view().ends_with(suffix); }

  [[nodiscard]] constexpr bool ends_with(char suffix) const noexcept { return view().ends_with(suffix); }

  [[nodiscard]] constexpr bool ends_with(const char* suffix) const { return view().ends_with(suffix); }

  [[nodiscard]] constexpr bool contains(std::string_view text) const noexcept { return view().contains(text); }

  [[nodiscard]] constexpr bool contains(char ch) const noexcept { return view().contains(ch); }

  [[nodiscard]] constexpr bool contains(const char* text) const { return view().contains(text); }

#define MBO_STRING_OR_VIEW_FIND_OVERLOADS(name, default_pos)                                                    \
  [[nodiscard]] constexpr size_type name(std::string_view text, size_type pos = (default_pos)) const noexcept { \
    return view().name(text, pos);                                                                              \
  }                                                                                                             \
  [[nodiscard]] constexpr size_type name(char ch, size_type pos = (default_pos)) const noexcept {               \
    return view().name(ch, pos);                                                                                \
  }                                                                                                             \
  [[nodiscard]] constexpr size_type name(const char* text, size_type pos, size_type count) const {              \
    return view().name(text, pos, count);                                                                       \
  }                                                                                                             \
  [[nodiscard]] constexpr size_type name(const char* text, size_type pos = (default_pos)) const {               \
    return view().name(text, pos);                                                                              \
  }

  MBO_STRING_OR_VIEW_FIND_OVERLOADS(find, 0)
  MBO_STRING_OR_VIEW_FIND_OVERLOADS(rfind, npos)
  MBO_STRING_OR_VIEW_FIND_OVERLOADS(find_first_of, 0)
  MBO_STRING_OR_VIEW_FIND_OVERLOADS(find_last_of, npos)
  MBO_STRING_OR_VIEW_FIND_OVERLOADS(find_first_not_of, 0)
  MBO_STRING_OR_VIEW_FIND_OVERLOADS(find_last_not_of, npos)

#undef MBO_STRING_OR_VIEW_FIND_OVERLOADS

  // NOLINTEND(readability-identifier-length,readability-identifier-naming)

  constexpr bool operator==(const StringOrView& other) const noexcept { return view() == other.view(); }

  constexpr auto operator<=>(const StringOrView& other) const noexcept { return view() <=> other.view(); }

  template<typename Other>
  requires(
      !std::same_as<std::remove_cvref_t<Other>, StringOrView>
      && requires(const Other& other) { std::string_view{other}; })
  constexpr bool operator==(const Other& other) const noexcept {
    return view() == std::string_view{other};
  }

  template<typename Other>
  requires(
      !std::same_as<std::remove_cvref_t<Other>, StringOrView>
      && requires(const Other& other) { std::string_view{other}; })
  constexpr auto operator<=>(const Other& other) const noexcept {
    return view() <=> std::string_view{other};
  }

  template<typename Sink>
  friend void AbslStringify(Sink& sink, const StringOrView& value) {
    absl::Format(&sink, "%s", value.view());
  }

  template<typename H>
  friend H AbslHashValue(H hash_state, const StringOrView& value) {
    return H::combine(std::move(hash_state), value.view());
  }

  friend std::ostream& operator<<(std::ostream& out, const StringOrView& value) {
    return out.write(value.data(), static_cast<std::streamsize>(value.size()));
  }

 private:
  std::variant<std::string_view, std::string> value_{std::string_view{}};
};

}  // namespace mbo

template<>
struct std::hash<mbo::StringOrView> {
  std::size_t operator()(const mbo::StringOrView& value) const noexcept {
    return std::hash<std::string_view>{}(value.view());
  }
};

#if defined(__cpp_lib_format) && __cpp_lib_format >= (20L * 100 + 19) * 100 + 7
template<>
struct std::formatter<mbo::StringOrView, char> : std::formatter<std::string_view, char> {
  template<typename FormatContext>
  auto format(const mbo::StringOrView& value, FormatContext& context) const {
    return std::formatter<std::string_view, char>::format(value.view(), context);
  }
};
#endif

#endif  // MBO_TYPES_STRING_OR_VIEW_H_
