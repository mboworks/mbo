// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_TYPES_INTERNAL_STRONG_INTEGRAL_H_
#define MBO_TYPES_INTERNAL_STRONG_INTEGRAL_H_

#include <compare>
#include <concepts>
#include <utility>

#include "mbo/config/config.h"

namespace mbo::types::types_internal {

template<typename T>
concept StrongIntegralRepresentation = std::integral<T> && !std::same_as<T, bool>;

// Shared nominal-integral storage. Validity and arithmetic belong to the public semantic wrappers,
// not to this representation layer.
// NOLINTBEGIN(readability-identifier-naming): Strong integrals expose STL value-wrapper vocabulary.
template<typename Derived, typename Tag, StrongIntegralRepresentation Representation, Representation DefaultValue>
class MBO_CONFIG_TRIVIAL_ABI StrongIntegralBase {
 public:
  using tag_type = Tag;
  using value_type = Representation;

  static constexpr value_type default_value = DefaultValue;

  constexpr StrongIntegralBase() noexcept = default;
  constexpr StrongIntegralBase(const StrongIntegralBase&) noexcept = default;
  constexpr StrongIntegralBase(StrongIntegralBase&&) noexcept = default;
  constexpr StrongIntegralBase& operator=(const StrongIntegralBase&) = delete;
  constexpr StrongIntegralBase& operator=(StrongIntegralBase&&) noexcept = default;
  ~StrongIntegralBase() = default;

  [[nodiscard]] constexpr value_type value() const noexcept { return value_; }

  friend constexpr bool operator==(Derived lhs, Derived rhs) noexcept { return lhs.value() == rhs.value(); }

  friend constexpr auto operator<=>(Derived lhs, Derived rhs) noexcept { return lhs.value() <=> rhs.value(); }

  template<typename H>
  friend H AbslHashValue(H hash, Derived value) {
    return H::combine(std::move(hash), value.value());
  }

 protected:
  explicit constexpr StrongIntegralBase(value_type value) noexcept : value_(value) {}

  constexpr void set_value(value_type value) noexcept { value_ = value; }

 private:
  value_type value_ = default_value;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::types::types_internal

#endif  // MBO_TYPES_INTERNAL_STRONG_INTEGRAL_H_
