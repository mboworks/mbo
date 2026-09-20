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
#ifndef MBO_JSON_JSON_H_
#define MBO_JSON_JSON_H_

#include <compare>
#include <concepts>  // IWYU pragma: keep
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "mbo/config/require.h"
#include "mbo/log/demangle.h"
#include "mbo/types/cases.h"
#include "mbo/types/compare.h"
#include "mbo/types/stringify.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep
#include "mbo/types/variant.h"

// NOLINTBEGIN(readability-identifier-naming)

namespace mbo::json {

class Json;

namespace json_internal {

using Float = double;  // Could be `long double`
using SignedInt = int64_t;
using UnsignedInt = uint64_t;

using Array = std::unique_ptr<std::vector<Json>>;
using Object = absl::flat_hash_map<std::string, std::unique_ptr<Json>>;
using Variant = std::variant<std::nullopt_t, Array, bool, Float, SignedInt, Object, std::string, UnsignedInt>;

template<typename Value>
concept JsonUseSignedInt = std::is_signed_v<Value> && std::integral<Value> && std::convertible_to<Value, SignedInt>;

template<typename Value>
concept JsonUseUnsignedInt =
    std::is_unsigned_v<Value> && std::integral<Value> && std::convertible_to<Value, UnsignedInt>;

template<typename Value>
concept JsonUseFloat = !std::same_as<std::remove_cvref_t<Value>, bool>
                       && (std::floating_point<Value> || std::convertible_to<Value, Float>);

}  // namespace json_internal

template<typename Value>
concept JsonUseNull = std::convertible_to<Value, std::nullopt_t>;

template<typename Value>
concept JsonUseArray = std::same_as<std::remove_cvref_t<Value>, json_internal::Array>;

template<typename Value>
concept JsonUseBool = std::same_as<std::remove_cvref_t<Value>, bool>;

template<typename Value>
concept JsonUseNumber = json_internal::JsonUseSignedInt<Value> || json_internal::JsonUseUnsignedInt<Value>
                        || json_internal::JsonUseFloat<Value>;

template<typename Value>
concept JsonUseObject = std::same_as<std::remove_cvref_t<Value>, Json>;

template<typename Value>
concept JsonUseString = types::ConstructibleFrom<std::string, Value>;

template<typename T>
concept ConvertibleToJson = JsonUseNull<T> || JsonUseBool<T> || JsonUseNumber<T> || JsonUseString<T>;

class Json {
 public:
  using Float = json_internal::Float;
  using SignedInt = json_internal::SignedInt;
  using UnsignedInt = json_internal::UnsignedInt;
  using Array = json_internal::Array;
  using Object = json_internal::Object;
  using Variant = json_internal::Variant;

 private:
  using array_iterator = Array::element_type::iterator;
  using const_array_iterator = Array::element_type::const_iterator;
  using object_iterator = Object::iterator;
  using const_object_iterator = Object::const_iterator;

  // Iterator (const and mutable) for Value iteration over both Arrays and Objects.
  template<bool IsConst>
  class value_iterator_t {
   private:
    template<bool>
    friend class value_iterator_t;

    using array_iterator = std::conditional_t<IsConst, Json::const_array_iterator, Json::array_iterator>;
    using object_iterator = std::conditional_t<IsConst, Json::const_object_iterator, Json::object_iterator>;

    using const_iterator = value_iterator_t<true>;
    using mutable_iterator = value_iterator_t<false>;

   public:
    using iterator_category = std::forward_iterator_tag;  // properties are forward only
    using difference_type = array_iterator::difference_type;
    using value_type = array_iterator::value_type;
    using pointer = array_iterator::pointer;
    using reference = array_iterator::reference;
    using element_type = array_iterator::value_type;  // Clang 16

    ~value_iterator_t() noexcept = default;
    value_iterator_t() noexcept = default;  // Sadly needed for `std::forward_iterator`

    explicit value_iterator_t(array_iterator arr_it) noexcept : it_(arr_it) {}

