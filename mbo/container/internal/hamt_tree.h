// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_TREE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_TREE_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "mbo/container/hamt_options.h"
#include "mbo/container/internal/hamt_clone.h"
#include "mbo/container/internal/hamt_erase.h"
#include "mbo/container/internal/hamt_insert.h"
#include "mbo/container/internal/hamt_iterator.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/container/internal/hamt_replace.h"
#include "mbo/container/internal/hamt_root_owner.h"
#include "mbo/container/internal/hamt_update.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

struct HamtMutationResult final {
  bool changed = false;
  std::optional<HamtError> error;
};

// One reachable snapshot, not exclusive ownership or total allocator usage.
struct HamtStructuralDiagnostics final {
  std::size_t nodes = 0;
  std::size_t entries = 0;
  std::size_t collision_nodes = 0;
  std::size_t collision_entries = 0;
  std::size_t largest_collision = 0;
  std::size_t maximum_depth = 0;
  std::size_t node_allocation_bytes = 0;
};

// Shared map/set implementation over immutable stored entries. Mutations use
// path copying; copying this value retains its root, not its entries. The source
// is borrowed and must outlive all snapshots. Public wrappers supply its domain.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<
    HamtOptions Options,
    typename Entry,
    typename Hash,
    typename KeyOf,
    typename Equal,
    mbo::memory::BlockSource Source>
requires ValidHamtOptions<Options>
class HamtTree final {
 public:
  using value_type = Entry;
  using size_type = std::size_t;
  using key_type = std::remove_cvref_t<std::invoke_result_t<const KeyOf&, const Entry&>>;
  using hash_type = std::invoke_result_t<const Hash&, const key_type&>;
  using node_type = HamtSharedNode<Options.fragment_bits, Entry>;
  using iterator = HamtIterator<Options.fragment_bits, Entry>;
  using const_iterator = iterator;
  using mutable_iterator = HamtIterator<Options.fragment_bits, Entry, true>;

  static_assert(std::unsigned_integral<hash_type>);
  static_assert(std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_move_constructible_v<Entry>);
  static_assert(std::is_nothrow_invocable_v<const KeyOf&, const Entry&>);
  static_assert(std::is_nothrow_invocable_r_v<hash_type, const Hash&, const key_type&>);
  static_assert(std::is_nothrow_invocable_r_v<bool, const Equal&, const key_type&, const key_type&>);
  static_assert(std::is_nothrow_copy_constructible_v<Hash> && std::is_nothrow_move_constructible_v<Hash>);
  static_assert(std::is_nothrow_copy_constructible_v<KeyOf> && std::is_nothrow_move_constructible_v<KeyOf>);
  static_assert(std::is_nothrow_copy_constructible_v<Equal> && std::is_nothrow_move_constructible_v<Equal>);
  static_assert(
      std::is_nothrow_swappable_v<Hash> && std::is_nothrow_swappable_v<KeyOf> && std::is_nothrow_swappable_v<Equal>);
  static_assert(
      std::is_nothrow_destructible_v<Hash> && std::is_nothrow_destructible_v<KeyOf>
      && std::is_nothrow_destructible_v<Equal>);

  HamtTree(Source& source, Hash hash, KeyOf key_of, Equal equal) noexcept
      : hash_(std::move(hash)), key_of_(std::move(key_of)), equal_(std::move(equal)), root_(source) {}

  HamtTree(const HamtTree&) noexcept = default;

  HamtTree& operator=(const HamtTree& other) noexcept {
    HamtTree copied(other);
    swap(copied);
    return *this;
  }

  HamtTree(HamtTree&& other) noexcept
      : hash_(other.hash_),
        key_of_(other.key_of_),
        equal_(other.equal_),
        root_(std::move(other.root_)),
        size_(std::exchange(other.size_, 0)) {}

  HamtTree& operator=(HamtTree&& other) noexcept {
    HamtTree moved(std::move(other));
    swap(moved);
    return *this;
  }

  ~HamtTree() = default;

  std::size_t size() const noexcept { return size_; }

  bool empty() const noexcept { return size_ == 0; }

  // Cold, allocation-free traversal. Shared nodes are included once in this
  // snapshot; their bytes are not attributed exclusively to this owner. Node
  // payload allocations, source control blocks and retained free blocks are
  // deliberately excluded. External synchronization is required as usual.
  HamtStructuralDiagnostics structural_diagnostics() const noexcept {
    HamtStructuralDiagnostics result;
    const auto visit = [&result](auto&& self, const node_type* node, std::size_t depth) noexcept -> void {
      if (node == nullptr) {
        return;
      }
      ++result.nodes;
      result.entries += node->entries().size();
      result.node_allocation_bytes += node->allocation_bytes();
      if (depth > result.maximum_depth) {
        result.maximum_depth = depth;
      }
      if (node->is_collision()) {
        ++result.collision_nodes;
        result.collision_entries += node->entries().size();
        if (node->entries().size() > result.largest_collision) {
          result.largest_collision = node->entries().size();
        }
      }
      for (const node_type* child : node->children()) {
        self(self, child, depth + 1);
      }
    };
    visit(visit, root_.get(), 0);
    return result;
  }

