// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_MAP_H_
#define MBO_STRINGS_STRING_INTERNER_MAP_H_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mbo/container/segmented_sequence.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {

template<typename Values, typename Mapped>
concept StringInternerMappedEntries =
    std::is_nothrow_destructible_v<Values> && requires(Values& values, const Values& read, std::size_t ordinal) {
      requires std::same_as<typename Values::value_type, Mapped>;
      { read.size() } noexcept -> std::same_as<std::size_t>;
      { read.at(ordinal) } -> std::convertible_to<const Mapped&>;
      { values.pop_back() } -> std::same_as<void>;
    };

// StringInterner map semantics without duplicating string ownership or lookup.
// Mapped objects occupy a parallel stable-address dense sequence. Parent values
// are immutable through children and obey the same captured cutoff as keys.
template<typename Mapped, typename Core = StringInterner<>, typename Values = mbo::container::SegmentedSequence<Mapped>>
requires StringInternerMappedEntries<Values, Mapped>
// NOLINTBEGIN(readability-identifier-naming): StringInternerMap follows STL container vocabulary.
class StringInternerMap final {
 public:
  using mapped_type = Mapped;
  using core_type = Core;
  using id_type = Core::id_type;
  using size_type = Core::size_type;
  using insertion_result = Core::insertion_result;

  struct entry_reference final {
    std::string_view key;
    const Mapped& mapped;
  };

  struct MappedStorageDiagnostics final {
    size_type objects = 0;
    size_type live_object_bytes = 0;
    std::optional<size_type> segment_bytes_reserved;
    std::optional<size_type> lookup_directory_bytes_reserved;
    std::optional<size_type> segment_directory_bytes_reserved;
  };

  struct StorageDiagnostics final {
    Core::StorageDiagnostics interner;
    MappedStorageDiagnostics mapped;
  };

  class iterator final {
   public:
    using value_type = entry_reference;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;

    iterator() noexcept = default;

    reference operator*() const noexcept(!::mbo::config::kRequireThrows) {
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot dereference a singular StringInternerMap iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ < owner_->size(), "Cannot dereference a StringInternerMap end iterator");
      const auto identifier = id_type::try_from_ordinal(position_).value_or(id_type{});
      const auto key = current_owner_->core_.get(identifier).value_or(std::string_view{});
      return {.key = key, .mapped = current_owner_->values_.at(position_ - current_owner_->first_local_id())};
    }

