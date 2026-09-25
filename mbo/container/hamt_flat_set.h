// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_HAMT_FLAT_SET_H_
#define MBO_CONTAINER_HAMT_FLAT_SET_H_

#include <exception>
#include <functional>
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

// Persistent, unordered set with entries stored directly in packed HAMT nodes.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<
    typename Key,
    typename Hash = std::hash<Key>,
    typename Equal = std::equal_to<>,
    HamtOptions Options = HamtOptions{},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
requires ValidHamtOptions<Options>
class HamtFlatSet final {
 private:
  using KeyOf = container_internal::HamtIdentityKey<Key>;

  using Tree = container_internal::HamtTree<Options, Key, Hash, KeyOf, Equal, Source>;
  using Owned = container_internal::HamtOwnedTree<Tree, Source>;

 public:
  using key_type = Key;
  using value_type = Key;
  using size_type = Tree::size_type;
  using iterator = Tree::iterator;
  using const_iterator = iterator;
  using mutation_result = std::variant<std::pair<HamtFlatSet, bool>, HamtError>;

  class transient_type;

  explicit HamtFlatSet(Hash hash = Hash{}, Equal equal = Equal{}) noexcept
  requires std::is_nothrow_default_constructible_v<Source>
      : owned_(MakeOwned(std::move(hash), std::move(equal))) {}

  HamtFlatSet(const HamtFlatSet&) noexcept = default;
  HamtFlatSet& operator=(const HamtFlatSet&) noexcept = default;
  HamtFlatSet(HamtFlatSet&&) noexcept = default;
  HamtFlatSet& operator=(HamtFlatSet&&) noexcept = default;
  ~HamtFlatSet() = default;

  template<typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtFlatSet> try_create(
      Hash hash = Hash{},
      Equal equal = Equal{},
      SourceArgs&&... source_args) noexcept {
    auto owned = Owned::TryCreate(std::move(hash), KeyOf{}, std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!owned) {
      return std::nullopt;
    }
    return HamtFlatSet(std::move(*owned));
  }

  size_type size() const noexcept { return owned_.tree().size(); }

  bool empty() const noexcept { return owned_.tree().empty(); }

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
  requires requires(const HamtFlatSet& set, const LookupKey& key) { set.contains(key); }
  size_type count(const LookupKey& key) const noexcept {
    return static_cast<size_type>(contains(key));
  }

  [[nodiscard]] mutation_result try_insert(const Key& key) const noexcept {
    HamtFlatSet next(*this);
    const auto result = next.owned_.tree().try_insert(key);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtFlatSet, bool>(std::move(next), result.changed);
  }

  [[nodiscard]] std::pair<HamtFlatSet, bool> insert(const Key& key) const noexcept {
    return RequireValue(try_insert(key));
  }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.try_erase(key); }
  [[nodiscard]] mutation_result try_erase(const LookupKey& key) const noexcept {
    HamtFlatSet next(*this);
    const auto result = next.owned_.tree().try_erase(key);
    if (result.error) {
      return *result.error;
    }
    return std::pair<HamtFlatSet, bool>(std::move(next), result.changed);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatSet& set, const LookupKey& key) { set.try_erase(key); }
  [[nodiscard]] std::pair<HamtFlatSet, bool> erase(const LookupKey& key) const noexcept {
    return RequireValue(try_erase(key));
  }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) const & noexcept {
    using Destination = HamtFlatSet<Key, Hash, Equal, Options, OtherSource>;
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

  void swap(HamtFlatSet& other) noexcept { owned_.swap(other.owned_); }

  friend void swap(HamtFlatSet& first, HamtFlatSet& second) noexcept { first.swap(second); }

 private:
  template<
      typename OtherKey,
      typename OtherHash,
      typename OtherEqual,
      HamtOptions OtherOptions,
      mbo::memory::BlockSource OtherSource>
  requires ValidHamtOptions<OtherOptions>
  friend class HamtFlatSet;

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

  explicit HamtFlatSet(Owned owned) noexcept : owned_(std::move(owned)) {}

  Owned owned_;
};

template<typename Key, typename Hash, typename Equal, HamtOptions Options, mbo::memory::BlockSource Source>
requires ValidHamtOptions<Options>
class HamtFlatSet<Key, Hash, Equal, Options, Source>::transient_type final {
 public:
  using iterator = HamtFlatSet::iterator;
  using insertion_result = std::variant<std::pair<iterator, bool>, HamtError>;
  using erasure_result = std::variant<size_type, HamtError>;

  explicit transient_type(HamtFlatSet snapshot) noexcept : set_(std::move(snapshot)) {}

  transient_type(const transient_type&) = delete;
  transient_type& operator=(const transient_type&) = delete;
  transient_type(transient_type&&) noexcept = default;
  transient_type& operator=(transient_type&&) noexcept = default;
  ~transient_type() = default;

  size_type size() const noexcept { return set_.size(); }

  bool empty() const noexcept { return set_.empty(); }

  static constexpr size_type max_size() noexcept { return HamtFlatSet::max_size(); }

  iterator begin() const noexcept { return set_.begin(); }

  static iterator end() noexcept { return HamtFlatSet::end(); }

  iterator cbegin() const noexcept { return begin(); }

  static iterator cend() noexcept { return end(); }

  const Hash& hash_function() const noexcept { return set_.hash_function(); }

  const Equal& key_eq() const noexcept { return set_.key_eq(); }

  template<typename LookupKey>
  requires requires(const HamtFlatSet& set, const LookupKey& key) { set.find(key); }
  iterator find(const LookupKey& key) const noexcept {
    return set_.find(key);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatSet& set, const LookupKey& key) { set.contains(key); }
  bool contains(const LookupKey& key) const noexcept {
    return set_.contains(key);
  }

  template<typename LookupKey>
  requires requires(const HamtFlatSet& set, const LookupKey& key) { set.count(key); }
  size_type count(const LookupKey& key) const noexcept {
    return set_.count(key);
  }

  [[nodiscard]] insertion_result try_insert(const Key& key) noexcept {
    const auto result = set_.owned_.tree().try_insert(key);
    if (result.error) {
      return *result.error;
    }
    return std::pair<iterator, bool>(set_.find(key), result.changed);
  }

  std::pair<iterator, bool> insert(const Key& key) noexcept { return HamtFlatSet::RequireValue(try_insert(key)); }

  template<typename LookupKey>
  requires requires(Tree& tree, const LookupKey& key) { tree.try_erase(key); }
  [[nodiscard]] erasure_result try_erase(const LookupKey& key) noexcept {
    const auto result = set_.owned_.tree().try_erase(key);
    if (result.error) {
      return *result.error;
    }
    return static_cast<size_type>(result.changed);
  }

  template<typename LookupKey>
  requires requires(transient_type& transient, const LookupKey& key) { transient.try_erase(key); }
  size_type erase(const LookupKey& key) noexcept {
    return HamtFlatSet::RequireValue(try_erase(key));
  }

  void clear() noexcept { set_.owned_.tree().clear(); }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) && noexcept {
    return std::move(set_).template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
  }

  [[nodiscard]] HamtFlatSet persistent() && noexcept { return std::move(set_); }

  void swap(transient_type& other) noexcept { set_.swap(other.set_); }

  friend void swap(transient_type& first, transient_type& second) noexcept { first.swap(second); }

 private:
  HamtFlatSet set_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_HAMT_FLAT_SET_H_
