// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_H_
#define MBO_STRINGS_STRING_INTERNER_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

#include "mbo/container/segmented_sequence.h"
#include "mbo/strings/string_id.h"

namespace mbo::strings {

struct StringInternerOptions final {
  bool parent_first = true;
};

enum class StringInternError { kIdExhausted, kCharacterStorageExhausted, kEntryStorageExhausted, kIndexExhausted };

template<typename Id>
concept StringInternerId =
    (std::integral<Id> && !std::same_as<Id, bool>) || (requires {
      typename Id::value_type;
    } && StringIdRepresentation<typename Id::value_type> && std::same_as<Id, StringId<typename Id::value_type>>);

template<typename Container, typename Id>
concept StringInternerStringContainer =
    requires(Container& container, const Container& read, std::string_view key, Id identifier) {
      { read.find(key) } noexcept -> std::same_as<std::optional<Id>>;
      { container.try_insert(key, identifier) } noexcept -> std::same_as<std::optional<bool>>;
    };

template<typename Storage>
concept StringInternerStorage =
    requires(Storage& storage, const Storage& read, std::string_view text, const Storage::checkpoint_type& checkpoint) {
      { storage.try_store(text) } noexcept -> std::same_as<std::optional<std::string_view>>;
      { read.checkpoint() } noexcept -> std::same_as<typename Storage::checkpoint_type>;
      { storage.rewind(checkpoint) } noexcept -> std::same_as<void>;
    };

template<typename Entries>
concept StringInternerEntries =
    std::ranges::bidirectional_range<Entries> && std::same_as<typename Entries::value_type, std::string_view>
    && requires(Entries& entries, const Entries& read, std::string_view value) {
         { read.size() } -> std::convertible_to<std::size_t>;
         entries.emplace_back(value);
         entries.pop_back();
       };

// Append-only owner. Parent pointers borrow; every ancestor must outlive children.
// NOLINTBEGIN(readability-identifier-naming): StringInterner follows STL container vocabulary.
template<
    typename StringContainer,
    typename Storage,
    StringInternerId Id = StringId<std::uint32_t>,
    typename Entries = mbo::container::SegmentedSequence<std::string_view>,
    StringInternerOptions Options = {}>
requires(
    StringInternerStringContainer<StringContainer, Id> && StringInternerStorage<Storage>
    && StringInternerEntries<Entries>)
class StringInterner final {
 public:
  using id_type = Id;
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
      return owner_->get(IdFromOrdinal(position_).value_or(id_type{})).value_or(std::string_view{});
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

  StringInterner(const StringInterner&) = delete;
  StringInterner& operator=(const StringInterner&) = delete;
  StringInterner(StringInterner&&) = delete;
  StringInterner& operator=(StringInterner&&) = delete;
  ~StringInterner() = default;

  size_type size() const noexcept { return first_local_id_ + entries_.size(); }

  size_type local_size() const noexcept { return entries_.size(); }

  bool empty() const noexcept { return size() == 0; }

  const StringInterner* parent() const noexcept { return parent_; }

  size_type first_local_id() const noexcept { return first_local_id_; }

  std::optional<std::string_view> get(id_type identifier) const noexcept {
    const auto ordinal = IdOrdinal(identifier);
    if (!ordinal || *ordinal >= size()) {
      return std::nullopt;
    }
    const auto position = *ordinal;
    const auto* owner = this;
    while (position < owner->first_local_id_) {
      owner = owner->parent_;
    }
    return owner->EntryAt(position - owner->first_local_id_);
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

  [[nodiscard]] insertion_result insert_parent_first(std::string_view key) { return Insert(key, find(key)); }

  [[nodiscard]] insertion_result insert_child_first(std::string_view key) { return Insert(key, rfind(key)); }

  [[nodiscard]] insertion_result insert(std::string_view key) {
    if constexpr (Options.parent_first) {
      return insert_parent_first(key);
    } else {
      return insert_child_first(key);
    }
  }

 private:
  [[nodiscard]] static constexpr std::optional<id_type> IdFromOrdinal(size_type ordinal) noexcept {
    if constexpr (std::integral<id_type>) {
      if (!std::in_range<id_type>(ordinal)) {
        return std::nullopt;
      }
      return static_cast<id_type>(ordinal);
    } else {
      return id_type::try_from_ordinal(ordinal);
    }
  }

  [[nodiscard]] static constexpr std::optional<size_type> IdOrdinal(id_type identifier) noexcept {
    const auto value = [&] {
      if constexpr (std::integral<id_type>) {
        return identifier;
      } else {
        return identifier.value();
      }
    }();
    if (!std::in_range<size_type>(value)) {
      return std::nullopt;
    }
    return static_cast<size_type>(value);
  }

  [[nodiscard]] std::string_view EntryAt(size_type position) const noexcept {
    return *std::ranges::next(entries_.begin(), static_cast<std::ranges::range_difference_t<Entries>>(position));
  }

  [[nodiscard]] bool TryAppendEntry(std::string_view value) {
    if constexpr (requires { entries_.try_emplace_back(value); }) {
      return entries_.try_emplace_back(value).has_value();
    } else {
      entries_.emplace_back(value);
      return true;
    }
  }

  std::optional<id_type> FindLocal(std::string_view key, size_type limit) const noexcept {
    const auto found = strings_.find(key);
    const auto ordinal = found ? IdOrdinal(*found) : std::nullopt;
    return ordinal && *ordinal < limit ? found : std::nullopt;
  }

  std::optional<id_type> FindForward(std::string_view key, size_type limit) const noexcept {
    if (parent_ != nullptr) {
      if (auto found = parent_->FindForward(key, std::min(limit, first_local_id_))) {
        return found;
      }
    }
    return FindLocal(key, limit);
  }

  insertion_result Insert(std::string_view key, std::optional<id_type> existing) {
    if (existing) {
      return std::pair<id_type, bool>(*existing, false);
    }
    auto identifier = IdFromOrdinal(size());
    if (!identifier) {
      return StringInternError::kIdExhausted;
    }
    const auto checkpoint = storage_.checkpoint();
    const auto stored = storage_.try_store(key);
    if (!stored) {
      return StringInternError::kCharacterStorageExhausted;
    }
#if __cpp_exceptions
    try {
#endif
      if (!TryAppendEntry(*stored)) {
        storage_.rewind(checkpoint);
        return StringInternError::kEntryStorageExhausted;
      }
#if __cpp_exceptions
    } catch (...) {
      storage_.rewind(checkpoint);
      throw;
    }
#endif
    const auto inserted = strings_.try_insert(*stored, *identifier);
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
  StringContainer strings_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_H_
