// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_HAMT_STRING_INDEX_H_
#define MBO_STRINGS_HAMT_STRING_INDEX_H_

#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "mbo/container/hamt_flat_map.h"
#include "mbo/strings/string_id.h"

namespace mbo::strings {

// Owns index nodes, not character bytes. Keys must outlive the index.
// NOLINTBEGIN(readability-identifier-naming): index adapter follows STL container vocabulary.
template<
    typename Id = StringId<>,
    typename Hash = std::hash<std::string_view>,
    typename Equal = std::equal_to<>,
    mbo::container::HamtOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
class HamtStringIndex final {
 private:
  using Map = mbo::container::HamtFlatMap<std::string_view, Id, Hash, Equal, Options, Source>;

 public:
  std::optional<Id> find(std::string_view key) const noexcept {
    const auto found = map_.find(key);
    return found == map_.end() ? std::optional<Id>{} : std::optional<Id>(found->second);
  }

  // true: inserted; false: duplicate; nullopt: allocation or size exhaustion.
  // Failed insertion preserves all previous keys and IDs.
  std::optional<bool> try_insert(std::string_view key, Id identifier) noexcept {
    auto result = map_.try_insert({key, identifier});
    auto* const inserted = std::get_if<std::pair<Map, bool>>(&result);
    if (inserted == nullptr) {
      return std::nullopt;
    }
    const bool changed = inserted->second;
    map_ = std::move(inserted->first);
    return changed;
  }

 private:
  Map map_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_HAMT_STRING_INDEX_H_