    explicit value_iterator_t(object_iterator obj_it) noexcept : it_(obj_it) {}

    value_iterator_t(const value_iterator_t&) noexcept = default;
    value_iterator_t& operator=(const value_iterator_t&) noexcept = default;
    value_iterator_t(value_iterator_t&&) noexcept = default;
    value_iterator_t& operator=(value_iterator_t&&) noexcept = default;

    // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved,*explicit-*)

    value_iterator_t(const mutable_iterator& other) noexcept
    requires std::same_as<value_iterator_t, const_iterator> {
      std::visit([this](const auto& iterator) { it_ = iterator; }, other.it_);
    }

    value_iterator_t& operator=(const mutable_iterator& other) noexcept
    requires std::same_as<value_iterator_t, const_iterator> {
      std::visit([this](const auto& iterator) { it_ = iterator; }, other.it_);
      return *this;
    }

    value_iterator_t(mutable_iterator&& other) noexcept
    requires std::same_as<value_iterator_t, const_iterator> {
      std::visit([this](auto&& iterator) { it_ = std::forward<decltype(iterator)>(iterator); }, std::move(other.it_));
    }

    value_iterator_t& operator=(mutable_iterator&& other) noexcept
    requires std::same_as<value_iterator_t, const_iterator> {
      std::visit([this](auto&& iterator) { it_ = std::forward<decltype(iterator)>(iterator); }, std::move(other.it_));
      return *this;
    }

    operator array_iterator() noexcept { return std::get<array_iterator>(it_); }

    operator object_iterator() noexcept { return std::get<object_iterator>(it_); }

    operator const_array_iterator() const noexcept { return std::get<array_iterator>(it_); }

    operator const_object_iterator() const noexcept { return std::get<object_iterator>(it_); }

    // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved,*explicit-*)

    reference operator*() const noexcept {
      return IsArray() ? *std::get<array_iterator>(it_) : *std::get<object_iterator>(it_)->second;
    }

    pointer operator->() const noexcept { return &**this; }

    explicit operator reference() const noexcept { return **this; }

    value_iterator_t& operator++() noexcept {
      if (IsArray()) {
        ++std::get<array_iterator>(it_);
      } else {
        ++std::get<object_iterator>(it_);
      }
      return *this;
    }

    value_iterator_t operator++(int) noexcept {  // NOLINT(cert-dcl21-cpp)
      value_iterator_t result = *this;
      if (IsArray()) {
        ++std::get<array_iterator>(it_);
      } else {
        ++std::get<object_iterator>(it_);
      }
      return result;
    }

    friend bool operator==(const value_iterator_t<IsConst>& lhs, const value_iterator_t<IsConst>& rhs) noexcept {
      return lhs.it_ == rhs.it_;
    }

   private:
    bool IsArray() const noexcept { return std::holds_alternative<array_iterator>(it_); }

    std::variant<array_iterator, object_iterator> it_{};
  };

  static_assert(std::forward_iterator<value_iterator_t<true>>);  // Required for ValuesViewT

  template<bool IsConst>
  class ValuesViewT : public std::ranges::view_interface<ValuesViewT<IsConst>> {
   private:
    using array_iterator = std::conditional_t<IsConst, Json::const_array_iterator, Json::array_iterator>;
    using object_iterator = std::conditional_t<IsConst, Json::const_object_iterator, Json::object_iterator>;

   public:
    using value_iterator = value_iterator_t<IsConst>;
    using value_type = value_iterator::value_type;
    using reference = value_iterator::reference;
    using difference_type = value_iterator::difference_type;

    ValuesViewT() = delete;

    ValuesViewT(const Json& json, array_iterator begin, array_iterator end) : json_(json), begin_(begin), end_(end) {}

    ValuesViewT(const Json& json, object_iterator begin, object_iterator end) : json_(json), begin_(begin), end_(end) {}

    value_iterator begin() const noexcept { return begin_; }

    value_iterator end() const noexcept { return end_; }

