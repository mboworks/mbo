// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_H_
#define MBO_STRINGS_STRING_INTERNER_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mbo/container/segmented_sequence.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/hamt_string_index.h"
#include "mbo/strings/string_id.h"

namespace mbo::strings {

struct StringInternerOptions final {
  bool parent_first = true;
};

enum class StringInternError { kIdExhausted, kCharacterStorageExhausted, kEntryStorageExhausted, kIndexExhausted };

template<typename Index, typename Id>
concept StringInternerIndex = requires(Index& index, const Index& read, std::string_view key, Id identifier) {
  { read.find(key) } noexcept -> std::same_as<std::optional<Id>>;
  { index.try_insert(key, identifier) } noexcept -> std::same_as<std::optional<bool>>;
};

template<typename Storage>
concept StringInternerStorage =
    requires(Storage& storage, const Storage& read, std::string_view text, const Storage::checkpoint_type& checkpoint) {
      { storage.try_store(text) } noexcept -> std::same_as<std::optional<std::string_view>>;
      { read.checkpoint() } noexcept -> std::same_as<typename Storage::checkpoint_type>;
      { storage.rewind(checkpoint) } noexcept -> std::same_as<void>;
    };

// Append-only owner. Parent pointers borrow; every ancestor must outlive children.
// NOLINTBEGIN(readability-identifier-naming): StringInterner follows STL container vocabulary.
template<
    StringIdRepresentation Representation = std::uint32_t,
    typename Storage = ArenaStringStorage<>,
    typename Entries = mbo::container::SegmentedSequence<std::string_view>,
    typename Index = HamtStringIndex<StringId<Representation>>,
    StringInternerOptions Options = {}>
requires(StringInternerIndex<Index, StringId<Representation>> && StringInternerStorage<Storage>)
class StringInterner final {
 public:
  using id_type = StringId<Representation>;
  using size_type = std::size_t;
  using insertion_result = std::variant<std::pair<id_type, bool>, StringInternError>;

  class iterator final {
   public:
    using value_type = std::string_view;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    iterator() noexcept = default;

    reference operator*() const noexcept {
      return owner_->get(id_type(static_cast<Representation>(position_))).value_or(std::string_view{});
    }

    iterator& operator++() noexcept {
      ++position_;
      return *this;
    }

    iterator operator++(int) noexcept {
      auto before = *this;
      ++*this;
      return before;
    }

    iterator& operator--() noexcept {
      --position_;
      return *this;
    }

    iterator operator--(int) noexcept {
      auto before = *this;
      --*this;
      return before;
    }

    friend bool operator==(const iterator&, const iterator&) noexcept = default;

   private:
    friend class StringInterner;

    iterator(const StringInterner* owner, size_type position) noexcept : owner_(owner), position_(position) {}

    const StringInterner* owner_ = nullptr;
    size_type position_ = 0;
  };

  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;

  iterator begin() const noexcept { return iterator(this, 0); }

  iterator end() const noexcept { return iterator(this, size()); }

  iterator cbegin() const noexcept { return begin(); }

  iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }

  reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

  StringInterner() = default;

  explicit StringInterner(const StringInterner* parent) noexcept
      : parent_(parent), first_local_id_(parent == nullptr ? 0 : parent->size()) {}

  template<typename IndexFactory>
  requires(std::invocable<IndexFactory&> && std::same_as<std::invoke_result_t<IndexFactory&>, Index>)
  StringInterner(const StringInterner* parent, IndexFactory factory) noexcept(
      std::is_nothrow_invocable_v<IndexFactory&> && std::is_nothrow_default_constructible_v<Storage>
      && std::is_nothrow_default_constructible_v<Entries>)
      : parent_(parent), first_local_id_(parent == nullptr ? 0 : parent->size()), index_(std::invoke(factory)) {}

  StringInterner(const StringInterner&) = delete;

  template<typename StorageFactory, typename EntriesFactory, typename IndexFactory>
  requires(std::is_nothrow_invocable_v<StorageFactory&> && std::same_as<std::invoke_result_t<StorageFactory&>, Storage>
           && std::is_nothrow_invocable_v<EntriesFactory&>
           && std::same_as<std::invoke_result_t<EntriesFactory&>, Entries> && std::is_nothrow_invocable_v<IndexFactory&>
           && std::same_as<std::invoke_result_t<IndexFactory&>, Index>)
  StringInterner(
      const StringInterner* parent,
      StorageFactory storage,
      EntriesFactory entries,
      IndexFactory index) noexcept
      : parent_(parent),
        first_local_id_(parent == nullptr ? 0 : parent->size()),
        storage_(std::invoke(storage)),
        entries_(std::invoke(entries)),
        index_(std::invoke(index)) {}

