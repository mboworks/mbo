// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_TABLE_H_
#define MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_TABLE_H_

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

#include "mbo/config/require.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/internal/frozen_index.h"
#include "mbo/types/optional_ref.h"

namespace mbo::container::experimental::frozen_internal {

// NOLINTBEGIN(readability-identifier-naming): standard container and iterator vocabulary.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// bounded table indices.

// NOLINTBEGIN(bugprone-unchecked-optional-access): index slots refer only to engaged elements in [0, size).

template<typename Key, typename Value, auto CapacityOrOptions, typename Hash, typename KeyEqual>
class FrozenTable {
  static_assert(std::is_object_v<Key>, "Frozen keys must be object types");
  static_assert(
      std::is_invocable_r_v<std::uint64_t, const Hash&, const Key&>,
      "Frozen hash must be callable through a const reference");
  static_assert(
      std::predicate<const KeyEqual&, const Key&, const Key&>,
      "Frozen equality must be callable through a const reference");
  static constexpr FrozenOptions kOptions = Options<CapacityOrOptions>();
  static_assert(kOptions.capacity <= 4'096, "Frozen capacity exceeds construction bound (4096)");
  static_assert(kOptions.slots <= 65'536, "Frozen slots exceed construction bound (65536)");
  static_assert(kOptions.max_seed <= 65'536, "Frozen seed limit exceeds construction bound (65536)");
  static_assert(kOptions.max_work <= 1'048'576, "Frozen work limit exceeds construction bound (1048576)");
  using Index = FrozenIndex<kOptions>;
  static constexpr std::size_t kCapacity = Index::kCapacity;
  static constexpr std::size_t kSlots = Index::kSlots;
  static constexpr std::uint32_t kEmpty = Index::kEmpty;
  static constexpr bool kSet = std::same_as<Key, Value>;

  struct Data {
    std::array<std::optional<Value>, kCapacity> values{};
    Index index;
    std::size_t size = 0;
    std::size_t work = 0;
    [[no_unique_address]] Hash hash;
    [[no_unique_address]] KeyEqual equal;
  };

  static constexpr const Key& GetKey(const Value& value) noexcept {
    if constexpr (kSet) {
      // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter): the caller owns the stored element.
      return value;
    } else {
      return value.first;
    }
  }

  static constexpr void CheckDuplicate(const Value& lhs, const Value& rhs) {
    if constexpr (!kSet) {
      if constexpr (requires {
                      { lhs.second == rhs.second } -> std::convertible_to<bool>;
                    }) {
        MBO_CONFIG_REQUIRE(lhs.second == rhs.second, "Frozen conflicting duplicate key");
      } else {
        MBO_CONFIG_REQUIRE(false, "Frozen duplicate mapped values cannot be compared");
      }
    }
  }

  struct Builder {
    Data data;
    Index::Hashes hashes{};
    std::size_t key_bytes = 0;

    constexpr void Add(Value value) {
      Index::Spend(data.work);
      const Key& key = GetKey(value);
      CheckKeyBytes(key);
      const std::uint64_t code = std::as_const(data.hash)(key);
      MBO_CONFIG_REQUIRE(kSlots > 0, "Frozen slot capacity exceeded");
      const auto slot = FindSlot(value, code);
      if (!slot) {
        return;
      }
      MBO_CONFIG_REQUIRE(data.size < kCapacity, "Frozen element capacity exceeded");
      hashes[data.size] = code;
      data.index.slots[*slot] = static_cast<std::uint32_t>(data.size);
      data.values[data.size++].emplace(std::move(value));
    }

    constexpr std::optional<std::size_t> FindSlot(const Value& value, std::uint64_t code) {
      std::size_t slot = code % std::max<std::size_t>(1, kSlots);
      std::size_t probes = 0;
      while (data.index.slots[slot] != kEmpty) {
        Index::Spend(data.work);
        const auto index = data.index.slots[slot];
        if (hashes[index] == code) {
          const bool equivalent = std::as_const(data.equal)(GetKey(*data.values[index]), GetKey(value));
          MBO_CONFIG_REQUIRE(equivalent, "Frozen distinct keys have identical hashes");
          CheckDuplicate(*data.values[index], value);
          return std::nullopt;
        }
        ++probes;
        MBO_CONFIG_REQUIRE(probes < kSlots, "Frozen slot capacity exceeded");
        slot = (slot + 1) % std::max<std::size_t>(1, kSlots);
      }
      return slot;
    }

    constexpr void CheckKeyBytes(const Key& key) {
      if constexpr (std::same_as<Key, std::string_view>) {
        const bool fits = key.size() <= kOptions.max_key_bytes - key_bytes;
        MBO_CONFIG_REQUIRE(fits, "Frozen key byte budget exhausted");
        key_bytes += key.size();
      }
    }
  };

  template<std::input_iterator It, std::sentinel_for<It> End>
  static constexpr Data Build(It first, End last, Hash hash, KeyEqual equal) {
    Builder builder{.data = {.hash = std::move(hash), .equal = std::move(equal)}};
    for (; first != last; ++first) {
      builder.Add(Value(*first));
    }
    builder.data.index.Build(builder.hashes, builder.data.size, builder.data.work);
    return std::move(builder.data);
  }

 public:
  using key_type = Key;
  using value_type = Value;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = const Value&;
  using const_reference = const Value&;
  using pointer = const Value*;
  using const_pointer = const Value*;
  static constexpr size_type npos = std::numeric_limits<size_type>::max();

  template<bool Local>
  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;
    using value_type = Value;
    using difference_type = std::ptrdiff_t;
    using reference = const Value&;
    using pointer = const Value*;

    constexpr Iterator() noexcept = default;

    constexpr reference operator*() const noexcept { return *owner_->data_.values[pos_]; }

    constexpr pointer operator->() const noexcept { return std::addressof(**this); }

    constexpr Iterator& operator++() noexcept {
      if constexpr (Local) {
        pos_ = owner_->size();
      } else {
        ++pos_;
      }
      return *this;
    }

    constexpr Iterator operator++(int) noexcept {
      auto previous = *this;
      ++*this;
      return previous;
    }

    constexpr bool operator==(const Iterator&) const noexcept = default;

   private:
    friend class FrozenTable;

    constexpr Iterator(const FrozenTable* owner, size_type pos) noexcept : owner_(owner), pos_(pos) {}

    const FrozenTable* owner_ = nullptr;
    size_type pos_ = 0;
  };

  using iterator = Iterator<false>;
  using const_iterator = iterator;
  using local_iterator = Iterator<true>;
  using const_local_iterator = local_iterator;

  static constexpr bool kTransparent = requires {
    typename Hash::is_transparent;
    typename KeyEqual::is_transparent;
  };
  template<typename K>
  static constexpr bool kForeign =
      kTransparent && !std::same_as<std::remove_cvref_t<K>, Key> && std::invocable<const Hash&, const K&>
      && std::predicate<const KeyEqual&, const Key&, const K&>;

  constexpr FrozenTable() : FrozenTable(std::array<Value, 0>{}) {}

  template<std::input_iterator It, std::sentinel_for<It> End>
  constexpr FrozenTable(It first, End last, Hash hash = {}, KeyEqual equal = {})
      : data_(Build(first, last, std::move(hash), std::move(equal))) {}

  template<typename U, std::size_t N>
  constexpr explicit FrozenTable(const std::array<U, N>& values, Hash hash = {}, KeyEqual equal = {})
      : FrozenTable(values.begin(), values.end(), std::move(hash), std::move(equal)) {}

  constexpr FrozenTable(std::initializer_list<Value> values, Hash hash = {}, KeyEqual equal = {})
      : FrozenTable(values.begin(), values.end(), std::move(hash), std::move(equal)) {}

  template<std::ranges::input_range Range>
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): traverse the named range within its lifetime.
  constexpr FrozenTable(std::from_range_t /*tag*/, Range&& values, Hash hash = {}, KeyEqual equal = {})
      : FrozenTable(std::ranges::begin(values), std::ranges::end(values), std::move(hash), std::move(equal)) {}

  constexpr FrozenTable(const FrozenTable&) = default;
  constexpr FrozenTable(FrozenTable&&) noexcept(std::is_nothrow_copy_constructible_v<Data>) = default;
  constexpr FrozenTable& operator=(const FrozenTable&) = delete;
  constexpr FrozenTable& operator=(FrozenTable&&) = delete;
  constexpr ~FrozenTable() = default;

  constexpr iterator begin() const noexcept { return {this, 0}; }

  constexpr iterator end() const noexcept { return {this, size()}; }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr bool empty() const noexcept { return size() == 0; }

  constexpr size_type size() const noexcept { return data_.size; }

  constexpr size_type capacity() const noexcept { return kCapacity; }

  constexpr size_type max_size() const noexcept { return std::min(kCapacity, kSlots); }

  constexpr Hash hash_function() const { return data_.hash; }

  constexpr KeyEqual key_eq() const { return data_.equal; }

  constexpr size_type bucket_count() const noexcept { return kSlots; }

  constexpr size_type max_bucket_count() const noexcept { return kSlots; }

  constexpr float max_load_factor() const noexcept { return 1.0F; }

  constexpr float load_factor() const noexcept {
    return kSlots == 0 ? 0.0F : static_cast<float>(size()) / static_cast<float>(kSlots);
  }

  constexpr size_type construction_work() const noexcept { return data_.work; }

  constexpr const_reference at_index(size_type index) const {
    MBO_CONFIG_REQUIRE(index < size(), "Frozen index out of range");
    return *data_.values[index];
  }

  constexpr size_type bucket(const Key& key) const { return Bucket(key); }

  template<typename K>
  requires(kForeign<K>)
  constexpr size_type bucket(const K& key) const {
    return Bucket(key);
  }

  constexpr size_type bucket_size(size_type index) const {
    MBO_CONFIG_REQUIRE(index < kSlots, "Frozen bucket out of range");
    return data_.index.slots[index] == kEmpty ? 0 : 1;
  }

  constexpr local_iterator begin(size_type index) const {
    return {this, bucket_size(index) == 0 ? size() : data_.index.slots[index]};
  }

  constexpr local_iterator end(size_type index) const {
    MBO_CONFIG_REQUIRE(index < kSlots, "Frozen bucket out of range");
    return {this, size()};
  }

  constexpr const_local_iterator cbegin(size_type index) const { return begin(index); }

  constexpr const_local_iterator cend(size_type index) const { return end(index); }

  constexpr size_type index_of(const Key& key) const { return IndexOf(key); }

  template<typename K>
  requires(kForeign<K>)
  constexpr size_type index_of(const K& key) const {
    return IndexOf(key);
  }

  constexpr iterator find(const Key& key) const { return Find(key); }

  template<typename K>
  requires(kForeign<K>)
  constexpr iterator find(const K& key) const {
    return Find(key);
  }

  constexpr bool contains(const Key& key) const { return index_of(key) != npos; }

  template<typename K>
  requires(kForeign<K>)
  constexpr bool contains(const K& key) const {
    return index_of(key) != npos;
  }

  constexpr size_type count(const Key& key) const { return contains(key) ? 1 : 0; }

  template<typename K>
  requires(kForeign<K>)
  constexpr size_type count(const K& key) const {
    return contains(key) ? 1 : 0;
  }

  constexpr std::pair<iterator, iterator> equal_range(const Key& key) const { return EqualRange(key); }

  template<typename K>
  requires(kForeign<K>)
  constexpr std::pair<iterator, iterator> equal_range(const K& key) const {
    return EqualRange(key);
  }

  template<std::ranges::input_range Range>
  constexpr bool contains_all(Range&& keys) const {
    return std::ranges::all_of(std::forward<Range>(keys), [this](const auto& key) { return this->contains(key); });
  }

  template<std::ranges::input_range Range>
  constexpr bool contains_any(Range&& keys) const {
    return std::ranges::any_of(std::forward<Range>(keys), [this](const auto& key) { return this->contains(key); });
  }

 protected:
  template<typename K>
  constexpr auto Lookup(const K& key) const {
    using Result =
        std::conditional_t<kSet, Key, typename std::conditional_t<kSet, std::pair<Key, Key>, Value>::second_type>;
    const auto index = IndexOf(key);
    if (index == npos) {
      return mbo::types::OptionalRef<const Result>{};
    }
    if constexpr (kSet) {
      return mbo::types::OptionalRef<const Result>{*data_.values[index]};
    } else {
      return mbo::types::OptionalRef<const Result>{data_.values[index]->second};
    }
  }

 private:
  template<typename K>
  constexpr size_type Bucket(const K& key) const {
    return data_.index.Bucket(data_.hash(key));
  }

  template<typename K>
  constexpr size_type IndexOf(const K& key) const {
    if (empty()) {
      return npos;
    }
    const auto index = data_.index.slots[Bucket(key)];
    return index != kEmpty && data_.equal(GetKey(*data_.values[index]), key) ? index : npos;
  }

  template<typename K>
  constexpr iterator Find(const K& key) const {
    const auto index = IndexOf(key);
    return {this, index == npos ? size() : index};
  }

  template<typename K>
  constexpr std::pair<iterator, iterator> EqualRange(const K& key) const {
    const auto found = Find(key);
    return {found, found == end() ? found : std::next(found)};
  }

  const Data data_;
};

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::experimental::frozen_internal

#endif  // MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_TABLE_H_
