// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_HAMT_NODE_MAP_H_
#define MBO_CONTAINER_HAMT_NODE_MAP_H_

#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "mbo/container/hamt_options.h"
#include "mbo/container/internal/hamt_key_of.h"
#include "mbo/container/internal/hamt_node_iterator.h"
#include "mbo/container/internal/hamt_node_key_of.h"
#include "mbo/container/internal/hamt_node_value.h"
#include "mbo/container/internal/hamt_owned_tree.h"
#include "mbo/container/internal/hamt_tree.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {

// Persistent, unordered map with separately owned entries and shared packed topology.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<
    typename Key,
    typename Mapped,
    typename Hash = std::hash<Key>,
    typename Equal = std::equal_to<>,
    HamtOptions Options = HamtOptions{},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
requires ValidHamtOptions<Options>
class HamtNodeMap final {
 private:
  using Entry = std::pair<const Key, Mapped>;

  using Payload = container_internal::HamtNodeValue<Entry, Source>;
  using KeyOf = container_internal::HamtNodeKeyOf<container_internal::HamtPairKey<Key, Mapped>>;

  using Tree = container_internal::HamtTree<Options, Payload, Hash, KeyOf, Equal, Source>;
  using Owned = container_internal::HamtOwnedTree<Tree, Source>;

 public:
  using key_type = Key;
  using value_type = Entry;
  using mapped_type = Mapped;
  using size_type = typename Tree::size_type;
  using iterator = container_internal::HamtNodeIterator<typename Tree::iterator>;
  using const_iterator = iterator;
  using mutation_result = std::variant<std::pair<HamtNodeMap, bool>, HamtError>;

  class transient_type;

  explicit HamtNodeMap(Hash hash = Hash{}, Equal equal = Equal{}) noexcept
  requires std::is_nothrow_default_constructible_v<Source>
      : owned_(MakeOwned(std::move(hash), std::move(equal))) {}

  HamtNodeMap(const HamtNodeMap&) noexcept = default;
  HamtNodeMap& operator=(const HamtNodeMap&) noexcept = default;
  HamtNodeMap(HamtNodeMap&&) noexcept = default;
  HamtNodeMap& operator=(HamtNodeMap&&) noexcept = default;
  ~HamtNodeMap() = default;

  template<typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtNodeMap> TryCreate(
      Hash hash = Hash{},
      Equal equal = Equal{},
      SourceArgs&&... source_args) noexcept {
    auto owned = Owned::TryCreate(std::move(hash), KeyOf{}, std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!owned) {
      return std::nullopt;
    }
    return HamtNodeMap(std::move(*owned));
  }

  size_type size() const noexcept { return owned_.tree().size(); }

  bool empty() const noexcept { return owned_.tree().empty(); }

  static constexpr size_type max_size() noexcept { return Tree::max_size(); }

  iterator begin() const noexcept { return iterator(owned_.tree().begin()); }

  static iterator end() noexcept { return {}; }

  iterator cbegin() const noexcept { return begin(); }

  static iterator cend() noexcept { return end(); }

  const Hash& hash_function() const noexcept { return owned_.tree().hash_function(); }

  const Equal& key_eq() const noexcept { return owned_.tree().key_eq(); }

  template<typename LookupKey>
  requires requires(const Tree& tree, const LookupKey& key) { tree.find(key); }
  iterator find(const LookupKey& key) const noexcept {
    return iterator(owned_.tree().find(key));
  }

  template<typename LookupKey>
  requires requires(const Tree& tree, const LookupKey& key) { tree.contains(key); }
  bool contains(const LookupKey& key) const noexcept {
    return owned_.tree().contains(key);
  }

  template<typename LookupKey>
  requires requires(const Tree& tree, const LookupKey& key) { tree.Find(key); }
  const Mapped& at(const LookupKey& key) const noexcept {
    const auto* const entry = owned_.tree().Find(key);
    if (entry == nullptr) {
      std::terminate();
    }
    return entry->get()->second;
  }

