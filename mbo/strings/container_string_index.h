// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_CONTAINER_STRING_INDEX_H_
#define MBO_STRINGS_CONTAINER_STRING_INDEX_H_

#include <concepts>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "mbo/strings/string_id.h"

namespace mbo::strings {

// For STL/Abseil-style map containers. Keys borrow the interner's byte storage.
// Native container exceptions terminate: this is not a recoverable allocator adapter.
template<typename Id = StringId<>, typename Container = std::unordered_map<std::string_view, Id>>
requires(
    std::same_as<typename Container::key_type, std::string_view> && std::same_as<typename Container::mapped_type, Id>)
class ContainerStringIndex final {
 public:
  std::optional<Id> find(std::string_view key) const noexcept {
    const auto position = entries_.find(key);
    return position == entries_.end() ? std::optional<Id>{} : std::optional<Id>(position->second);
  }

  std::optional<bool> try_insert(std::string_view key, Id id) noexcept {
    if (entries_.find(key) != entries_.end()) {
      return false;
    }
    return entries_.emplace(key, id).second;
  }

 private:
  Container entries_;
};

}  // namespace mbo::strings

#endif  // MBO_STRINGS_CONTAINER_STRING_INDEX_H_