  StringInterner& operator=(const StringInterner&) = delete;
  StringInterner(StringInterner&&) = delete;
  StringInterner& operator=(StringInterner&&) = delete;
  ~StringInterner() = default;

  size_type size() const noexcept { return first_local_id_ + entries_.size(); }

  size_type local_size() const noexcept { return entries_.size(); }

  bool empty() const noexcept { return size() == 0; }

  const StringInterner* parent() const noexcept { return parent_; }

  size_type first_local_id() const noexcept { return first_local_id_; }

  // Cold-path diagnostics: no counters or allocations on insertion and lookup.
  // Visits exactly the visible prefix, in ID order, including empty strings.
  template<typename Visitor>
  requires(std::is_nothrow_invocable_r_v<void, Visitor&, size_type>)
  void visit_string_sizes(Visitor&& visitor) const noexcept {
    auto&& callback = std::forward<Visitor>(visitor);
    for (const std::string_view text : *this) {
      std::invoke(callback, text.size());
    }
  }

  // These figures belong to this node's character storage, not its ancestors,
  // entry descriptors, or index. Unsupported backends report unknown, not zero.
  std::optional<size_type> local_character_bytes_used() const noexcept {
    if constexpr (requires(const Storage& storage) {
                    { storage.bytes_used() } noexcept -> std::same_as<size_type>;
                  }) {
      return storage_.bytes_used();
    } else {
      return std::nullopt;
    }
  }

  std::optional<size_type> local_character_bytes_reserved() const noexcept {
    if constexpr (requires(const Storage& storage) {
                    { storage.bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      return storage_.bytes_reserved();
    } else {
      return std::nullopt;
    }
  }

  std::optional<std::string_view> get(id_type identifier) const noexcept {
    if (std::cmp_greater_equal(identifier.value(), size())) {
      return std::nullopt;
    }
    const auto position = static_cast<size_type>(identifier.value());
    const auto* owner = this;
    while (position < owner->first_local_id_) {
      owner = owner->parent_;
    }
    return owner->entries_.at(position - owner->first_local_id_);
  }

  std::optional<id_type> find(std::string_view key) const noexcept { return FindForward(key, size()); }

  std::optional<id_type> rfind(std::string_view key) const noexcept {
    const auto* owner = this;
    auto limit = size();
    while (owner != nullptr) {
      if (auto found = owner->FindLocal(key, limit)) {
        return found;
      }
      limit = std::min(limit, owner->first_local_id_);
      owner = owner->parent_;
    }
    return std::nullopt;
  }

  [[nodiscard]] insertion_result intern_parent_first(std::string_view key) noexcept { return Insert(key, find(key)); }

  [[nodiscard]] insertion_result intern_child_first(std::string_view key) noexcept { return Insert(key, rfind(key)); }

  [[nodiscard]] insertion_result intern(std::string_view key) noexcept {
    if constexpr (Options.parent_first) {
      return intern_parent_first(key);
    } else {
      return intern_child_first(key);
    }
  }

 private:
  std::optional<id_type> FindLocal(std::string_view key, size_type limit) const noexcept {
    const auto found = index_.find(key);
    return found && std::cmp_less(found->value(), limit) ? found : std::nullopt;
  }

  std::optional<id_type> FindForward(std::string_view key, size_type limit) const noexcept {
    if (parent_ != nullptr) {
      if (auto found = parent_->FindForward(key, std::min(limit, first_local_id_))) {
        return found;
      }
    }
    return FindLocal(key, limit);
  }

  insertion_result Insert(std::string_view key, std::optional<id_type> existing) noexcept {
    if (existing) {
      return std::pair<id_type, bool>(*existing, false);
    }
    auto identifier = id_type::try_from_ordinal(size());
    if (!identifier) {
      return StringInternError::kIdExhausted;
    }
    const auto checkpoint = storage_.checkpoint();
    const auto stored = storage_.try_store(key);
    if (!stored) {
      return StringInternError::kCharacterStorageExhausted;
    }
    if (!entries_.try_emplace_back(*stored)) {
      storage_.rewind(checkpoint);
      return StringInternError::kEntryStorageExhausted;
    }
    const auto inserted = index_.try_insert(*stored, *identifier);
    if (!inserted || !*inserted) {
      entries_.pop_back();
      storage_.rewind(checkpoint);
      return StringInternError::kIndexExhausted;
    }
    return std::pair<id_type, bool>(*identifier, true);
  }

  const StringInterner* parent_ = nullptr;
  size_type first_local_id_ = 0;
  Storage storage_;
  Entries entries_;
  Index index_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_H_