    // Add `empty` and `size`, so that GoogleTest works.
    bool empty() const noexcept { return json_.empty(); }

    std::size_t size() const noexcept { return json_.size(); }

   private:
    const Json& json_;
    value_iterator begin_;
    value_iterator end_;
  };

 public:
  enum class Kind {
    kNull,    // IsNull
    kArray,   // IsArray
    kBool,    // IsBool
    kNumber,  // IsNumber = IsDouble || IsInteger
    kObject,  // IsObject
    kString,  // IsString
  };

  enum class SerializeMode {
    kCompact = static_cast<int>(types::Stringify::OutputMode::kJson),
    kLine = static_cast<int>(types::Stringify::OutputMode::kJsonLine),
    kPretty = static_cast<int>(types::Stringify::OutputMode::kJsonPretty),
  };

  using value_type = Json;

  using reference = Array::element_type::reference;
  using const_reference = Array::element_type::const_reference;
  using pointer = Array::element_type::pointer;
  using const_pointer = Array::element_type::const_pointer;
  using size_type = Array::element_type::size_type;
  using difference_type = Array::element_type::difference_type;

  using iterator = array_iterator;
  using const_iterator = const_array_iterator;
  using reverse_iterator = Array::element_type::reverse_iterator;
  using const_reverse_iterator = Array::element_type::const_reverse_iterator;

  using value_iterator = value_iterator_t<false>;
  using const_value_iterator = value_iterator_t<true>;

  using values_view = ValuesViewT<false>;
  using const_values_view = ValuesViewT<true>;

  template<typename Value>
  using ToKind = types::Cases<  // Ordered most precise to least preceise.
      types::IfThen<JsonUseNull<Value>, std::integral_constant<Kind, Kind::kNull>>,
      types::IfThen<JsonUseBool<Value>, std::integral_constant<Kind, Kind::kBool>>,
      types::IfThen<JsonUseString<Value>, std::integral_constant<Kind, Kind::kString>>,
      types::IfThen<JsonUseNumber<Value>, std::integral_constant<Kind, Kind::kNumber>>,
      types::IfThen<JsonUseObject<Value>, std::integral_constant<Kind, Kind::kObject>>,
      types::IfThen<JsonUseArray<Value>, std::integral_constant<Kind, Kind::kArray>>,
      types::IfElse<void>>;

  template<typename Value>
  static Kind GetKind(Value&& /*unused*/) {  // NOLINT(*-missing-std-forward)
    return ToKind<Value>::value;
  }

  ~Json() noexcept = default;

  Json() noexcept : data_{std::nullopt} {}

  Json(const Json& other) : Json() {
    std::visit(
        types::Overloaded{
            [&](const Object& /*unused*/) { CopyObject(other); },
            [&]<typename Other>(Other&& other) { Json::operator=(std::forward<Other>(other)); }},
        other.data_);
  }

  Json& operator=(const Json& other) {
    if (this != &other) {
      Json copy(other);
      data_ = std::move(copy.data_);
    }
    return *this;
  }

  Json(Json&&) noexcept = default;
  Json& operator=(Json&&) noexcept = default;

  explicit Json(std::nullopt_t /*value*/) noexcept : data_{std::nullopt} {}

  template<ConvertibleToJson Value>
  explicit Json(Value&& value) : data_{std::nullopt} {
    *this = std::forward<Value>(value);
  }

  Json& operator=(const std::unique_ptr<Json>& other) {  // NOLINT(misc-no-recursion)
    if (other == nullptr) {
      data_.emplace<std::nullopt_t>(std::nullopt);
    } else {
      *this = Json(*other);
    }
    return *this;
  }

  template<typename Value, typename = ToKind<Value>>
  Json& operator=(Value&& value) {  // NOLINT(misc-unconventional-assign-operator,*-signature): See above.
    CopyFrom(std::forward<Value>(value));
    return *this;
  }

  explicit Json(std::string_view value) : data_{std::nullopt} { data_.emplace<std::string>(value); }

