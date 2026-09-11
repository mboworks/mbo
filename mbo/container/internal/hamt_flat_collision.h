// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_FLAT_COLLISION_H_
#define MBO_CONTAINER_INTERNAL_HAMT_FLAT_COLLISION_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "mbo/container/hamt_options.h"
#include "mbo/container/internal/hamt_collision.h"
#include "mbo/container/segmented_sequence.h"

namespace mbo::container::container_internal {

template<typename Hash, typename Value>
struct HamtStoredValue final {
  Hash hash;
  Value value;
};

template<typename Entry>
struct HamtCollisionInsertResult final {
  Entry* entry = nullptr;
  bool inserted = false;
  std::optional<HamtError> error;
};

template<
    typename Value,
    typename KeyOf,
    typename Equal,
    HamtOptions Options = {},
    typename Hash = std::uint64_t,
    typename Entry = HamtStoredValue<Hash, Value>,
    typename Storage = SegmentedSequence<Entry>>
requires ValidHamtOptions<Options>
class HamtFlatCollisionBucket final {
 public:
  using entry_type = Entry;
  using iterator = typename Storage::iterator;
  using const_iterator = typename Storage::const_iterator;

  constexpr HamtFlatCollisionBucket() = default;

  constexpr explicit HamtFlatCollisionBucket(KeyOf key_of, Equal equal)
      : key_of_(std::move(key_of)), equal_(std::move(equal)) {}

  template<typename Key>
  constexpr iterator find(Hash hash, const Key& key) noexcept {
    return FindHamtCollision(begin(), end(), hash, key, HashOf{}, EntryKeyOf{key_of_}, equal_);
  }

  template<typename Key>
  constexpr const_iterator find(Hash hash, const Key& key) const noexcept {
    return FindHamtCollision(begin(), end(), hash, key, HashOf{}, EntryKeyOf{key_of_}, equal_);
  }

  constexpr HamtCollisionInsertResult<Entry> try_insert(Hash hash, Value value) noexcept
  requires(
      std::is_nothrow_move_constructible_v<Value>
      && noexcept(std::declval<Equal&>()(
          std::declval<KeyOf&>()(std::declval<const Value&>()),
          std::declval<KeyOf&>()(std::declval<const Value&>()))))
  {
    const auto existing = find(hash, std::invoke(key_of_, value));
    if (existing != end()) {
      return {.entry = std::addressof(*existing), .inserted = false};
    }
    if (size() >= Options.maximum_size) {
      return {.error = HamtError::kMaxSizeExceeded};
    }
    auto inserted = entries_.try_emplace_back(Entry{.hash = hash, .value = std::move(value)});
    if (!inserted) {
      return {.error = HamtError::kAllocationExhausted};
    }
    return {.entry = std::addressof(inserted->get()), .inserted = true};
  }

  template<typename Key>
  constexpr bool erase(Hash hash, const Key& key) noexcept
  requires std::is_nothrow_move_assignable_v<Entry>
  {
    const auto pos = find(hash, key);
    if (pos == end()) {
      return false;
    }
    if (pos != end() - 1) {
      *pos = std::move(entries_.back());
    }
    entries_.pop_back();
    return true;
  }

  constexpr iterator begin() noexcept { return entries_.begin(); }

  constexpr const_iterator begin() const noexcept { return entries_.begin(); }

  constexpr iterator end() noexcept { return entries_.end(); }

  constexpr const_iterator end() const noexcept { return entries_.end(); }

  constexpr std::size_t size() const noexcept { return entries_.size(); }

  constexpr bool empty() const noexcept { return entries_.empty(); }

 private:
  struct HashOf final {
    constexpr Hash operator()(const Entry& entry) const noexcept { return entry.hash; }
  };

  struct EntryKeyOf final {
    KeyOf key_of;

    constexpr decltype(auto) operator()(const Entry& entry) const noexcept { return std::invoke(key_of, entry.value); }
  };

  [[no_unique_address]] KeyOf key_of_;
  [[no_unique_address]] Equal equal_;
  Storage entries_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_FLAT_COLLISION_H_
