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

#include "mbo/config/require.h"
#include "mbo/container/segmented_sequence.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/hamt_string_index.h"
#include "mbo/strings/string_id.h"

namespace mbo::strings {

struct StringInternerOptions final {
  bool parent_first = true;
  std::size_t maximum_parent_depth = std::numeric_limits<std::size_t>::max();
};

enum class StringInternError {
  kIdExhausted,
  kCharacterStorageExhausted,
  kEntryStorageExhausted,
  kIndexExhausted,
  kMappedStorageExhausted,
};

template<typename Index, typename Id>
concept StringInternerIndex = std::is_nothrow_destructible_v<Index>
                              && requires(Index& index, const Index& read, std::string_view key, Id identifier) {
                                   { read.find(key) } noexcept -> std::same_as<std::optional<Id>>;
                                   { index.try_insert(key, identifier) } noexcept -> std::same_as<std::optional<bool>>;
                                 };

template<typename Storage>
concept StringInternerStorage = std::is_nothrow_destructible_v<Storage>
                                && requires(
                                    Storage& storage,
                                    const Storage& read,
                                    std::string_view text,
                                    const Storage::checkpoint_type& checkpoint) {
                                     {
                                       storage.try_store(text)
                                     } noexcept -> std::same_as<std::optional<std::string_view>>;
                                     { read.checkpoint() } noexcept -> std::same_as<typename Storage::checkpoint_type>;
                                     { storage.rewind(checkpoint) } noexcept -> std::same_as<void>;
                                   };

// Syntax cannot prove stable views or rollback: successful append adds exactly
// one descriptor; failed append changes nothing; pop removes only the last one.
template<typename Entries>
concept StringInternerEntries =
    std::is_nothrow_destructible_v<Entries>
    && requires(Entries& entries, const Entries& read, std::string_view value, std::size_t ordinal) {
         requires std::same_as<typename Entries::value_type, std::string_view>;
         { read.size() } noexcept -> std::same_as<std::size_t>;
         { read.at(ordinal) } -> std::convertible_to<std::string_view>;
         { static_cast<bool>(entries.try_emplace_back(value)) } -> std::same_as<bool>;
         { entries.pop_back() } -> std::same_as<void>;
       };

// Append-only owner. Parent pointers borrow; every ancestor must outlive children.
// NOLINTBEGIN(readability-identifier-naming): StringInterner follows STL container vocabulary.
template<
    StringIdRepresentation Representation = std::uint32_t,
    typename Storage = ArenaStringStorage<>,
    typename Entries = mbo::container::SegmentedSequence<std::string_view>,
    typename Index = HamtStringIndex<StringId<Representation>>,
    StringInternerOptions Options = {}>
requires(
    StringInternerIndex<Index, StringId<Representation>> && StringInternerStorage<Storage>
    && StringInternerEntries<Entries>)
class StringInterner final {
 public:
  using id_type = StringId<Representation>;
  using size_type = std::size_t;
  using insertion_result = std::variant<std::pair<id_type, bool>, StringInternError>;

  struct LookupTrace final {
    std::optional<id_type> id;
    size_type index_queries = 0;
    std::optional<size_type> parent_depth;
  };

  struct EntryStorageDiagnostics final {
    size_type descriptors = 0;
    size_type live_descriptor_bytes = 0;
    std::optional<size_type> segment_bytes_reserved;
    std::optional<size_type> lookup_directory_bytes_reserved;
    std::optional<size_type> segment_directory_bytes_reserved;
  };

  // One local interner node. Optional byte counts remain unknown when a
  // configured backend does not expose the corresponding cold diagnostic.
  struct StorageDiagnostics final {
    size_type string_count = 0;
    std::optional<size_type> character_bytes_used;
    std::optional<size_type> character_bytes_reserved;
    size_type live_descriptor_bytes = 0;
    std::optional<size_type> descriptor_segment_bytes_reserved;
    std::optional<size_type> descriptor_lookup_directory_bytes_reserved;
    std::optional<size_type> descriptor_segment_directory_bytes_reserved;
    std::optional<size_type> index_nodes;
    std::optional<size_type> index_entries;
    std::optional<size_type> index_collision_nodes;
    std::optional<size_type> index_collision_entries;
    std::optional<size_type> index_largest_collision;
    std::optional<size_type> index_maximum_depth;
    std::optional<size_type> index_node_bytes;
    std::optional<size_type> index_entry_bytes;
  };

  class iterator final {
   public:
    using value_type = std::string_view;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    iterator() noexcept = default;

    reference operator*() const noexcept(!::mbo::config::kRequireThrows) {
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot dereference a singular StringInterner iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ < owner_->size(), "Cannot dereference a StringInterner end iterator");
      return current_owner_->entries_.at(position_ - current_owner_->first_local_id_);
    }

    iterator& operator++() noexcept(!::mbo::config::kRequireThrows) {
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot increment a singular StringInterner iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ < owner_->size(), "Cannot increment a StringInterner end iterator");
      ++position_;
      if (position_ < owner_->size() && position_ >= current_limit_) {
        Resolve();
      }
      return *this;
    }

