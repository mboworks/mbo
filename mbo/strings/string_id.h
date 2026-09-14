// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_ID_H_
#define MBO_STRINGS_STRING_ID_H_

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace mbo::strings {

template<typename T>
concept StringIdRepresentation = std::same_as<T, std::uint8_t> || std::same_as<T, std::uint16_t>
                                 || std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

// Dense ordinal, not a hash. Every representable value, including zero, is valid.
template<StringIdRepresentation Representation = std::uint32_t>
class StringId final {
 public:
  using representation_type = Representation;

  constexpr StringId() noexcept = default;

  explicit constexpr StringId(Representation value) noexcept : value_(value) {}

  template<std::unsigned_integral Ordinal>
  requires(!std::same_as<Ordinal, bool>)
  [[nodiscard]] static constexpr std::optional<StringId> TryFromOrdinal(Ordinal ordinal) noexcept {
    if (std::cmp_greater(ordinal, std::numeric_limits<Representation>::max())) {
      return std::nullopt;
    }
    return StringId(static_cast<Representation>(ordinal));
  }

  constexpr Representation value() const noexcept { return value_; }

  friend constexpr auto operator<=>(const StringId&, const StringId&) noexcept = default;

 private:
  Representation value_ = 0;
};

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_ID_H_
