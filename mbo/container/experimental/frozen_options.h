// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_FROZEN_OPTIONS_H_
#define MBO_CONTAINER_EXPERIMENTAL_FROZEN_OPTIONS_H_

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace mbo::container::experimental {

// Construction work is bounded independently of the compiler's constexpr limit.
struct FrozenOptions {
  std::size_t capacity = 0;
  // Zero selects twice capacity. Set to capacity for a minimal table when every input is unique.
  std::size_t slots = 0;
  std::size_t max_seed = 256;
  std::size_t max_work = 65'536;
  std::size_t max_key_bytes = 65'536;
};

namespace frozen_internal {

constexpr std::uint64_t Mix(std::uint64_t value) noexcept {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

template<auto CapacityOrOptions>
consteval FrozenOptions Options() {
  if constexpr (std::same_as<decltype(CapacityOrOptions), FrozenOptions>) {
    return CapacityOrOptions;
  } else {
    static_assert(std::integral<decltype(CapacityOrOptions)>);
    static_assert(CapacityOrOptions >= 0, "Frozen capacity must be nonnegative");
    return {.capacity = static_cast<std::size_t>(CapacityOrOptions)};
  }
}

}  // namespace frozen_internal

// Default hashing is deliberately independent of std::hash and process-specific hash seeds.
// Specialize this template, or pass a hash object, for other key types. Equal keys must hash equally.
template<typename Key>
struct FrozenHash;

template<std::integral Key>
struct FrozenHash<Key> {
  constexpr std::uint64_t operator()(Key key) const noexcept {
    return frozen_internal::Mix(static_cast<std::uint64_t>(key));
  }
};

template<typename Key>
requires std::is_enum_v<Key>
struct FrozenHash<Key> {
  constexpr std::uint64_t operator()(Key key) const noexcept {
    return FrozenHash<std::underlying_type_t<Key>>{}(std::to_underlying(key));
  }
};

template<>
struct FrozenHash<std::string_view> {
  using is_transparent = void;  // NOLINT(readability-identifier-naming)

  constexpr std::uint64_t operator()(std::string_view key) const noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char byte : key) {
      hash ^= static_cast<unsigned char>(byte);
      hash *= 0x100000001b3ULL;
    }
    return frozen_internal::Mix(hash);
  }
};

}  // namespace mbo::container::experimental

#endif  // MBO_CONTAINER_EXPERIMENTAL_FROZEN_OPTIONS_H_