  template<typename LookupKey, typename Editor>
  requires(
      std::is_nothrow_invocable_v<const Editor&, Mapped&>
      && std::same_as<std::invoke_result_t<const Editor&, Mapped&>, void>
      && requires(Tree& tree, const LookupKey& key) { tree.Find(key); })
  [[nodiscard]] mutation_result try_update(const LookupKey& key, const Editor& editor) const noexcept {
    HamtNodeMap next(*this);
    const auto result = next.TryUpdate(key, editor);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtNodeMap, bool>(std::move(next), result.changed);
  }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.contains(key); }
  size_type count(const LookupKey& key) const noexcept {
    return static_cast<size_type>(contains(key));
  }

  [[nodiscard]] mutation_result try_insert(const value_type& entry) const noexcept
  requires std::is_nothrow_copy_constructible_v<value_type>
  {
    HamtNodeMap next(*this);
    const auto result = next.TryInsert(entry);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtNodeMap, bool>(std::move(next), result.changed);
  }

  [[nodiscard]] std::pair<HamtNodeMap, bool> insert(const value_type& entry) const noexcept
  requires std::is_nothrow_copy_constructible_v<value_type>
  {
    return RequireValue(try_insert(entry));
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.try_erase(key); }
  [[nodiscard]] mutation_result try_erase(const LookupKey& key) const noexcept {
    HamtNodeMap next(*this);
    const auto result = next.owned_.tree().try_erase(key);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtNodeMap, bool>(std::move(next), result.changed);
  }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.try_erase(key); }
  [[nodiscard]] std::pair<HamtNodeMap, bool> erase(const LookupKey& key) const noexcept {
    return RequireValue(try_erase(key));
  }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) const & noexcept {
    using Destination = HamtNodeMap<Key, Mapped, Hash, Equal, Options, OtherSource>;
    auto cloned = Destination::TryCreate(hash_function(), key_eq(), std::forward<SourceArgs>(source_args)...);
    if (!cloned) {
      return std::optional<Destination>{};
    }
    for (const auto& entry : *this) {
      if (cloned->TryInsert(entry).error) {
        return std::optional<Destination>{};
      }
    }
    return cloned;
  }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) && noexcept {
    auto cloned = std::as_const(*this).template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
    if (cloned) {
      owned_.tree().clear();
    }
    return cloned;
  }

  transient_type transient() const & noexcept { return transient_type(*this); }

  transient_type transient() && noexcept { return transient_type(std::move(*this)); }

  [[nodiscard]] mutation_result try_insert(value_type&& entry) const noexcept
  requires std::is_nothrow_move_constructible_v<value_type>
  {
    HamtNodeMap next(*this);
    const auto result = next.TryInsert(std::move(entry));
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtNodeMap, bool>(std::move(next), result.changed);
  }

  [[nodiscard]] std::pair<HamtNodeMap, bool> insert(value_type&& entry) const noexcept
  requires std::is_nothrow_move_constructible_v<value_type>
  {
    return RequireValue(try_insert(std::move(entry)));
  }

  void swap(HamtNodeMap& other) noexcept { owned_.swap(other.owned_); }

  friend void swap(HamtNodeMap& first, HamtNodeMap& second) noexcept { first.swap(second); }

 private:
  std::optional<HamtError> TryPrepareMutable() noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    auto& tree = owned_.tree();
    if (auto error = tree.TryMakeUnique()) {
      return error;
    }
    auto begin = std::get<typename Tree::mutable_iterator>(tree.TryMutableBegin());
    for (auto position = begin; position != typename Tree::mutable_iterator{}; ++position) {
      if (!position->try_get_mutable()) {
        return HamtError::kAllocationExhausted;
      }
    }
    return std::nullopt;
  }

  template<typename EntryArg>
  requires std::is_nothrow_constructible_v<Entry, EntryArg&&>
  container_internal::HamtMutationResult TryInsert(EntryArg&& entry) noexcept {
    auto& tree = owned_.tree();
    if (tree.contains(entry.first)) {
      return {};
    }
    if (tree.size() == max_size()) {
      return {.changed = false, .error = HamtError::kMaxSizeExceeded};
    }
    auto payload = Payload::TryCreate(owned_.domain(), std::forward<EntryArg>(entry));
    if (!payload) {
      return {.changed = false, .error = HamtError::kAllocationExhausted};
    }
    return tree.try_insert(*payload);
  }

  template<typename LookupKey>
  requires std::is_nothrow_copy_constructible_v<Entry>
  std::variant<Entry*, HamtError> TryMutableEntry(const LookupKey& key) noexcept {
    auto result = owned_.tree().TryGetMutable(key);
    auto* const payload = std::get_if<Payload*>(&result);
    if (payload == nullptr) {
      return std::get<HamtError>(result);
    }
    if (*payload == nullptr) {
      return static_cast<Entry*>(nullptr);
    }
    const auto editable = (*payload)->try_get_mutable();
    if (!editable) {
      return HamtError::kAllocationExhausted;
    }
    return editable.value_or(nullptr);
  }

  template<typename LookupKey, typename Editor>
  container_internal::HamtMutationResult TryUpdate(const LookupKey& key, const Editor& editor) noexcept {
    auto result = TryMutableEntry(key);
    auto* const entry = std::get_if<Entry*>(&result);
    if (entry == nullptr) {
      return {.changed = false, .error = std::get<HamtError>(result)};
    }
    if (*entry == nullptr) {
      return {};
    }
    std::invoke(editor, (*entry)->second);
    return {.changed = true};
  }

  template<
      typename OtherKey,
      typename OtherMapped,
      typename OtherHash,
      typename OtherEqual,
      HamtOptions OtherOptions,
      mbo::memory::BlockSource OtherSource>
  requires ValidHamtOptions<OtherOptions>
  friend class HamtNodeMap;

  static Owned MakeOwned(Hash hash, Equal equal) noexcept {
    return std::move(Owned::TryCreate(std::move(hash), KeyOf{}, std::move(equal))).value();
  }

  template<typename Value>
  static Value RequireValue(std::variant<Value, HamtError> result) noexcept {
    return std::get<Value>(std::move(result));
  }

  explicit HamtNodeMap(Owned owned) noexcept : owned_(std::move(owned)) {}

  Owned owned_;
};

