// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_KEY_OF_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_KEY_OF_H_

#include <functional>
#include <type_traits>
#include <utility>

namespace mbo::container::container_internal {

// Reuses the map/set key extractor over separately owned payload values.
// Handles in a valid HAMT must be nonempty; returned references borrow payloads.
template<typename KeyOf>
struct HamtNodeKeyOf final {
  template<typename Handle>
  requires(
      noexcept(std::declval<const Handle&>().get())
      && std::is_nothrow_invocable_v<const KeyOf&, decltype(*std::declval<const Handle&>().get())>)
  constexpr decltype(auto) operator()(const Handle& handle) const noexcept {
    return std::invoke(key_of, *handle.get());
  }

  KeyOf key_of{};
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_KEY_OF_H_
