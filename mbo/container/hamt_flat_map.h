// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_HAMT_FLAT_MAP_H_
#define MBO_CONTAINER_HAMT_FLAT_MAP_H_

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
#include "mbo/container/internal/hamt_owned_tree.h"
#include "mbo/container/internal/hamt_tree.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {

// Persistent, unordered map with entries stored directly in packed HAMT nodes.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<
    typename Key,
    typename Mapped,
    typename Hash = std::hash<Key>,
    typename Equal = std::equal_to<>,
    HamtOptions Options = HamtOptions{},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
requires ValidHamtOptions<Options>
class HamtFlatMap final {
 private:
  using Entry = std::pair<const Key, Mapped>;

  using KeyOf = container_internal::HamtPairKey<Key, Mapped>;

  using Tree = container_internal::HamtTree<Options, Entry, Hash, KeyOf, Equal, Source>;
  using Owned = container_internal::HamtOwnedTree<Tree, Source>;

  template<typename Editor>
  struct MappedEditor final {
    const Editor& editor;

    void operator()(Entry& entry) const noexcept { std::invoke(editor, entry.second); }
  };

 public:
  using key_type = Key;
  using value_type = Entry;
  using mapped_type = Mapped;
  using size_type = Tree::size_type;
  using iterator = Tree::iterator;
  using const_iterator = iterator;
  using mutation_result = std::variant<std::pair<HamtFlatMap, bool>, HamtError>;

  class transient_type;

  explicit HamtFlatMap(Hash hash = Hash{}, Equal equal = Equal{}) noexcept
  requires std::is_nothrow_default_constructible_v<Source>
      : owned_(MakeOwned(std::move(hash), std::move(equal))) {}

  HamtFlatMap(const HamtFlatMap&) noexcept = default;
  HamtFlatMap& operator=(const HamtFlatMap&) noexcept = default;
  HamtFlatMap(HamtFlatMap&&) noexcept = default;
  HamtFlatMap& operator=(HamtFlatMap&&) noexcept = default;
  ~HamtFlatMap() = default;

  template<typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtFlatMap> try_create(
      Hash hash = Hash{},
      Equal equal = Equal{},
      SourceArgs&&... source_args) noexcept {
    auto owned = Owned::TryCreate(std::move(hash), KeyOf{}, std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!owned) {
      return std::nullopt;
    }
    return HamtFlatMap(std::move(*owned));
  }

  template<mbo::memory::BlockSource ControlSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtFlatMap> try_create_in(
      ControlSource& storage,
      Hash hash,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto owned = Owned::TryCreateIn(
        storage, std::move(hash), KeyOf{}, std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!owned) {
      return std::nullopt;
    }
    return HamtFlatMap(std::move(*owned));
  }

  size_type size() const noexcept { return owned_.tree().size(); }

  bool empty() const noexcept { return owned_.tree().empty(); }

  auto structural_diagnostics() const noexcept { return owned_.tree().structural_diagnostics(); }

  static constexpr size_type max_size() noexcept { return Tree::max_size(); }

  iterator begin() const noexcept { return owned_.tree().begin(); }

  static iterator end() noexcept { return Tree::end(); }

  iterator cbegin() const noexcept { return begin(); }

  static iterator cend() noexcept { return end(); }

  const Hash& hash_function() const noexcept { return owned_.tree().hash_function(); }

  const Equal& key_eq() const noexcept { return owned_.tree().key_eq(); }

  template<typename LookupKey>
  requires requires(const Tree& tree, const LookupKey& key) { tree.find(key); }
  iterator find(const LookupKey& key) const noexcept {
    return owned_.tree().find(key);
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
    return entry->second;
  }

  template<typename LookupKey, typename Editor>
  requires(
      std::is_nothrow_invocable_v<const Editor&, Mapped&>
      && std::same_as<std::invoke_result_t<const Editor&, Mapped&>, void>
      && requires(Tree& tree, const LookupKey& key) { tree.Find(key); })
  [[nodiscard]] mutation_result try_update(const LookupKey& key, const Editor& editor) const noexcept {
    HamtFlatMap next(*this);
    const auto result = next.owned_.tree().try_update(key, MappedEditor<Editor>{.editor = editor});
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtFlatMap, bool>(std::move(next), result.changed);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.contains(key); }
  size_type count(const LookupKey& key) const noexcept {
    return static_cast<size_type>(contains(key));
  }