  explicit operator bool() const noexcept { return !IsNull(); }

  bool IsNull() const noexcept { return std::holds_alternative<std::nullopt_t>(data_); }

  bool IsBool() const noexcept { return std::holds_alternative<bool>(data_); }

  bool IsFalse() const noexcept { return IsBool() && !std::get<bool>(data_); }

  bool IsTrue() const noexcept { return IsBool() && std::get<bool>(data_); }

  bool IsSignedInt() const noexcept { return std::holds_alternative<SignedInt>(data_); }

  bool IsUnsignedInt() const noexcept { return std::holds_alternative<UnsignedInt>(data_); }

  bool IsInteger() const noexcept { return IsSignedInt() || IsUnsignedInt(); }

  bool IsFloat() const noexcept { return std::holds_alternative<Float>(data_); }

  bool IsNumber() const noexcept { return IsInteger() || IsFloat(); }

  bool IsString() const noexcept { return std::holds_alternative<std::string>(data_); }

  bool IsArray() const noexcept { return std::holds_alternative<Array>(data_); }

  bool IsObject() const noexcept { return std::holds_alternative<Object>(data_); }

  Kind GetKind() const noexcept {
    if (IsNull()) {
      return Kind::kNull;
    } else if (IsArray()) {
      return Kind::kArray;
    } else if (IsBool()) {
      return Kind::kBool;
    } else if (IsNumber()) {
      return Kind::kNumber;
    } else if (IsObject()) {
      return Kind::kObject;
    } else if (IsString()) {
      return Kind::kString;
    }
    MBO_CONFIG_REQUIRE(false, "Bad type");
    return Kind::kNull;
  }

  std::ostream& Stream(
      std::ostream& os,
      SerializeMode mode = SerializeMode::kCompact,
      const types::StringifyRootOptions& root_options = types::StringifyRootOptions{}) const {
    const ::mbo::types::Stringify stringify{static_cast<types::Stringify::OutputMode>(mode), root_options};
    if (IsNull()) {
      struct Null {};

      stringify.Stream(os, Null{});
    } else {
      MBO_CONFIG_REQUIRE(IsObject(), "Only Objects can be serialized.");
      stringify.Stream(os, std::get<Object>(data_));
    }
    return os;
  }

  std::string Serialize(
      SerializeMode mode = SerializeMode::kCompact,
      const types::StringifyRootOptions& root_options = types::StringifyRootOptions{}) const {
    std::stringstream os;
    Stream(os, mode, root_options);
    return os.str();
  }

  // Change value to a `Null` value.
  Json& Reset() {
    data_.emplace<std::nullopt_t>(std::nullopt);
    return *this;
  }

  // Change value to an `Array`.
  Json& MakeArray() { return MakeType(std::make_unique<Array::element_type>()); }

  // Change value to an `Object`
  Json& MakeObject() { return MakeType(Object{}); }

  template<types::ConstructibleInto<std::string> Str>
  Json& MakeString(Str&& str = {}) {
    return MakeType(std::forward<Str>(str));
  }

  bool empty() const noexcept {
    if (IsArray()) {
      return std::get<Array>(data_)->empty();
    }
    if (IsObject()) {
      return std::get<Object>(data_).empty();
    }
    return true;
  }

  std::size_t size() const noexcept {
    if (IsArray()) {
      return std::get<Array>(data_)->size();
    }
    if (IsObject()) {
      return std::get<Object>(data_).size();
    }
    return 0;
  }

  void clear() {
    if (IsArray()) {
      std::get<Array>(data_)->clear();
    } else if (IsObject()) {
      std::get<Object>(data_).clear();
    } else {
      Reset();
    }
  }

  bool contains(std::string_view property) const noexcept {
    return IsObject() && std::get<Object>(data_).contains(property);
  }

  template<typename Arg>
  requires(types::ConstructibleFrom<Json, Arg>)
  Json& emplace_back(Arg&& arg) {
    MakeArray();
    auto& values = std::get<Array>(data_);
    values->emplace_back(std::forward<Arg>(arg));
    return values->back();
  }