template<
    typename Key,
    typename Mapped,
    typename Hash,
    typename Equal,
    HamtOptions Options,
    mbo::memory::BlockSource Source>
requires ValidHamtOptions<Options>
class HamtNodeMap<Key, Mapped, Hash, Equal, Options, Source>::transient_type final {
 public:
  using iterator = container_internal::HamtNodeIterator<typename Tree::mutable_iterator, true>;
  using const_iterator = typename HamtNodeMap::iterator;
  using iterator_result = std::variant<iterator, HamtError>;
  using insertion_result = std::variant<std::pair<iterator, bool>, HamtError>;
  using erasure_result = std::variant<size_type, HamtError>;
  using update_result = std::variant<bool, HamtError>;
  using access_result = std::variant<Mapped*, HamtError>;

  explicit transient_type(HamtNodeMap snapshot) noexcept : map_(std::move(snapshot)) {}

  transient_type(const transient_type&) = delete;
  transient_type& operator=(const transient_type&) = delete;
  transient_type(transient_type&&) noexcept = default;
  transient_type& operator=(transient_type&&) noexcept = default;
  ~transient_type() = default;

  size_type size() const noexcept { return map_.size(); }

  bool empty() const noexcept { return map_.empty(); }

  static constexpr size_type max_size() noexcept { return HamtNodeMap::max_size(); }

  [[nodiscard]] iterator_result try_begin() noexcept {
    if (auto error = map_.TryPrepareMutable()) {
      return *error;
    }
    return iterator(std::get<typename Tree::mutable_iterator>(map_.owned_.tree().TryMutableBegin()));
  }

  iterator begin() noexcept { return HamtNodeMap::RequireValue(try_begin()); }

  const_iterator begin() const noexcept { return map_.begin(); }

  iterator end() noexcept { return {}; }

  const_iterator end() const noexcept { return {}; }

  const_iterator cbegin() const noexcept { return map_.begin(); }

  const_iterator cend() const noexcept { return {}; }

  const Hash& hash_function() const noexcept { return map_.hash_function(); }

