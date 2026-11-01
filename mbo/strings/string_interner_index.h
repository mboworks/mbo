// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_INDEX_H_
#define MBO_STRINGS_STRING_INTERNER_INDEX_H_

#include <string_view>

namespace mbo::strings {

// Index policies own hash/probe preparation. A prepared value may contain a hash, bucket, and
// container-specific probe state; it must remain valid until Find or Insert is called.
struct StringInternerIndexStlPolicy {
  template<typename Container>
  using prepared_type = std::string_view;

  template<typename Container>
  static prepared_type<Container> Prepare(const Container&, std::string_view key) noexcept {
    return key;
  }

  template<typename Container>
  static auto Find(const Container& container, prepared_type<Container> prepared) {
    return container.find(prepared);
  }

  template<typename Container, typename Id>
  static auto Insert(Container& container, prepared_type<Container> prepared, Id identifier) {
    if constexpr (requires { typename Container::mapped_type; }) {
      return container.emplace(typename Container::key_type(prepared), identifier);
    } else {
      return container.emplace(typename Container::key_type(prepared));
    }
  }
};

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_INDEX_H_