    iterator& operator++() noexcept(!::mbo::config::kRequireThrows) {
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot increment a singular StringInternerMap iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ < owner_->size(), "Cannot increment a StringInternerMap end iterator");
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
      MBO_CONFIG_REQUIRE_DEBUG(owner_ != nullptr, "Cannot decrement a singular StringInternerMap iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ > 0, "Cannot decrement a StringInternerMap begin iterator");
      MBO_CONFIG_REQUIRE_DEBUG(position_ <= owner_->size(), "StringInternerMap iterator is out of range");
      --position_;
      if (position_ < current_owner_->first_local_id()) {
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
    friend class StringInternerMap;

    iterator(const StringInternerMap* owner, size_type position) noexcept : owner_(owner), position_(position) {
      Resolve();
    }

    void Resolve() noexcept {
      current_owner_ = owner_;
      current_limit_ = owner_->size();
      while (position_ < current_owner_->first_local_id()) {
        current_limit_ = std::min(current_limit_, current_owner_->first_local_id());
        current_owner_ = current_owner_->parent_;
      }
    }

    const StringInternerMap* owner_ = nullptr;
    const StringInternerMap* current_owner_ = nullptr;
    size_type position_ = 0;
    size_type current_limit_ = 0;
  };

  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;

  StringInternerMap() noexcept(
      std::is_nothrow_default_constructible_v<Core> && std::is_nothrow_default_constructible_v<Values>)
  requires(std::default_initializable<Core> && std::default_initializable<Values>)
  = default;

  explicit StringInternerMap(const StringInternerMap* parent) noexcept
  requires(std::is_nothrow_constructible_v<Core, const Core*> && std::is_nothrow_default_constructible_v<Values>)
      : parent_(parent), core_(parent == nullptr ? nullptr : std::addressof(parent->core_)) {}

  template<typename CoreFactory, typename ValuesFactory>
  requires(std::is_nothrow_invocable_v<CoreFactory&, const Core*>
           && std::same_as<std::invoke_result_t<CoreFactory&, const Core*>, Core>
           && std::is_nothrow_invocable_v<ValuesFactory&> && std::same_as<std::invoke_result_t<ValuesFactory&>, Values>)
  StringInternerMap(const StringInternerMap* parent, CoreFactory core, ValuesFactory values) noexcept
      : parent_(parent),
        core_(std::invoke(core, parent == nullptr ? nullptr : std::addressof(parent->core_))),
        values_(std::invoke(values)) {}

  StringInternerMap(const StringInternerMap&) = delete;
  StringInternerMap& operator=(const StringInternerMap&) = delete;
  StringInternerMap(StringInternerMap&&) = delete;
  StringInternerMap& operator=(StringInternerMap&&) = delete;
  ~StringInternerMap() = default;

  size_type size() const noexcept { return core_.size(); }

  size_type local_size() const noexcept { return core_.local_size(); }

  bool empty() const noexcept { return core_.empty(); }

  const Core& string_interner() const noexcept { return core_; }

  MappedStorageDiagnostics local_mapped_storage_diagnostics() const noexcept {
    MappedStorageDiagnostics result{.objects = local_size(), .live_object_bytes = local_size() * sizeof(Mapped)};
    if constexpr (requires(const Values& values) {
                    { values.bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.segment_bytes_reserved = values_.bytes_reserved();
    }
    if constexpr (requires(const Values& values) {
                    { values.directory_bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.lookup_directory_bytes_reserved = values_.directory_bytes_reserved();
    }
    if constexpr (requires(const Values& values) {
                    { values.segment_directory_bytes_reserved() } noexcept -> std::same_as<size_type>;
                  }) {
      result.segment_directory_bytes_reserved = values_.segment_directory_bytes_reserved();
    }
    return result;
  }

  StorageDiagnostics local_storage_diagnostics() const noexcept {
    return {.interner = core_.local_storage_diagnostics(), .mapped = local_mapped_storage_diagnostics()};
  }

  iterator begin() const noexcept { return iterator(this, 0); }

  iterator end() const noexcept { return iterator(this, size()); }

  iterator cbegin() const noexcept { return begin(); }

  iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }

  reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

  const StringInternerMap* parent() const noexcept { return parent_; }

  size_type first_local_id() const noexcept { return core_.first_local_id(); }

  std::optional<id_type> find(std::string_view key) const noexcept { return core_.find(key); }

  std::optional<id_type> rfind(std::string_view key) const noexcept { return core_.rfind(key); }

  std::optional<std::string_view> key(id_type identifier) const noexcept { return core_.get(identifier); }

  const Mapped* mapped(id_type identifier) const noexcept {
    if (identifier.value() >= size()) {
      return nullptr;
    }
    const auto* owner = this;
    while (identifier.value() < owner->first_local_id()) {
      owner = owner->parent_;
    }
    return std::addressof(owner->values_.at(identifier.value() - owner->first_local_id()));
  }

  std::optional<entry_reference> get(id_type identifier) const noexcept {
    const auto found_key = key(identifier);
    const auto* const found_mapped = mapped(identifier);
    if (!found_key || found_mapped == nullptr) {
      return std::nullopt;
    }
    return entry_reference{.key = *found_key, .mapped = *found_mapped};
  }

  template<typename... Args>
  requires(
      std::is_nothrow_constructible_v<Mapped, Args...>
      && requires(Values& values, Args&&... args) {
           { values.try_emplace_back(std::forward<Args>(args)...) };
         })
  [[nodiscard]] insertion_result try_emplace(std::string_view key, Args&&... args) noexcept(
      noexcept(std::declval<Core&>().intern(std::declval<std::string_view>()))) {
    if (const auto existing = core_.find(key)) {
      return std::pair<id_type, bool>(*existing, false);
    }
    const auto appended = values_.try_emplace_back(std::forward<Args>(args)...);
    if (!appended) {
      return StringInternError::kMappedStorageExhausted;
    }
    auto interned = core_.intern(key);
    const auto* const inserted = std::get_if<std::pair<id_type, bool>>(&interned);
    if (inserted == nullptr || !inserted->second) {
      values_.pop_back();
    }
    return interned;
  }

 private:
  const StringInternerMap* parent_ = nullptr;
  Core core_;
  Values values_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_MAP_H_