  template<typename Arg>
  requires(types::ConstructibleFrom<Json, Arg>)
  Json& emplace(std::string_view property, Arg&& arg) {
    MakeObject();
    auto& properties = std::get<Object>(data_);
    return *properties.emplace(property, std::make_unique<Json>(std::forward<Arg>(arg))).first->second;
  }

  template<typename Arg>
  requires(types::ConstructibleFrom<Json, Arg>)
  void push_back(Arg&& arg) {
    MakeArray();
    auto& values = std::get<Array>(data_);
    values->emplace_back(std::forward<Arg>(arg));
  }

  void pop_back() {
    if (IsArray()) {
      auto& values = std::get<Array>(data_);
      values->pop_back();
    } else {
      MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array or Null.");
    }
  }

  // NOLINTBEGIN(*-avoid-unchecked-container-access): Json's own accessors. Each
  // either bounds-checks first (MBO_CONFIG_REQUIRE) or inserts; the subscripts
  // below are the checked implementation, not unchecked use of it.
  Json& operator[](std::size_t index) {
    MakeArray();
    MBO_CONFIG_REQUIRE(index < size(), "Out of range.");
    return (*std::get<Array>(data_))[index];
  }

  const Json& operator[](std::size_t index) const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    MBO_CONFIG_REQUIRE(index < size(), "Out of range.");
    return (*std::get<Array>(data_))[index];
  }

  Json& operator[](std::string_view property) {
    MakeObject();
    auto [it, inserted] = std::get<Object>(data_).emplace(property, nullptr);
    if (inserted) {
      it->second = std::make_unique<Json>();
    }
    return *it->second;
  }

  const Json& operator[](std::string_view property) const {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    MBO_CONFIG_REQUIRE(contains(property), "Property not present:") << "'" << property << "'.";
    return *(std::get<Object>(data_)).at(property);
  }

  Json& at(std::size_t index) { return (*this)[index]; }

  const Json& at(std::size_t index) const { return (*this)[index]; }

  Json& at(std::string_view property) {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    MBO_CONFIG_REQUIRE(contains(property), "Property not present:") << "'" << property << "'.";
    return *std::get<Object>(data_).at(property);
  }

  const Json& at(std::string_view property) const { return (*this)[property]; }

  // NOLINTEND(*-avoid-unchecked-container-access)

  iterator erase(const_iterator pos) {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->erase(pos);
  }

  iterator erase(const_iterator first, const_iterator last) {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->erase(first, last);
  }

  value_iterator erase(const_value_iterator pos) {
    if (IsArray()) {
      return value_iterator{std::get<Array>(data_)->erase(pos)};
    }
    MBO_CONFIG_REQUIRE(IsObject(), "Is neither Array nor Object.");
    const_value_iterator end(pos);
    return value_iterator{std::get<Object>(data_).erase(pos, ++end)};
  }

  value_iterator erase(const_value_iterator first, const_value_iterator last) {
    if (IsArray()) {
      return value_iterator{std::get<Array>(data_)->erase(first, last)};
    }
    MBO_CONFIG_REQUIRE(IsObject(), "Is neither Array nor Object.");
    return value_iterator{std::get<Object>(data_).erase(first, last)};
  }

  size_type erase(std::string_view property) {
    if (IsObject()) {
      return std::get<Object>(data_).erase(property);
    }
    return 0;
  }

  void resize(size_type count) {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    std::get<Array>(data_)->resize(count);
  }

  template<ConvertibleToJson Value>
  void resize(size_type count, const Value& value) {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    std::get<Array>(data_)->resize(count, Json(value));
  }

