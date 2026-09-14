// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_HAMT_STRING_INDEX_H_
#define MBO_STRINGS_HAMT_STRING_INDEX_H_

#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
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
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
    typename MapBackend = mbo::container::HamtFlatMap<std::string_view, Id, Hash, Equal, Options, Source>>
class HamtStringIndex final {
 private:
  using Map = MapBackend;

 public:
  HamtStringIndex() = default;

  explicit HamtStringIndex(Hash hash, Equal equal = Equal{}) noexcept
  requires std::is_nothrow_default_constructible_v<Source>
      : map_(std::move(hash), std::move(equal)) {}

  template<typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtStringIndex> try_create(
      Hash hash,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto map = Map::try_create(std::move(hash), std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (map) {
      return HamtStringIndex(std::move(map).value());
    }
    return std::nullopt;
  }

  std::optional<Id> find(std::string_view key) const noexcept {
    const auto found = map_.find(key);
    return found == map_.end() ? std::optional<Id>{} : std::optional<Id>(found->second);
  }

  template<mbo::memory::BlockSource ControlSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtStringIndex> try_create_in(
      ControlSource& storage,
      Hash hash,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto map = Map::try_create_in(storage, std::move(hash), std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!map) {
      return std::nullopt;
    }
    return HamtStringIndex(std::move(*map));
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
  explicit HamtStringIndex(Map&& map) noexcept : map_(std::move(map)) {}

  Map map_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_HAMT_STRING_INDEX_H_
