// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INTERNER_POLICY_H_
#define MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INTERNER_POLICY_H_

#include <expected>
#include <string_view>
#include <utility>

#include "mbo/strings/string_interner_error.h"
#include "mbo/strings/string_interner_policies.h"

namespace mbo::container::experimental::hamt {

// Preserve recoverable HAMT exhaustion through the generic interner's policy
// extension point. Its normal emplace() interface retains the hard-failure form.
struct HamtStringInternerIndexPolicy : mbo::strings::StringInternerIndexStlPolicy {
  template<typename Container>
  static auto BeginInsertion(Container& container) noexcept(noexcept(container.BeginInsertion())) {
    return container.BeginInsertion();
  }

  template<typename Container, typename Id>
  static auto Insert(Container& container, std::string_view key, Id identifier) noexcept(
      noexcept(container.try_emplace(key, identifier)))
      -> std::expected<std::pair<typename Container::iterator, bool>, mbo::strings::StringInternerError> {
    auto inserted = container.try_emplace(key, identifier);
    if (!inserted) {
      return std::unexpected(mbo::strings::StringInternerError::kIndexExhausted);
    }
    return *inserted;
  }
};

}  // namespace mbo::container::experimental::hamt

#endif  // MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INTERNER_POLICY_H_