  iterator begin() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->begin();
  }

  const_iterator begin() const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->begin();
  }

  const_iterator cbegin() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->cbegin();
  }

  iterator end() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->end();
  }

  const_iterator end() const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->end();
  }

  const_iterator cend() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->cend();
  }

  reverse_iterator rbegin() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->rbegin();
  }

  const_reverse_iterator rbegin() const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->rbegin();
  }

  const_reverse_iterator crbegin() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->crbegin();
  }

  reverse_iterator rend() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->rend();
  }

  const_reverse_iterator rend() const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->rend();
  }

  const_reverse_iterator crend() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::get<Array>(data_)->crend();
  }

  auto values() -> values_view {
    if (IsArray()) {
      auto& array = std::get<Array>(data_);
      MBO_CONFIG_REQUIRE_DEBUG(array != nullptr, "May not be nulltr.");
      std::cout << "Array Size: " << array->size() << "\n" << std::flush;
      return {*this, array->begin(), array->end()};
    }
    MBO_CONFIG_REQUIRE(IsObject(), "Is neither Array nor Object.");
    auto& object = std::get<Object>(data_);
    std::cout << "Object Size: " << object.size() << "\n" << std::flush;
    return {*this, object.begin(), object.end()};
  }

  auto values() const -> const_values_view {
    if (IsArray()) {
      const auto& array = std::get<Array>(data_);
      return {*this, array->begin(), array->end()};
    }
    MBO_CONFIG_REQUIRE(IsObject(), "Is neither Array nor Object.");
    const auto& object = std::get<Object>(data_);
    return {*this, object.begin(), object.end()};
  }

  auto array_values() {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::views::all(*std::get<Array>(data_));
  }

  auto array_values() const {
    MBO_CONFIG_REQUIRE(IsArray(), "Is not an Array.");
    return std::views::all(*std::get<Array>(data_));
  }

  auto property_names() const {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    return std::views::keys(std::get<Object>(data_));
  }

  auto property_pairs() {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    return std::views::transform(
        std::get<Object>(data_),
        [](Object::reference v) -> std::pair<const std::string&, Json&> { return {v.first, *v.second}; });
  }

  auto property_pairs() const {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    return std::views::transform(
        std::get<Object>(data_),
        [](Object::const_reference v) -> std::pair<const std::string&, const Json&> { return {v.first, *v.second}; });
  }

  auto property_values() {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    return std::views::transform(std::get<Object>(data_), [](Object::const_reference v) -> Json& { return *v.second; });
  }

  auto property_values() const {
    MBO_CONFIG_REQUIRE(IsObject(), "Is not an Object.");
    return std::views::transform(
        std::get<Object>(data_), [](Object::const_reference v) -> const Json& { return *v.second; });
  }

  bool operator==(std::nullopt_t /*unused*/) const noexcept { return IsNull(); }

  std::strong_ordering operator<=>(const Json& other) const noexcept { return Compare(other); }

  bool operator==(const Json& other) const noexcept { return Compare(other) == std::strong_ordering::equal; }

  bool operator<(const Json& other) const noexcept { return Compare(other) == std::strong_ordering::less; }

  template<ConvertibleToJson Value>
  std::strong_ordering operator<=>(const Value& other) const noexcept {
    return Compare(other);
  }

  template<ConvertibleToJson Value>
  bool operator==(const Value& other) const noexcept {
    return Compare(other) == std::strong_ordering::equal;
  }

  template<ConvertibleToJson Value>
  bool operator<(const Value& other) const noexcept {
    return Compare(other) == std::strong_ordering::less;
  }

  friend const Variant& MboTypesStringifyValueAccess(
      const Json& json,
      const types::StringifyFieldOptions& /*options*/) {
    return json.data_;
  }

 private:
  template<typename T>
  bool IsType() const noexcept {
    return std::holds_alternative<T>(data_);
  }

  template<typename T>
  inline Json& MakeType(T&& value);

  template<typename Value, typename = ToKind<Value>>
  void CopyFrom(Value&& value) {
    if constexpr (JsonUseNull<Value>) {
      data_.emplace<std::nullopt_t>(std::nullopt);
    } else if constexpr (JsonUseBool<Value>) {
      data_.emplace<bool>(value);
    } else if constexpr (JsonUseString<Value>) {
      data_.emplace<std::string>(std::forward<Value>(value));
    } else if constexpr (json_internal::JsonUseSignedInt<Value>) {
      data_.emplace<SignedInt>(value);
    } else if constexpr (json_internal::JsonUseUnsignedInt<Value>) {
      data_.emplace<UnsignedInt>(value);
    } else if constexpr (json_internal::JsonUseFloat<Value>) {
      data_.emplace<Float>(value);
    } else if constexpr (JsonUseArray<Value>) {
      data_.emplace<Array>(std::make_unique<Array::element_type>(*value));
    } else if constexpr (JsonUseObject<Value>) {
      CopyObject(value);
    } else {
      MBO_CONFIG_REQUIRE(false, "Bad type: ") << log::DemangleV(value);
    }
  }

  void CopyObject(const Json& other) {         // NOLINT(misc-no-recursion)
    Object& object = data_.emplace<Object>();  // NOLINT(*-auto)
    for (const auto& [name, value] : std::get<Object>(other.data_)) {
      // Object aliases an absl map; map insert: operator[] creates the entry, so there is no range to exceed.
      // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
      std::unique_ptr<Json>& ref = object[name];
      if (ref == nullptr) {
        ref = std::make_unique<Json>();
      }
      *ref = value;
    }
  }

  std::strong_ordering Compare(const Json& other) const noexcept;

  template<ConvertibleToJson Value>
  std::strong_ordering Compare(const Value& other) const noexcept;

  Variant data_;
};

