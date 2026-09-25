// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_OWNED_TREE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_OWNED_TREE_H_

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "mbo/container/internal/hamt_source_domain.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// Shared map/set lifetime layer; Tree supplies the key/value representation.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<typename Tree, mbo::memory::BlockSource Source>
class HamtOwnedTree final {
 public:
  static_assert(std::is_nothrow_copy_constructible_v<Tree> && std::is_nothrow_move_constructible_v<Tree>);
  static_assert(std::is_nothrow_swappable_v<Tree> && std::is_nothrow_destructible_v<Tree>);
  using tree_type = Tree;
  using domain_type = HamtSourceDomain<Source>;

  HamtOwnedTree(const HamtOwnedTree&) noexcept = default;

  HamtOwnedTree& operator=(const HamtOwnedTree& other) noexcept {
    HamtOwnedTree copy(other);
    swap(copy);
    return *this;
  }

  // Retain the source in the empty moved-from container so it remains reusable.
  // NOLINTNEXTLINE(cert-oop11-cpp,performance-move-constructor-init): Copying the domain is the contract.
  HamtOwnedTree(HamtOwnedTree&& other) noexcept : domain_(other.domain_), tree_(std::move(other.tree_)) {}

  HamtOwnedTree& operator=(HamtOwnedTree&& other) noexcept {
    if (this != std::addressof(other)) {
      HamtOwnedTree moved(std::move(other));
      swap(moved);
    }
    return *this;
  }

  ~HamtOwnedTree() = default;

  template<typename Hash, typename KeyOf, typename Equal, typename... SourceArgs>
  requires(
      std::is_nothrow_constructible_v<Source, SourceArgs...>
      && std::is_nothrow_constructible_v<Tree, Source&, Hash, KeyOf, Equal>
      && std::is_nothrow_move_constructible_v<Hash> && std::is_nothrow_move_constructible_v<KeyOf>
      && std::is_nothrow_move_constructible_v<Equal>)
  [[nodiscard]] static std::optional<HamtOwnedTree> TryCreate(
      Hash hash,
      KeyOf key_of,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto domain = domain_type::TryCreate(std::forward<SourceArgs>(source_args)...);
    if (!domain) {
      return std::nullopt;
    }
    return HamtOwnedTree(std::move(*domain), std::move(hash), std::move(key_of), std::move(equal));
  }

  Tree& tree() noexcept { return tree_; }

  const Tree& tree() const noexcept { return tree_; }

  // Node payload allocation must retain the same stable source as its tree.
  const domain_type& domain() const noexcept { return domain_; }

  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) const & noexcept {
    using ClonedTree = decltype(tree_.try_clone_to(std::declval<OtherSource&>()))::value_type;
    using ClonedOwned = HamtOwnedTree<ClonedTree, OtherSource>;
    auto domain = HamtSourceDomain<OtherSource>::TryCreate(std::forward<SourceArgs>(source_args)...);
    if (!domain) {
      return std::optional<ClonedOwned>{};
    }
    auto cloned = tree_.try_clone_to(*domain->get());
    if (!cloned) {
      return std::optional<ClonedOwned>{};
    }
    return std::optional<ClonedOwned>(ClonedOwned(std::move(*domain), std::move(*cloned)));
  }

  // Failure does not consume the original; success leaves a reusable empty core.
  template<mbo::memory::BlockSource OtherSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<OtherSource, SourceArgs...>
  [[nodiscard]] auto try_clone_to(SourceArgs&&... source_args) && noexcept {
    auto cloned = std::as_const(*this).template try_clone_to<OtherSource>(std::forward<SourceArgs>(source_args)...);
    if (cloned) {
      tree_.clear();
    }
    return cloned;
  }

  void swap(HamtOwnedTree& other) noexcept {
    domain_.swap(other.domain_);
    tree_.swap(other.tree_);
  }

  friend void swap(HamtOwnedTree& first, HamtOwnedTree& second) noexcept { first.swap(second); }

 private:
  template<typename, mbo::memory::BlockSource>
  friend class HamtOwnedTree;

  HamtOwnedTree(domain_type domain, Tree tree) noexcept : domain_(std::move(domain)), tree_(std::move(tree)) {}

  template<typename Hash, typename KeyOf, typename Equal>
  HamtOwnedTree(domain_type domain, Hash hash, KeyOf key_of, Equal equal) noexcept
      : domain_(std::move(domain)), tree_(*domain_.get(), std::move(hash), std::move(key_of), std::move(equal)) {}

  // Destruction is reverse declaration order: release nodes before the source.
  domain_type domain_;
  Tree tree_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_OWNED_TREE_H_