    iterator operator++(int) noexcept(!::mbo::config::kRequireThrows) {
      auto before = *this;
      ++*this;
      return before;
    }

    iterator& operator--() noexcept(!::mbo::config::kRequireThrows) {
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot decrement a singular StringInterner iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ > 0, "Cannot decrement a StringInterner begin iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ <= owner_->size(), "StringInterner iterator is out of range");
      --position_;
      if (position_ < current_owner_->first_local_id_) {
        Resolve();
      }
      return *this;
    }

    iterator operator--(int) noexcept(!::mbo::config::kRequireThrows) {
      auto before = *this;
      --*this;
      return before;
    }

    friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
      return lhs.owner_ == rhs.owner_ && lhs.position_ == rhs.position_;
    }

   private:
    friend class StringInterner;

    iterator(const StringInterner* owner, size_type position) noexcept : owner_(owner), position_(position) {
      Resolve();
    }

    void Resolve() noexcept {
      current_owner_ = owner_;
      current_limit_ = owner_->size();
      while (position_ < current_owner_->first_local_id_) {
        current_limit_ = std::min(current_limit_, current_owner_->first_local_id_);
        current_owner_ = current_owner_->parent_;
      }
    }

    const StringInterner* owner_ = nullptr;
    const StringInterner* current_owner_ = nullptr;
    size_type position_ = 0;
    size_type current_limit_ = 0;
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
      : parent_(ValidateParent(parent)), first_local_id_(parent == nullptr ? 0 : parent->size()) {}

  template<typename IndexFactory>
  requires(std::is_nothrow_invocable_v<IndexFactory&> && std::same_as<std::invoke_result_t<IndexFactory&>, Index>
           && std::is_nothrow_default_constructible_v<Storage> && std::is_nothrow_default_constructible_v<Entries>)
  StringInterner(const StringInterner* parent, IndexFactory factory) noexcept
      : parent_(ValidateParent(parent)),
        first_local_id_(parent == nullptr ? 0 : parent->size()),
        index_(std::invoke(factory)) {}

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
      : parent_(ValidateParent(parent)),
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

  static constexpr size_type max_size() noexcept {
    if constexpr (std::numeric_limits<Representation>::digits < std::numeric_limits<size_type>::digits) {
      return static_cast<size_type>(std::numeric_limits<Representation>::max());
    } else {
      return std::numeric_limits<size_type>::max();
    }
  }

  static constexpr size_type max_parent_depth() noexcept { return Options.maximum_parent_depth; }

  bool empty() const noexcept { return size() == 0; }

  const StringInterner* parent() const noexcept { return parent_; }

  const StringInterner* root() const noexcept {
    const auto* owner = this;
    while (owner->parent_ != nullptr) {
      owner = owner->parent_;
    }
    return owner;
  }

  // Direct-parent growth only. Later ancestor entries never become visible.
  bool parent_has_grown() const noexcept { return parent_ != nullptr && parent_->size() > first_local_id_; }

  size_type first_local_id() const noexcept { return first_local_id_; }

  // Only this node's index. Parent indexes may contain post-cutoff insertions
  // and must be inspected separately, never summed as visible-string counts.
  auto local_index_diagnostics() const noexcept
  requires requires(const Index& index) {
    { index.structural_diagnostics() } noexcept;
  }
  {
    return index_.structural_diagnostics();
  }

  template<typename Visitor>
  requires requires(const Index& index, Visitor& visitor) {
    { index.visit_node_diagnostics(visitor) } noexcept;
  }
  void visit_local_index_nodes(Visitor&& visitor) const noexcept {
    index_.visit_node_diagnostics(std::forward<Visitor>(visitor));
  }

  EntryStorageDiagnostics local_entry_storage_diagnostics() const noexcept {
    EntryStorageDiagnostics result{
        .descriptors = local_size(), .live_descriptor_bytes = local_size() * sizeof(std::string_view)};
    if constexpr (requires(const Entries& entries) {
                    { entries.bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.segment_bytes_reserved = entries_.bytes_reserved();
    }
    if constexpr (requires(const Entries& entries) {
                    { entries.directory_bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.lookup_directory_bytes_reserved = entries_.directory_bytes_reserved();
    }
    if constexpr (requires(const Entries& entries) {
                    { entries.segment_directory_bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.segment_directory_bytes_reserved = entries_.segment_directory_bytes_reserved();
    }
    return result;
  }

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

  StorageDiagnostics local_storage_diagnostics() const noexcept {
    const auto entries = local_entry_storage_diagnostics();
    StorageDiagnostics result{
        .string_count = local_size(),
        .character_bytes_used = local_character_bytes_used(),
        .character_bytes_reserved = local_character_bytes_reserved(),
        .live_descriptor_bytes = entries.live_descriptor_bytes,
        .descriptor_segment_bytes_reserved = entries.segment_bytes_reserved,
        .descriptor_lookup_directory_bytes_reserved = entries.lookup_directory_bytes_reserved,
        .descriptor_segment_directory_bytes_reserved = entries.segment_directory_bytes_reserved,
    };
    if constexpr (requires(const Index& index) {
                    { index.structural_diagnostics() } noexcept;
                  }) {
      const auto index = index_.structural_diagnostics();
      result.index_nodes = index.nodes;
      result.index_entries = index.entries;
      result.index_collision_nodes = index.collision_nodes;
      result.index_collision_entries = index.collision_entries;
      result.index_largest_collision = index.largest_collision;
      result.index_maximum_depth = index.maximum_depth;
      result.index_node_bytes = index.node_allocation_bytes;
      result.index_entry_bytes = index.entry_allocation_bytes;
    }
    return result;
  }

  // Cold-path diagnostics for every storage domain reachable through this
  // interner. `visible_strings` is restricted by the captured parent cutoff;
  // `storage` describes the owner's complete current local allocation domain,
  // which may also contain strings added after that cutoff.
  template<typename Visitor>
  requires(std::is_nothrow_invocable_r_v<void, Visitor&, size_type, size_type, const StorageDiagnostics&>)
  void visit_storage_diagnostics(Visitor&& visitor) const noexcept {
    auto&& callback = std::forward<Visitor>(visitor);
    const auto* owner = this;
    size_type visible_end = size();
    size_type depth = 0;
    while (owner != nullptr) {
      const auto storage = owner->local_storage_diagnostics();
      std::invoke(callback, depth, visible_end - owner->first_local_id_, storage);
      visible_end = owner->first_local_id_;
      owner = owner->parent_;
      ++depth;
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

  // Explicitly instrumented lookup; ordinary find/rfind have no trace counters.
  LookupTrace trace_find(std::string_view key) const noexcept {
    LookupTrace trace;
    const auto search = [&](auto&& self, const StringInterner* owner, size_type limit,
                            size_type depth) noexcept -> bool {
      if (owner->parent_ != nullptr && self(self, owner->parent_, std::min(limit, owner->first_local_id_), depth + 1)) {
        return true;
      }
      ++trace.index_queries;
      trace.id = owner->FindLocal(key, limit);
      if (trace.id) {
        trace.parent_depth = depth;
        return true;
      }
      return false;
    };
    search(search, this, size(), 0);
    return trace;
  }

  LookupTrace trace_rfind(std::string_view key) const noexcept {
    LookupTrace trace;
    const auto* owner = this;
    auto limit = size();
    size_type depth = 0;
    while (owner != nullptr) {
      ++trace.index_queries;
      trace.id = owner->FindLocal(key, limit);
      if (trace.id) {
        trace.parent_depth = depth;
        return trace;
      }
      limit = std::min(limit, owner->first_local_id_);
      owner = owner->parent_;
      ++depth;
    }
    return trace;
  }

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

  [[nodiscard]] insertion_result intern_parent_first(std::string_view key) noexcept(!::mbo::config::kRequireThrows) {
    return Insert(key, find(key));
  }

  [[nodiscard]] insertion_result intern_child_first(std::string_view key) noexcept(!::mbo::config::kRequireThrows) {
    return Insert(key, rfind(key));
  }

  [[nodiscard]] insertion_result intern(std::string_view key) noexcept(!::mbo::config::kRequireThrows) {
    if constexpr (Options.parent_first) {
      return intern_parent_first(key);
    } else {
      return intern_child_first(key);
    }
  }

  // Convenience adapters deliberately discard the detailed exhaustion reason.
  [[nodiscard]] std::optional<std::pair<id_type, bool>> try_intern(std::string_view key) noexcept(
      !::mbo::config::kRequireThrows) {
    const auto result = intern(key);
    const auto* const inserted = std::get_if<std::pair<id_type, bool>>(&result);
    return inserted == nullptr ? std::nullopt : std::optional(*inserted);
  }

  [[nodiscard]] std::optional<id_type> try_intern_id(std::string_view key) noexcept(!::mbo::config::kRequireThrows) {
    const auto result = try_intern(key);
    return result ? std::optional(result->first) : std::nullopt;
  }

 private:
  static const StringInterner* ValidateParent(const StringInterner* parent) noexcept {
    if constexpr (Options.maximum_parent_depth != std::numeric_limits<size_type>::max()) {
      size_type depth = 0;
      for (const auto* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent_) {
        ++depth;
      }
      ABSL_LOG_IF(FATAL, depth > Options.maximum_parent_depth)
          << "StringInterner parent chain exceeds maximum_parent_depth=" << Options.maximum_parent_depth;
    }
    return parent;
  }

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

  insertion_result Insert(std::string_view key, std::optional<id_type> existing) noexcept(
      !::mbo::config::kRequireThrows) {
    if (existing) {
      return std::pair<id_type, bool>(*existing, false);
    }
    const auto ordinal = size();
    if (ordinal >= max_size()) {
      return StringInternError::kIdExhausted;
    }
    auto identifier = id_type::try_from_ordinal(ordinal);
    MBO_CONFIG_REQUIRE_DEBUG(identifier.has_value(), "StringInterner max_size does not fit its ID representation");
    const auto checkpoint = storage_.checkpoint();
    const auto stored = storage_.try_store(key);
    if (!stored) {
      storage_.rewind(checkpoint);
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