static_assert(types::ThreeWayComparableTo<Json, int>);
static_assert(types::ThreeWayComparableTo<Json, unsigned>);

namespace json_internal {

inline std::strong_ordering operator<=>(const std::unique_ptr<Json>& lhs, const std::unique_ptr<Json>& rhs) {
  // Uses `!= nullptr` so that the `nullptr < non-nullptr`. NOLINTNEXTLINE(readability-implicit-bool-conversion)
  if (const auto comp = (lhs != nullptr) <=> (rhs != nullptr); comp != std::strong_ordering::equal) {
    return comp;
  }
  if (lhs == nullptr) {
    return std::strong_ordering::equal;
  }
  return types::WeakToStrong(*lhs <=> *rhs);
}

template<typename T>
concept IsNullopt = std::same_as<std::remove_cvref_t<T>, std::nullopt_t>;

template<typename T>
concept NotNullopt = !IsNullopt<T>;

constexpr auto kJsonComparator = mbo::types::Overloaded{
    // NOLINTBEGIN(*-implicit-bool-conversion,*-narrowing-conversions)
    // The left hand side is always a variant-member of `Json::Variant`.
    [](const std::nullopt_t /*unused*/, const std::nullopt_t /*unused*/) { return std::strong_ordering::equal; },
    [](bool lhs, bool rhs) -> std::strong_ordering { return lhs <=> rhs; },
    // LCOV_MERGE_FUNC_LINE: repeated for every compatible arithmetic right-hand type.
    [](SignedInt lhs, types::IsArithmetic auto rhs) { return types::CompareArithmetic(lhs, rhs); },
    // LCOV_MERGE_FUNC_LINE: repeated for every compatible arithmetic right-hand type.
    [](UnsignedInt lhs, types::IsArithmetic auto rhs) { return types::CompareArithmetic(lhs, rhs); },
    // LCOV_MERGE_FUNC_LINE: repeated for every compatible arithmetic right-hand type.
    [](Float lhs, types::IsArithmetic auto rhs) { return types::CompareArithmetic(lhs, rhs); },
    [](const std::string& lhs, const JsonUseString auto& rhs) {
      return types::WeakToStrong(std::string_view(lhs) <=> std::string_view(rhs));
    },
    [](const Json::Array& lhs, const Json::Array& rhs) { return types::WeakToStrong(*lhs <=> *rhs); },
    [](const Json::Object& lhs, const Json::Object& rhs) -> std::strong_ordering {
      auto lhs_it = lhs.begin();
      auto rhs_it = rhs.begin();
      while (lhs_it != lhs.end() && rhs_it != rhs.end()) {
        if (auto comp = types::WeakToStrong(lhs_it->first <=> rhs_it->first); comp != std::strong_ordering::equal) {
          return comp;
        }
        if (auto comp = lhs_it->second <=> rhs_it->second; comp != std::strong_ordering::equal) {
          return comp;
        }
        ++lhs_it;
        ++rhs_it;
      }
      return lhs.size() <=> rhs.size();
    },
    []<typename LhsOther, typename RhsOther>(  // LCOV_EXCL_FUNC_LINE, LCOV_EXCL_LINE: generated cross-products.
        const LhsOther& lhs,
        const RhsOther& rhs) -> std::strong_ordering {
      if constexpr (IsNullopt<LhsOther> || IsNullopt<RhsOther>) {
        return NotNullopt<LhsOther> <=> NotNullopt<RhsOther>;
      } else {
        MBO_CONFIG_REQUIRE(false, "Missing explicit comparison implementation: ")
            << log::DemangleV(lhs) << " <=> " << log::DemangleV(rhs);
        return std::strong_ordering::equal;
      }
    },
    // NOLINTEND(*-implicit-bool-conversion,*-narrowing-conversions)
};

}  // namespace json_internal

