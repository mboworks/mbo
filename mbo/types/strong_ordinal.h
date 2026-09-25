// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_TYPES_STRONG_ORDINAL_H_
#define MBO_TYPES_STRONG_ORDINAL_H_

#include <concepts>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>

#include "mbo/config/config.h"
#include "mbo/config/require.h"
#include "mbo/types/internal/strong_integral.h"

namespace mbo::types {

template<typename T>
concept StrongOrdinalRepresentation = types_internal::StrongIntegralRepresentation<T>;

template<typename Tag, StrongOrdinalRepresentation Representation>
class StrongOrdinal;

// A zero-defaulted, nominally typed ordinal. It is non-copy-assignable and has no mutating API;
// move assignment remains available for container compatibility. Every representation value is a
// valid ordinal. Checked arithmetic fails only at representation boundaries.
// NOLINTBEGIN(readability-identifier-naming): Strong ordinals follow STL value-wrapper vocabulary.
template<typename Tag, StrongOrdinalRepresentation Representation>
class MBO_CONFIG_TRIVIAL_ABI ConstStrongOrdinal
    : public types_internal::StrongIntegralBase<ConstStrongOrdinal<Tag, Representation>, Tag, Representation, 0> {
 private:
  using Base = types_internal::StrongIntegralBase<ConstStrongOrdinal, Tag, Representation, 0>;

 public:
  using typename Base::tag_type;
  using typename Base::value_type;

  constexpr ConstStrongOrdinal() noexcept = default;
  constexpr ConstStrongOrdinal(const ConstStrongOrdinal&) noexcept = default;
  constexpr ConstStrongOrdinal(ConstStrongOrdinal&&) noexcept = default;
  constexpr ConstStrongOrdinal& operator=(const ConstStrongOrdinal&) = delete;
  constexpr ConstStrongOrdinal& operator=(ConstStrongOrdinal&&) noexcept = default;
  ~ConstStrongOrdinal() = default;

  explicit constexpr ConstStrongOrdinal(value_type value) noexcept : Base(value) {}

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  [[nodiscard]] static constexpr std::optional<ConstStrongOrdinal> try_from_ordinal(Ordinal ordinal) noexcept {
    if (!std::in_range<value_type>(ordinal)) {
      return std::nullopt;
    }
    return ConstStrongOrdinal(static_cast<value_type>(ordinal));
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr std::optional<ConstStrongOrdinal> try_add(Offset amount) const noexcept {
    ConstStrongOrdinal result = *this;
    if (!result.try_add_value(amount)) {
      return std::nullopt;
    }
    return result;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr std::optional<ConstStrongOrdinal> try_subtract(Offset amount) const noexcept {
    ConstStrongOrdinal result = *this;
    if (!result.try_subtract_value(amount)) {
      return std::nullopt;
    }
    return result;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr ConstStrongOrdinal operator+(Offset amount) const noexcept(!::mbo::config::kRequireThrows) {
    ConstStrongOrdinal result = *this;
    MBO_CONFIG_REQUIRE(result.try_add_value(amount), "ConstStrongOrdinal addition would overflow");
    return result;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr ConstStrongOrdinal operator-(Offset amount) const noexcept(!::mbo::config::kRequireThrows) {
    ConstStrongOrdinal result = *this;
    MBO_CONFIG_REQUIRE(result.try_subtract_value(amount), "ConstStrongOrdinal subtraction would underflow");
    return result;
  }

 private:
  friend class StrongOrdinal<Tag, Representation>;

  using Base::set_value;

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr bool try_add_value(Offset amount) noexcept {
    if (std::cmp_less(amount, 0)) {
      return false;
    }
    using unsigned_offset = std::make_unsigned_t<Offset>;
    using unsigned_value = std::make_unsigned_t<value_type>;
    const auto increment = static_cast<unsigned_offset>(amount);
    const auto current = static_cast<unsigned_value>(this->value());
    const auto available =
        static_cast<unsigned_value>(static_cast<unsigned_value>(std::numeric_limits<value_type>::max()) - current);
    if (std::cmp_greater(increment, available)) {
      return false;
    }
    set_value(static_cast<value_type>(static_cast<unsigned_value>(current + static_cast<unsigned_value>(increment))));
    return true;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr bool try_subtract_value(Offset amount) noexcept {
    if (std::cmp_less(amount, 0)) {
      return false;
    }
    using unsigned_offset = std::make_unsigned_t<Offset>;
    using unsigned_value = std::make_unsigned_t<value_type>;
    const auto decrement = static_cast<unsigned_offset>(amount);
    const auto current = static_cast<unsigned_value>(this->value());
    const auto available =
        static_cast<unsigned_value>(current - static_cast<unsigned_value>(std::numeric_limits<value_type>::lowest()));
    if (std::cmp_greater(decrement, available)) {
      return false;
    }
    set_value(static_cast<value_type>(static_cast<unsigned_value>(current - static_cast<unsigned_value>(decrement))));
    return true;
  }
};

// Mutable ordinal extension. The try_* mutations are non-throwing and leave the value unchanged on
// failure. Ordinary assignment and arithmetic enforce the same bounds through MBO_CONFIG_REQUIRE.
template<typename Tag, StrongOrdinalRepresentation Representation>
class MBO_CONFIG_TRIVIAL_ABI StrongOrdinal : public ConstStrongOrdinal<Tag, Representation> {
 private:
  using Base = ConstStrongOrdinal<Tag, Representation>;

 public:
  using Base::Base;
  using typename Base::tag_type;
  using typename Base::value_type;

  constexpr StrongOrdinal() noexcept = default;
  constexpr StrongOrdinal(const StrongOrdinal&) noexcept = default;
  constexpr StrongOrdinal(StrongOrdinal&&) noexcept = default;

  constexpr StrongOrdinal& operator=(const StrongOrdinal& other) noexcept {
    if (this != &other) {
      this->set_value(other.value());
    }
    return *this;
  }

  constexpr StrongOrdinal& operator=(StrongOrdinal&& other) noexcept {
    this->set_value(other.value());
    return *this;
  }

  ~StrongOrdinal() = default;

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  [[nodiscard]] static constexpr std::optional<StrongOrdinal> try_from_ordinal(Ordinal ordinal) noexcept {
    if (!std::in_range<value_type>(ordinal)) {
      return std::nullopt;
    }
    return StrongOrdinal(static_cast<value_type>(ordinal));
  }

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  [[nodiscard]] constexpr bool try_set(Ordinal ordinal) noexcept {
    if (!std::in_range<value_type>(ordinal)) {
      return false;
    }
    this->set_value(static_cast<value_type>(ordinal));
    return true;
  }

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  constexpr void set(Ordinal ordinal) noexcept(!::mbo::config::kRequireThrows) {
    MBO_CONFIG_REQUIRE(try_set(ordinal), "StrongOrdinal cannot be set outside its representation range");
  }

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  constexpr StrongOrdinal& operator=(Ordinal ordinal) noexcept(!::mbo::config::kRequireThrows) {
    set(ordinal);
    return *this;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr bool try_add_assign(Offset amount) noexcept {
    return this->try_add_value(amount);
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr bool try_subtract_assign(Offset amount) noexcept {
    return this->try_subtract_value(amount);
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  constexpr StrongOrdinal& operator+=(Offset amount) noexcept(!::mbo::config::kRequireThrows) {
    MBO_CONFIG_REQUIRE(try_add_assign(amount), "StrongOrdinal addition would overflow");
    return *this;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  constexpr StrongOrdinal& operator-=(Offset amount) noexcept(!::mbo::config::kRequireThrows) {
    MBO_CONFIG_REQUIRE(try_subtract_assign(amount), "StrongOrdinal subtraction would underflow");
    return *this;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr StrongOrdinal operator+(Offset amount) const noexcept(!::mbo::config::kRequireThrows) {
    StrongOrdinal result = *this;
    result += amount;
    return result;
  }

  template<std::integral Offset>
  requires(!std::same_as<Offset, bool>)
  [[nodiscard]] constexpr StrongOrdinal operator-(Offset amount) const noexcept(!::mbo::config::kRequireThrows) {
    StrongOrdinal result = *this;
    result -= amount;
    return result;
  }

  [[nodiscard]] constexpr bool try_increment() noexcept { return try_add_assign(1); }

  [[nodiscard]] constexpr bool try_decrement() noexcept { return try_subtract_assign(1); }

  constexpr StrongOrdinal& operator++() noexcept(!::mbo::config::kRequireThrows) {
    MBO_CONFIG_REQUIRE(try_increment(), "StrongOrdinal increment would overflow");
    return *this;
  }

  constexpr StrongOrdinal operator++(int) noexcept(!::mbo::config::kRequireThrows) {
    StrongOrdinal previous = *this;
    ++*this;
    return previous;
  }

  constexpr StrongOrdinal& operator--() noexcept(!::mbo::config::kRequireThrows) {
    MBO_CONFIG_REQUIRE(try_decrement(), "StrongOrdinal decrement would underflow");
    return *this;
  }

  constexpr StrongOrdinal operator--(int) noexcept(!::mbo::config::kRequireThrows) {
    StrongOrdinal previous = *this;
    --*this;
    return previous;
  }
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::types

namespace std {

template<typename Tag, mbo::types::StrongOrdinalRepresentation Representation>
struct hash<mbo::types::ConstStrongOrdinal<Tag, Representation>> {
  constexpr std::size_t operator()(mbo::types::ConstStrongOrdinal<Tag, Representation> value) const noexcept {
    return std::hash<Representation>{}(value.value());
  }
};

template<typename Tag, mbo::types::StrongOrdinalRepresentation Representation>
struct hash<mbo::types::StrongOrdinal<Tag, Representation>> {
  constexpr std::size_t operator()(mbo::types::StrongOrdinal<Tag, Representation> value) const noexcept {
    return std::hash<Representation>{}(value.value());
  }
};

}  // namespace std

#endif  // MBO_TYPES_STRONG_ORDINAL_H_
