// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_TYPES_H_
#define MBO_STRINGS_STRING_INTERNER_TYPES_H_

#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

#include "mbo/strings/string_id.h"
#include "mbo/strings/string_interner_error.h"
#include "mbo/strings/string_interner_traits.h"

namespace mbo::strings {

template<typename Id>
concept StringInternerId = IsStringIdRepresentation<Id> || mbo::types::IsConstStrongId<Id>;

// Key validation is independent of the mapped value.
template<typename Container>
concept StringInternerStringContainer = requires(const Container& read, std::string_view key) {
  typename Container::key_type;
  requires(
      std::same_as<StringInternerKey<Container>, std::string_view>
      || std::same_as<StringInternerKey<Container>, std::string>);
  requires std::is_const_v<std::remove_reference_t<decltype(StringInternerContainerTraits<Container>::GetKey(
      *std::declval<typename Container::iterator>()))>>;
  { read.find(key) } -> std::same_as<typename Container::const_iterator>;
  { read.end() } -> std::same_as<typename Container::const_iterator>;
};

template<typename Container, typename Id>
concept StringInternerIdIndex =
    StringInternerStringContainer<Container>
    && requires(Container& container, Container::iterator position) { container.erase(position); }
    && ((StringInternerContainerTraits<Container>::kIsMap && requires(Container& container, std::string_view key, Id identifier) {
          requires std::same_as<typename Container::mapped_type, Id>;
          {
            container.emplace(StringInternerKey<Container>(key), identifier)
          } -> std::same_as<std::pair<typename Container::iterator, bool>>;
        }) || (!StringInternerContainerTraits<Container>::kIsMap && requires(Container& container, std::string_view key) {
          {
            container.emplace(StringInternerKey<Container>(key))
          } -> std::same_as<std::pair<typename Container::iterator, bool>>;
        }));

template<typename Storage>
concept StringInternerStorage = requires(Storage& storage, std::string_view text) {
  { storage.try_store(text) } noexcept -> std::same_as<std::optional<std::string_view>>;
};

template<typename Entries, typename Entry>
concept StringInternerEntries =
    std::ranges::bidirectional_range<Entries> && std::ranges::bidirectional_range<const Entries>
    && std::same_as<typename Entries::value_type, Entry> && requires(Entries& entries, const Entries& read) {
         { read.size() } -> std::convertible_to<std::size_t>;
         entries.emplace_back();
         { entries.back() } -> std::same_as<Entry&>;
       };

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_TYPES_H_