  static constexpr std::size_t max_size() noexcept { return Options.maximum_size; }

  const Hash& hash_function() const noexcept { return hash_; }

  const Equal& key_eq() const noexcept { return equal_; }

  iterator begin() const noexcept { return iterator(root_.get(), this); }

  static iterator end() noexcept { return {}; }

  [[nodiscard]] std::optional<HamtError> TryMakeUnique() noexcept {
    if (AllUnique(root_.get())) {
      return std::nullopt;
    }
    const auto cloned = TryCloneHamtTree(root_.source(), root_.get());
    if (!cloned) {
      return HamtError::kAllocationExhausted;
    }
    root_.reset(*cloned);
    return std::nullopt;
  }

  [[nodiscard]] std::variant<mutable_iterator, HamtError> TryMutableBegin() noexcept {
    const auto error = TryMakeUnique();
    if (error) {
      return *error;
    }
    return mutable_iterator(root_.get(), this);
  }

  template<typename Key>
  requires(
      std::is_nothrow_invocable_r_v<hash_type, const Hash&, const Key&>
      && std::is_nothrow_invocable_r_v<bool, const Equal&, const key_type&, const Key&>)
  const Entry* Find(const Key& key) const noexcept {
    return FindHamtEntry(
        root_.get(), std::invoke(hash_, key), key, EntryHash{.hash = hash_, .key_of = key_of_}, key_of_, equal_);
  }

  template<typename Key>
  requires requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
  iterator find(const Key& key) const noexcept {
    const hash_type hash = std::invoke(hash_, key);
    const Entry* const found =
        FindHamtEntry(root_.get(), hash, key, EntryHash{.hash = hash_, .key_of = key_of_}, key_of_, equal_);
    return iterator::At(root_.get(), hash, found, this);
  }

  template<typename Key>
  requires requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
  [[nodiscard]] std::variant<mutable_iterator, HamtError> TryMutableFind(const Key& key) noexcept {
    const Entry* const original = Find(key);
    if (original == nullptr) {
      return mutable_iterator{};
    }
    const hash_type hash = std::invoke(hash_, key);
    if (AllUnique(root_.get())) {
      Entry* const found = hamt_update_internal::FindUniqueEntry(root_.get(), hash, original);
      return mutable_iterator::At(root_.get(), hash, found, this);
    }
    // A lookup key may borrow a unique node released while detaching the tree.
    const Entry lookup = *original;
    const auto error = TryMakeUnique();
    if (error) {
      return *error;
    }
    const Entry* const target = Find(std::invoke(key_of_, lookup));
    Entry* const found = hamt_update_internal::FindUniqueEntry(root_.get(), hash, target);
    return mutable_iterator::At(root_.get(), hash, found, this);
  }

  template<typename Key>
  requires requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
  bool contains(const Key& key) const noexcept {
    return Find(key) != nullptr;
  }

  // Detach a shared path before public wrappers expose its mapped value.
  template<typename Key>
  requires requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
  [[nodiscard]] std::variant<Entry*, HamtError> TryGetMutable(const Key& key) noexcept {
    const Entry* target = Find(key);
    if (target == nullptr) {
      return static_cast<Entry*>(nullptr);
    }
    const hash_type hash = std::invoke(hash_, key);
    // The returned pointer intentionally grants the caller mutable mapped-value access.
    // NOLINTNEXTLINE(misc-const-correctness): Mutability is this operation's contract.
    if (Entry* const unique = hamt_update_internal::FindUniqueEntry(root_.get(), hash, target); unique != nullptr) {
      return unique;
    }
    const auto detached = try_update(key, [](Entry&) noexcept {});
    if (detached.error) {
      return *detached.error;
    }
    target = Find(key);
    return hamt_update_internal::FindUniqueEntry(root_.get(), hash, target);
  }

  [[nodiscard]] HamtMutationResult try_insert(const Entry& entry) noexcept {
    const auto& key = std::invoke(key_of_, entry);
    if (size_ == max_size() && Find(key) == nullptr) {
      return {.error = HamtError::kMaxSizeExceeded};
    }
    const auto inserted = TryInsertHamtEntry(
        root_.source(), root_.get(), std::invoke(hash_, key), key, entry, EntryHash{.hash = hash_, .key_of = key_of_},
        key_of_, equal_, true);
    if (!inserted) {
      return {.error = HamtError::kAllocationExhausted};
    }
    root_.reset(inserted->root);
    size_ += static_cast<std::size_t>(inserted->inserted);
    return {.changed = inserted->inserted};
  }