template<typename T>
inline Json& Json::MakeType(T&& value) {  // NOLINT(*-function-cognitive-complexity)
  if constexpr (types::ConstructibleFrom<std::string, T>) {
    if (IsString()) {
      return *this;
    }
    MBO_CONFIG_REQUIRE(IsNull(), "Is not an std::string or Null.");
    data_.emplace<std::string>(std::forward<T>(value));
  } else {
    if (IsType<T>()) {
      return *this;
    }
    using RawT = std::remove_cvref_t<T>;
    if constexpr (std::same_as<RawT, std::nullopt_t>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not Null.");
    } else if constexpr (std::same_as<RawT, Array>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not an Array or Null.");
      MBO_CONFIG_REQUIRE(value != nullptr, "May not be a nullptr.");
    } else if constexpr (std::same_as<RawT, bool>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not a bool or Null.");
    } else if constexpr (std::same_as<RawT, Float>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not an Float or Null.");
    } else if constexpr (std::same_as<RawT, SignedInt>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not an SignedInt or Null.");
    } else if constexpr (std::same_as<RawT, UnsignedInt>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not an UnsignedInt or Null.");
    } else if constexpr (std::same_as<RawT, Object>) {
      MBO_CONFIG_REQUIRE(IsNull(), "Is not an Object or Null.");
    }
    data_.emplace<RawT>(std::forward<T>(value));
  }
  return *this;
}

inline std::strong_ordering Json::Compare(const Json& other) const noexcept {
  if (auto comp = GetKind() <=> other.GetKind(); comp != std::strong_ordering::equal) {
    return comp;
  }
  return std::visit(json_internal::kJsonComparator, data_, other.data_);
}

template<ConvertibleToJson Value>
inline std::strong_ordering Json::Compare(const Value& other) const noexcept {
  if (const auto comp = GetKind() <=> Json::GetKind(other); comp != std::strong_ordering::equal) {
    return comp;
  }
  return std::visit(
      [&other]<typename Lhs>(  // LCOV_EXCL_FUNC_LINE: unreachable variant alternatives for this value type.
          const Lhs& lhs) { return json_internal::kJsonComparator(lhs, other); },
      data_);
}

static_assert(types::HasMboTypesStringifyValueAccess<Json>);
static_assert(!types::HasMboTypesStringifyValueAccess<Json::Variant>);
static_assert(!types::HasMboTypesStringifyValueAccess<Json::Object::value_type>);

}  // namespace mbo::json

// NOLINTEND(readability-identifier-naming)

#endif  // MBO_JSON_JSON_H_
