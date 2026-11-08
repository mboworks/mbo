// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_TRAITS_H_
#define MBO_STRINGS_STRING_INTERNER_TRAITS_H_

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace mbo::strings {

// Whether insertion preserves existing key addresses. Unknown containers default to false;
// custom containers may specialize this only when every insertion preserves the guarantee.
template<typename Container>
inline constexpr bool kHasStableKeyAddresses = false;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::map<Args...>> = true;
template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::set<Args...>> = true;
template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::unordered_map<Args...>> = true;
template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::unordered_set<Args...>> = true;
template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<absl::node_hash_map<Args...>> = true;
template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<absl::node_hash_set<Args...>> = true;

template<typename Container>
struct StringInternerContainerTraits {
  using KeyType = std::remove_const_t<typename Container::key_type>;
  static constexpr bool kIsMap = requires { typename Container::mapped_type; };

  template<typename Entry>
  static constexpr decltype(auto) GetKey(Entry&& entry) noexcept {
    if constexpr (kIsMap) {
      return (std::forward<Entry>(entry).first);
    } else {
      return std::forward<Entry>(entry);
    }
  }
};

template<typename Container>
using StringInternerKey = StringInternerContainerTraits<Container>::KeyType;

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_TRAITS_H_
