// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_H_
#define MBO_STRINGS_STRING_INTERNER_H_

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/hash_container_defaults.h"
#include "mbo/config/require.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/internal/string_interner_base.h"  // IWYU pragma: export
#include "mbo/strings/string_id.h"
#include "mbo/strings/string_interner_types.h"  // IWYU pragma: export

namespace mbo::strings {

// NOLINTBEGIN(readability-identifier-naming): String interner wrappers follow STL container vocabulary.

// Interned strings without caller-supplied values. The index may be a set or a string-to-ID map.
template<
    StringInternerId Id = StringIdU32<>,
    typename Hash = absl::DefaultHashContainerHash<std::string_view>,
    typename StringContainer = absl::flat_hash_map<std::string_view, Id, Hash>,
    typename Storage =
        std::conditional_t<std::same_as<StringInternerKey<StringContainer>, std::string>, void, ArenaStringStorage<>>,
    typename Entries = strings_internal::StringInternerEntriesFor<const StringInternerKey<StringContainer>>,
    typename Policy = StringInternerPolicyHolder<>>
requires requires {
  typename strings_internal::StringInternerBase<
      Id,
      const StringInternerKey<StringContainer>,
      Hash,
      StringContainer,
      Storage,
      Entries,
      Policy>;
}
class StringInternerSet final
    : public strings_internal::
          StringInternerBase<
              Id,
              const StringInternerKey<StringContainer>,
              Hash,
              StringContainer,
              Storage,
              Entries,
              Policy> {
  using Base = strings_internal::
      StringInternerBase<
          Id,
          const StringInternerKey<StringContainer>,
          Hash,
          StringContainer,
          Storage,
          Entries,
          Policy>;

 public:
  using Base::Base;
};

// Interned string keys with caller-supplied values, also accessible through generated IDs.
// Keys are immutable; mapped values are mutable and shared with children in the captured prefix.
template<
    StringInternerId Id,
    typename Value,
    typename Hash = absl::DefaultHashContainerHash<std::string_view>,
    typename StringContainer = absl::flat_hash_map<std::string_view, Id, Hash>,
    typename Storage =
        std::conditional_t<std::same_as<StringInternerKey<StringContainer>, std::string>, void, ArenaStringStorage<>>,
    typename Entries =
        strings_internal::StringInternerEntriesFor<strings_internal::StringInternerMapElement<StringContainer, Value>>,
    typename Policy = StringInternerPolicyHolder<>>
requires requires {
  typename strings_internal::StringInternerBase<
      Id,
      strings_internal::StringInternerMapElement<StringContainer, Value>,
      Hash,
      StringContainer,
      Storage,
      Entries,
      Policy>;
}
class StringInternerMap final
    : public strings_internal::StringInternerBase<
          Id,
          strings_internal::StringInternerMapElement<StringContainer, Value>,
          Hash,
          StringContainer,
          Storage,
          Entries,
          Policy> {
  using Base = strings_internal::StringInternerBase<
      Id,
      strings_internal::StringInternerMapElement<StringContainer, Value>,
      Hash,
      StringContainer,
      Storage,
      Entries,
      Policy>;

 public:
  using Base::Base;
  using mapped_type = Value;
  using typename Base::id_type;
  using typename Base::insertion_result;
  using typename Base::value_type;

  mapped_type& at(id_type identifier) {
    auto entry = Base::get(identifier);
    MBO_CONFIG_REQUIRE(entry.has_value(), "String interner ID out of range");
    return Base::policy::Get(entry).second;
  }

  const mapped_type& at(id_type identifier) const {
    auto entry = Base::get(identifier);
    MBO_CONFIG_REQUIRE(entry.has_value(), "String interner ID out of range");
    return Base::policy::Get(entry).second;
  }

  mapped_type& operator[](id_type identifier) { return at(identifier); }

  const mapped_type& operator[](id_type identifier) const { return at(identifier); }

  template<typename Arg>
  requires std::constructible_from<mapped_type, Arg&&>
  [[nodiscard]] insertion_result insert(std::string_view key, Arg&& value) {
    return Base::Insert(key, std::forward<Arg>(value));
  }

  template<typename... Args>
  requires std::constructible_from<mapped_type, Args&&...>
  [[nodiscard]] insertion_result try_emplace(std::string_view key, Args&&... args) {
    return Base::Insert(key, std::forward<Args>(args)...);
  }
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_H_