  [[nodiscard]] mutation_result try_insert(const value_type& entry) const noexcept {
    HamtFlatMap next(*this);
    const auto result = next.owned_.tree().try_insert(entry);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtFlatMap, bool>(std::move(next), result.changed);
  }

  [[nodiscard]] std::pair<HamtFlatMap, bool> insert(const value_type& entry) const noexcept {
    return RequireValue(try_insert(entry));
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.try_erase(key); }
  [[nodiscard]] mutation_result try_erase(const LookupKey& key) const noexcept {
    HamtFlatMap next(*this);
    const auto result = next.owned_.tree().try_erase(key);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtFlatMap, bool>(std::move(next), result.changed);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.try_erase(key); }
  [[nodiscard]] std::pair<HamtFlatMap, bool> erase(const LookupKey& key) const noexcept {
    return RequireValue(try_erase(key));
  }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) const & noexcept {
    using Destination = HamtFlatMap<Key, Mapped, Hash, Equal, Options, OtherSource>;
    auto cloned = owned_.template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
    if (!cloned) {
      return std::optional<Destination>{};
    }
    return std::optional<Destination>(Destination(std::move(*cloned)));
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

  void swap(HamtFlatMap& other) noexcept { owned_.swap(other.owned_); }

  friend void swap(HamtFlatMap& first, HamtFlatMap& second) noexcept { first.swap(second); }

 private:
  template<
      typename OtherKey,
      typename OtherMapped,
      typename OtherHash,
      typename OtherEqual,
      HamtOptions OtherOptions,
      mbo::memory::BlockSource OtherSource>
  requires ValidHamtOptions<OtherOptions>
  friend class HamtFlatMap;

  static Owned MakeOwned(Hash hash, Equal equal) noexcept {
    auto owned = Owned::TryCreate(std::move(hash), KeyOf{}, std::move(equal));
    if (!owned) {
      std::terminate();
    }
    return std::move(*owned);
  }

  template<typename Value>
  static Value RequireValue(std::variant<Value, HamtError> result) noexcept {
    if (auto* const value = std::get_if<Value>(&result); value != nullptr) {
      return std::move(*value);
    }
    std::terminate();
  }

  explicit HamtFlatMap(Owned owned) noexcept : owned_(std::move(owned)) {}

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
class HamtFlatMap<Key, Mapped, Hash, Equal, Options, Source>::transient_type final {
 public:
  using iterator = Tree::mutable_iterator;
  using const_iterator = HamtFlatMap::iterator;
  using iterator_result = std::variant<iterator, HamtError>;
  using insertion_result = std::variant<std::pair<iterator, bool>, HamtError>;
  using erasure_result = std::variant<size_type, HamtError>;
  using update_result = std::variant<bool, HamtError>;
  using access_result = std::variant<Mapped*, HamtError>;

  explicit transient_type(HamtFlatMap snapshot) noexcept : map_(std::move(snapshot)) {}

  transient_type(const transient_type&) = delete;
  transient_type& operator=(const transient_type&) = delete;
  transient_type(transient_type&&) noexcept = default;
  transient_type& operator=(transient_type&&) noexcept = default;
  ~transient_type() = default;

  size_type size() const noexcept { return map_.size(); }

  bool empty() const noexcept { return map_.empty(); }

  auto structural_diagnostics() const noexcept { return map_.structural_diagnostics(); }

  static constexpr size_type max_size() noexcept { return HamtFlatMap::max_size(); }

  [[nodiscard]] iterator_result try_begin() noexcept { return map_.owned_.tree().TryMutableBegin(); }

  iterator begin() noexcept { return HamtFlatMap::RequireValue(try_begin()); }

  const_iterator begin() const noexcept { return map_.begin(); }

  iterator end() noexcept { return {}; }

  const_iterator end() const noexcept { return {}; }

  const_iterator cbegin() const noexcept { return map_.begin(); }

  const_iterator cend() const noexcept { return {}; }

  const Hash& hash_function() const noexcept { return map_.hash_function(); }

  const Equal& key_eq() const noexcept { return map_.key_eq(); }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.find(key); }
  const_iterator find(const LookupKey& key) const noexcept {
    return map_.find(key);
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.TryMutableFind(key); }
  [[nodiscard]] iterator_result try_find(const LookupKey& key) noexcept {
    return map_.owned_.tree().TryMutableFind(key);
  }

  template<typename LookupKey>
  requires requires(transient_type& map, const LookupKey& key) { map.try_find(key); }
  iterator find(const LookupKey& key) noexcept {
    return HamtFlatMap::RequireValue(try_find(key));
  }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.contains(key); }
  bool contains(const LookupKey& key) const noexcept {
    return map_.contains(key);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.at(key); }
  const Mapped& at(const LookupKey& key) const noexcept {
    return map_.at(key);
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.TryGetMutable(key); }
  [[nodiscard]] access_result try_at(const LookupKey& key) noexcept {
    auto result = map_.owned_.tree().TryGetMutable(key);
    if (auto* const entry = std::get_if<value_type*>(&result); entry != nullptr) {
      return *entry == nullptr ? nullptr : std::addressof((*entry)->second);
    }
    return std::get<HamtError>(result);
  }

  template<typename LookupKey>
  requires requires(transient_type& map, const LookupKey& key) { map.try_at(key); }
  Mapped& at(const LookupKey& key) noexcept {
    return ValueOrTerminate(try_at(key));
  }

  [[nodiscard]] access_result try_get_or_insert(const Key& key) noexcept
  requires std::is_nothrow_default_constructible_v<Mapped> {
    auto found = try_at(key);
    if (const auto* const error = std::get_if<HamtError>(&found); error != nullptr) {
      return *error;
    }
    if (std::get<Mapped*>(found) != nullptr) {
      return std::get<Mapped*>(found);
    }
    const auto inserted = map_.owned_.tree().try_insert(value_type(key, Mapped{}));
    if (inserted.error) {
      return *inserted.error;
    }
    return try_at(key);
  }

  Mapped& operator[](const Key& key) noexcept
  requires std::is_nothrow_default_constructible_v<Mapped> {
    return ValueOrTerminate(try_get_or_insert(key));
  }

  template<typename LookupKey, typename Editor>
  requires(
      std::is_nothrow_invocable_v<const Editor&, Mapped&>
      && std::same_as<std::invoke_result_t<const Editor&, Mapped&>, void>
      && requires(Tree& tree, const LookupKey& key) { tree.Find(key); })
  [[nodiscard]] update_result try_update(const LookupKey& key, const Editor& editor) noexcept {
    const auto result = map_.owned_.tree().try_update(key, MappedEditor<Editor>{.editor = editor});
    if (result.error) {
      return *result.error;
    }
    return result.changed;
  }

  template<typename LookupKey>
  requires requires(const HamtFlatMap& map, const LookupKey& key) { map.count(key); }
  size_type count(const LookupKey& key) const noexcept {
    return map_.count(key);
  }

  [[nodiscard]] insertion_result try_insert(const value_type& entry) noexcept {
    auto& tree = map_.owned_.tree();
    if (tree.contains(entry.first)) {
      auto existing = try_find(entry.first);
      if (const auto* const error = std::get_if<HamtError>(&existing); error != nullptr) {
        return *error;
      }
      return std::pair<iterator, bool>(HamtFlatMap::RequireValue(std::move(existing)), false);
    }
    if (tree.size() == max_size()) {
      return HamtError::kMaxSizeExceeded;
    }
    // The argument may borrow an entry invalidated by ownership preparation.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): the copy must outlive detachment.
    const value_type insertion = entry;
    const auto preparation = tree.TryMakeUnique();
    if (preparation) {
      return *preparation;
    }
    const auto result = map_.owned_.tree().try_insert(insertion);
    if (result.error) {
      return *result.error;
    }
    return std::pair<iterator, bool>(HamtFlatMap::RequireValue(try_find(insertion.first)), result.changed);
  }

  std::pair<iterator, bool> insert(const value_type& entry) noexcept {
    return HamtFlatMap::RequireValue(try_insert(entry));
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
    return HamtFlatMap::RequireValue(try_erase(key));
  }

  void clear() noexcept { map_.owned_.tree().clear(); }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) && noexcept {
    return std::move(map_).template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
  }

  [[nodiscard]] HamtFlatMap persistent() && noexcept { return std::move(map_); }

  void swap(transient_type& other) noexcept { map_.swap(other.map_); }

  friend void swap(transient_type& first, transient_type& second) noexcept { first.swap(second); }

 private:
  static Mapped& ValueOrTerminate(access_result result) noexcept {
    Mapped* const mapped = std::get<Mapped*>(result);
    if (mapped == nullptr) {
      std::terminate();
    }
    return *mapped;
  }

  HamtFlatMap map_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_HAMT_FLAT_MAP_H_
