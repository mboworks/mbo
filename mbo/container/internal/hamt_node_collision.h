// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_COLLISION_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_COLLISION_H_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "mbo/container/hamt_options.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

template<typename Entry>
struct HamtNodeCollisionInsertResult final {
  Entry* entry = nullptr;
  bool inserted = false;
  std::optional<HamtError> error;
};

// NOLINTBEGIN(readability-identifier-naming): collision buckets use STL container vocabulary.
template<
    typename Value,
    typename KeyOf,
    typename Equal,
    HamtOptions Options = {},
    typename Hash = std::uint64_t,
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
requires ValidHamtOptions<Options>
class HamtNodeCollisionBucket final {
  static_assert(std::is_nothrow_invocable_v<const KeyOf&, const Value&>);

 private:
  struct Node;

 public:
  struct Entry final {
    Hash hash;
    Value value;
  };

  constexpr HamtNodeCollisionBucket() = default;

  constexpr explicit HamtNodeCollisionBucket(Source source, KeyOf key_of = {}, Equal equal = {})
      : source_(std::move(source)), key_of_(std::move(key_of)), equal_(std::move(equal)) {}

  HamtNodeCollisionBucket(const HamtNodeCollisionBucket&) = delete;
  HamtNodeCollisionBucket& operator=(const HamtNodeCollisionBucket&) = delete;
  HamtNodeCollisionBucket(HamtNodeCollisionBucket&&) = delete;
  HamtNodeCollisionBucket& operator=(HamtNodeCollisionBucket&&) = delete;

  constexpr ~HamtNodeCollisionBucket() { clear(); }

  template<typename Key>
  constexpr Entry* find(Hash hash, const Key& key) noexcept {
    return const_cast<Entry*>(std::as_const(*this).find(hash, key));
  }

  template<typename Key>
  constexpr const Entry* find(Hash hash, const Key& key) const noexcept {
    for (const Node* node = head_; node != nullptr; node = node->next) {
      if (node->entry.hash == hash && std::invoke(equal_, std::invoke(key_of_, node->entry.value), key)) {
        return std::addressof(node->entry);
      }
    }
    return nullptr;
  }

  constexpr HamtNodeCollisionInsertResult<Entry> try_insert(Hash hash, Value value) noexcept
  requires(
      std::is_nothrow_move_constructible_v<Value>
      && noexcept(std::declval<const Equal&>()(
          std::declval<const KeyOf&>()(std::declval<const Value&>()),
          std::declval<const KeyOf&>()(std::declval<const Value&>()))))
  {
    if (Entry* const existing = find(hash, std::invoke(std::as_const(key_of_), std::as_const(value)));
        existing != nullptr) {
      return {.entry = existing};
    }
    if (size_ >= Options.maximum_size) {
      return {.error = HamtError::kMaxSizeExceeded};
    }
    const auto block = source_.TryAcquire(sizeof(Node), alignof(Node));
    if (!IsUsable(block)) {
      if (block) {
        source_.Release(*block);
      }
      return {.error = HamtError::kAllocationExhausted};
    }
    Node* const node = std::construct_at(
        reinterpret_cast<Node*>(block->data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        Node{.next = head_, .block = *block, .entry = Entry{.hash = hash, .value = std::move(value)}});
    head_ = node;
    ++size_;
    return {.entry = std::addressof(node->entry), .inserted = true};
  }

  template<typename Key>
  constexpr bool erase(Hash hash, const Key& key) noexcept {
    Node** link = &head_;
    while (*link != nullptr) {
      Node* const node = *link;
      if (node->entry.hash == hash
          && std::invoke(
              std::as_const(equal_), std::invoke(std::as_const(key_of_), std::as_const(node->entry.value)), key)) {
        *link = node->next;
        Destroy(node);
        --size_;
        return true;
      }
      link = &node->next;
    }
    return false;
  }

  constexpr void clear() noexcept {
    while (head_ != nullptr) {
      Node* const node = head_;
      head_ = node->next;
      Destroy(node);
    }
    size_ = 0;
  }

  constexpr std::size_t size() const noexcept { return size_; }

  constexpr bool empty() const noexcept { return size_ == 0; }

 private:
  struct Node final {
    Node* next;
    mbo::memory::MemoryBlock block;
    Entry entry;
  };

  static constexpr bool IsUsable(const std::optional<mbo::memory::MemoryBlock>& block) noexcept {
    return block && block->data != nullptr && block->size >= sizeof(Node) && block->alignment >= alignof(Node)
           && std::bit_cast<std::uintptr_t>(block->data) % alignof(Node) == 0;
  }

  constexpr void Destroy(Node* node) noexcept {
    const mbo::memory::MemoryBlock block = node->block;
    std::destroy_at(node);
    source_.Release(block);
  }

  [[no_unique_address]] Source source_{};
  [[no_unique_address]] KeyOf key_of_{};
  [[no_unique_address]] Equal equal_{};
  Node* head_ = nullptr;
  std::size_t size_ = 0;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_COLLISION_H_
