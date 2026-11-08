// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_INTERNAL_STRING_INTERNER_TRAITS_H_
#define MBO_STRINGS_INTERNAL_STRING_INTERNER_TRAITS_H_

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "mbo/container/segmented_vector.h"
#include "mbo/strings/string_interner_traits.h"

namespace mbo::strings::strings_internal {

template<typename Element>
struct StringInternerElementTraits;

template<typename Key>
struct StringInternerElementTraits<const Key> {
  using KeyType = Key;
  using Entry = std::conditional_t<std::same_as<Key, std::string_view>, Key, const Key*>;

  template<typename Slot>
  requires std::same_as<std::remove_const_t<Slot>, Entry>
  static const Key& GetElement(Slot& entry) noexcept {
    if constexpr (std::same_as<Key, std::string_view>) {
      return entry;
    } else {
      return *entry;
    }
  }

  static const Key& GetElement(const Entry&&) = delete;

  template<typename StoredKey>
  requires std::same_as<std::remove_const_t<StoredKey>, Key>
  static const Key& GetKey(StoredKey& key) noexcept {
    return key;
  }

  static const Key& GetKey(const Key&&) = delete;

  static void Publish(Entry& entry, const Key& key) noexcept {
    if constexpr (std::same_as<Key, std::string_view>) {
      entry = key;
    } else {
      entry = std::addressof(key);
    }
  }
};

template<typename Key, typename Value>
requires(!std::is_lvalue_reference_v<Key> || std::is_const_v<std::remove_reference_t<Key>>)
struct StringInternerElementTraits<std::pair<Key, Value>> {
  using KeyType = std::remove_cvref_t<Key>;
  using Element = std::pair<const Key, Value>;
  using Entry = std::optional<Element>;

  template<typename Slot>
  static decltype(auto) GetElement(Slot& entry) noexcept {
    // Only published slots in [0, local_size) are exposed through IDs and iterators.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return *entry;
  }

  static const KeyType& GetKey(const Element& element) noexcept { return element.first; }

  template<typename... Args>
  static void Publish(Entry& entry, const KeyType& key, Args&&... args) {
    entry.emplace(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(std::forward<Args>(args)...));
  }
};

template<typename Container, typename Value>
using StringInternerMapElement = std::pair<
    std::conditional_t<
        std::same_as<StringInternerKey<Container>, std::string>,
        const std::string&,
        const std::string_view>,
    Value>;

template<typename Element>
using StringInternerEntriesFor = mbo::container::SegmentedVector<typename StringInternerElementTraits<Element>::Entry>;

}  // namespace mbo::strings::strings_internal

#endif  // MBO_STRINGS_INTERNAL_STRING_INTERNER_TRAITS_H_
