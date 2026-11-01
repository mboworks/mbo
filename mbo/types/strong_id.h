// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_TYPES_STRONG_ID_H_
#define MBO_TYPES_STRONG_ID_H_

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

#include "mbo/config/config.h"
#include "mbo/types/internal/strong_integral.h"

namespace mbo::types {

template<typename T>
concept IsStrongIdRepresentation = std::integral<T> && !std::same_as<T, bool>;

// Nominal integral ID with an explicit invalid sentinel. Unlike an ordinal, its default value is
// invalid and it deliberately exposes no arithmetic API.
// NOLINTBEGIN(readability-identifier-naming): Strong IDs follow STL value-wrapper vocabulary.
template<
    typename Tag,
    IsStrongIdRepresentation Representation = std::uint32_t,
    Representation InvalidValue = std::numeric_limits<Representation>::max()>
class MBO_CONFIG_TRIVIAL_ABI ConstStrongId final
    : public types_internal::
          StrongIntegralBase<ConstStrongId<Tag, Representation, InvalidValue>, Tag, Representation, InvalidValue> {
 private:
  using Base = types_internal::StrongIntegralBase<ConstStrongId, Tag, Representation, InvalidValue>;

 public:
  using typename Base::tag_type;
  using typename Base::value_type;

  static constexpr value_type invalid_value = InvalidValue;

  constexpr ConstStrongId() noexcept = default;
  constexpr ConstStrongId(const ConstStrongId&) noexcept = default;
  constexpr ConstStrongId(ConstStrongId&&) noexcept = default;
  constexpr ConstStrongId& operator=(const ConstStrongId&) = delete;
  constexpr ConstStrongId& operator=(ConstStrongId&&) noexcept = default;
  ~ConstStrongId() = default;

  explicit constexpr ConstStrongId(value_type value) noexcept : Base(value) {}

  template<std::integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  [[nodiscard]] static constexpr std::optional<ConstStrongId> try_from_ordinal(Ordinal ordinal) noexcept {
    if (!std::in_range<value_type>(ordinal)) {
      return std::nullopt;
    }
    const auto value = static_cast<value_type>(ordinal);
    if (value == invalid_value) {
      return std::nullopt;
    }
    return ConstStrongId(value);
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept { return this->value() != invalid_value; }
};

template<typename Type, typename Tag = void>
concept IsConstStrongId = requires {
  typename Type::tag_type;
  typename Type::value_type;
  requires IsStrongIdRepresentation<typename Type::value_type>;
  requires(std::same_as<Tag, void> || std::same_as<Tag, typename Type::tag_type>);
  requires std::same_as<Type, ConstStrongId<typename Type::tag_type, typename Type::value_type, Type::invalid_value>>;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::types

namespace std {

template<typename Tag, mbo::types::IsStrongIdRepresentation Representation, Representation InvalidValue>
struct hash<mbo::types::ConstStrongId<Tag, Representation, InvalidValue>> {
  constexpr std::size_t operator()(
      mbo::types::ConstStrongId<Tag, Representation, InvalidValue> identifier) const noexcept {
    return std::hash<Representation>{}(identifier.value());
  }
};

}  // namespace std

#endif  // MBO_TYPES_STRONG_ID_H_