  template<typename Key>
  requires requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
  [[nodiscard]] HamtMutationResult try_erase(const Key& key) noexcept {
    const auto erased = TryEraseHamtEntry(
        root_.source(), root_.get(), std::invoke(hash_, key), key, EntryHash{.hash = hash_, .key_of = key_of_}, key_of_,
        equal_, true);
    if (!erased) {
      return {.error = HamtError::kAllocationExhausted};
    }
    root_.reset(erased->root);
    size_ -= static_cast<std::size_t>(erased->erased);
    return {.changed = erased->erased};
  }

  // Public map wrappers preserve the original immutable key in replacement.
  [[nodiscard]] HamtMutationResult try_replace(const Entry& replacement) noexcept {
    const auto& key = std::invoke(key_of_, replacement);
    const Entry* const target = Find(key);
    if (target == nullptr) {
      return {.changed = false};
    }
    const auto hash = std::invoke(hash_, key);
    if (Entry* const unique = hamt_update_internal::FindUniqueEntry(root_.get(), hash, target); unique != nullptr) {
      if (unique != std::addressof(replacement)) {
        // Copy before destruction: replacement may borrow data from the target.
        const Entry saved(replacement);
        std::destroy_at(unique);
        std::construct_at(unique, saved);
      }
      return {.changed = true};
    }
    const auto replaced = TryReplaceHamtEntry(
        root_.source(), root_.get(), hash, key, replacement, EntryHash{.hash = hash_, .key_of = key_of_}, key_of_,
        equal_);
    if (!replaced) {
      return {.error = HamtError::kAllocationExhausted};
    }
    root_.reset(replaced->root);
    return {.changed = replaced->replaced};
  }

  template<typename Key, typename Editor>
  requires(
      requires(const HamtTree& tree, const Key& key) { tree.Find(key); }
      && std::is_nothrow_invocable_v<const Editor&, Entry&>
      && std::same_as<std::invoke_result_t<const Editor&, Entry&>, void>)
  [[nodiscard]] HamtMutationResult try_update(const Key& key, const Editor& editor) noexcept {
    const auto updated = TryUpdateHamtEntry(
        root_.source(), root_.get(), std::invoke(hash_, key), key, EntryHash{.hash = hash_, .key_of = key_of_}, key_of_,
        equal_, editor);
    if (!updated) {
      return {.error = HamtError::kAllocationExhausted};
    }
    root_.reset(updated->root);
    return {.changed = updated->replaced};
  }

  void clear() noexcept {
    root_.reset();
    size_ = 0;
  }

  void swap(HamtTree& other) noexcept {
    using std::swap;
    swap(hash_, other.hash_);
    swap(key_of_, other.key_of_);
    swap(equal_, other.equal_);
    root_.swap(other.root_);
    swap(size_, other.size_);
  }

  friend void swap(HamtTree& lhs, HamtTree& rhs) noexcept { lhs.swap(rhs); }

  // Internal adoption: count must match the valid tree, inside the maximum.
  static HamtTree Adopt(
      Source& source,
      node_type* owned,
      std::size_t count,
      Hash hash,
      KeyOf key_of,
      Equal equal) noexcept {
    HamtTree tree(source, std::move(hash), std::move(key_of), std::move(equal));
    tree.root_.reset(owned);
    tree.size_ = count;
    return tree;
  }

  template<mbo::memory::BlockSource OtherSource>
  [[nodiscard]] std::optional<HamtTree<Options, Entry, Hash, KeyOf, Equal, OtherSource>> try_clone_to(
      OtherSource& destination) const noexcept {
    const auto cloned = TryCloneHamtTree(destination, root_.get());
    if (!cloned) {
      return std::nullopt;
    }
    using OtherTree = HamtTree<Options, Entry, Hash, KeyOf, Equal, OtherSource>;
    return OtherTree::Adopt(destination, *cloned, size_, hash_, key_of_, equal_);
  }

 private:
  static bool AllUnique(const node_type* node) noexcept {
    if (node == nullptr) {
      return true;
    }
    if (!node->is_unique()) {
      return false;
    }
    return std::ranges::all_of(node->children(), [](const node_type* child) noexcept { return AllUnique(child); });
  }

  struct EntryHash final {
    const Hash& hash;
    const KeyOf& key_of;

    hash_type operator()(const Entry& entry) const noexcept { return std::invoke(hash, std::invoke(key_of, entry)); }
  };

  [[no_unique_address]] Hash hash_;
  [[no_unique_address]] KeyOf key_of_;
  [[no_unique_address]] Equal equal_;
  HamtRootOwner<Options.fragment_bits, Entry, Source> root_;
  std::size_t size_ = 0;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_TREE_H_