  const Equal& key_eq() const noexcept { return map_.key_eq(); }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.find(key); }
  const_iterator find(const LookupKey& key) const noexcept {
    return map_.find(key);
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.TryMutableFind(key); }
  [[nodiscard]] iterator_result try_find(const LookupKey& key) noexcept {
    const auto* const payload = map_.owned_.tree().Find(key);
    if (payload == nullptr) {
      return iterator{};
    }
    const Key lookup = payload->get()->first;
    if (auto error = map_.TryPrepareMutable()) {
      return *error;
    }
    return iterator(std::get<typename Tree::mutable_iterator>(map_.owned_.tree().TryMutableFind(lookup)));
  }

  template<typename LookupKey>
  requires requires(transient_type& map, const LookupKey& key) { map.try_find(key); }
  iterator find(const LookupKey& key) noexcept {
    return HamtNodeMap::RequireValue(try_find(key));
  }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.contains(key); }
  bool contains(const LookupKey& key) const noexcept {
    return map_.contains(key);
  }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.at(key); }
  const Mapped& at(const LookupKey& key) const noexcept {
    return map_.at(key);
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.TryGetMutable(key); }
  [[nodiscard]] access_result try_at(const LookupKey& key) noexcept {
    auto result = map_.TryMutableEntry(key);
    if (auto* const entry = std::get_if<value_type*>(&result); entry != nullptr) {
      return *entry == nullptr ? nullptr : std::addressof((*entry)->second);
    }
    return std::get<HamtError>(result);
  }

  template<typename LookupKey>
  requires requires(transient_type& map, const LookupKey& key) { map.try_at(key); }
  Mapped& at(const LookupKey& key) noexcept {
    return RequireMapped(try_at(key));
  }

  [[nodiscard]] access_result try_get_or_insert(const Key& key) noexcept
  requires std::is_nothrow_default_constructible_v<Mapped>
  {
    auto found = try_at(key);
    const auto* const mapped = std::get_if<Mapped*>(&found);
    if (mapped == nullptr || *mapped != nullptr) {
      return found;
    }
    const auto inserted = map_.TryInsert(value_type(key, Mapped{}));
    if (inserted.error) {
      return *inserted.error;
    }
    return try_at(key);
  }

  Mapped& operator[](const Key& key) noexcept
  requires std::is_nothrow_default_constructible_v<Mapped>
  {
    return RequireMapped(try_get_or_insert(key));
  }

  template<typename LookupKey, typename Editor>
  requires(
      std::is_nothrow_invocable_v<const Editor&, Mapped&>
      && std::same_as<std::invoke_result_t<const Editor&, Mapped&>, void>
      && requires(Tree& tree, const LookupKey& key) { tree.Find(key); })
  [[nodiscard]] update_result try_update(const LookupKey& key, const Editor& editor) noexcept {
    const auto result = map_.TryUpdate(key, editor);
    if (result.error) {
      return *result.error;
    }
    return result.changed;
  }

  template<typename LookupKey>
  requires requires(const HamtNodeMap& map, const LookupKey& key) { map.count(key); }
  size_type count(const LookupKey& key) const noexcept {
    return map_.count(key);
  }

  [[nodiscard]] insertion_result try_insert(const value_type& entry) noexcept
  requires std::is_nothrow_copy_constructible_v<value_type>
  {
    auto& tree = map_.owned_.tree();
    if (tree.size() == max_size() && !tree.contains(entry.first)) {
      return HamtError::kMaxSizeExceeded;
    }
    // The argument may borrow an entry invalidated by ownership preparation.
    const value_type insertion = entry;
    const auto preparation = map_.TryPrepareMutable();
    if (preparation) {
      return *preparation;
    }
    const auto result = map_.TryInsert(insertion);
    if (result.error) {
      return *result.error;
    }
    return std::pair<iterator, bool>(HamtNodeMap::RequireValue(try_find(insertion.first)), result.changed);
  }

  std::pair<iterator, bool> insert(const value_type& entry) noexcept
  requires std::is_nothrow_copy_constructible_v<value_type>
  {
    return HamtNodeMap::RequireValue(try_insert(entry));
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.try_erase(key); }
  [[nodiscard]] erasure_result try_erase(const LookupKey& key) noexcept {
    const auto result = map_.owned_.tree().try_erase(key);
    if (result.error) {
      return *result.error;
    }
    return static_cast<size_type>(result.changed);
  }

  template<typename LookupKey>
  requires requires(transient_type& transient, const LookupKey& key) { transient.try_erase(key); }
  size_type erase(const LookupKey& key) noexcept {
    return HamtNodeMap::RequireValue(try_erase(key));
  }

  void clear() noexcept { map_.owned_.tree().clear(); }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) && noexcept {
    return std::move(map_).template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
  }

  [[nodiscard]] HamtNodeMap persistent() && noexcept { return std::move(map_); }

  void swap(transient_type& other) noexcept { map_.swap(other.map_); }

  friend void swap(transient_type& first, transient_type& second) noexcept { first.swap(second); }

 private:
  static Mapped& RequireMapped(access_result result) noexcept {
    Mapped* const mapped = std::get<Mapped*>(result);
    if (mapped == nullptr) {
      std::terminate();
    }
    return *mapped;
  }

  HamtNodeMap map_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_HAMT_NODE_MAP_H_
