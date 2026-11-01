// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_INTERNAL_STRING_INTERNER_BASE_H_
#define MBO_STRINGS_INTERNAL_STRING_INTERNER_BASE_H_

#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/hash_container_defaults.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/string_id.h"
#include "mbo/strings/string_interner_types.h"
#include "mbo/types/optional_ref.h"

namespace mbo::strings::strings_internal {

// Element storage is independent of the string index. Views are inexpensive values;
// owning keys are referenced in their address-stable string container.
template<typename Element>
struct StringInternerElementTraits;

template<typename Key>
struct StringInternerElementTraits<const Key> {
  using KeyType = Key;
  using Entry = std::conditional_t<std::same_as<Key, std::string_view>, Key, const Key*>;

  template<typename Slot>
  requires std::same_as<std::remove_const_t<Slot>, Entry>
  static const Key& GetElement(Slot& entry) noexcept {
    if constexpr (std::same_as<Key, std::string_view>) {
      // Rvalue entries are rejected below; this reference points into the dense entry sequence.
      // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
      return entry;
    } else {
      return *entry;
    }
  }

  static const Key& GetElement(const Entry&&) = delete;

  template<typename StoredKey>
  requires std::same_as<std::remove_const_t<StoredKey>, Key>
  static const Key& GetKey(StoredKey& key) noexcept {
    // Temporary keys, including const temporaries, are rejected by the rvalue overload below.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return key;
  }

  static const Key& GetKey(const Key&&) = delete;

  static void Publish(Entry& entry, const Key& key) noexcept {
    if constexpr (std::same_as<Key, std::string_view>) {
      entry = key;
    } else {
      entry = std::addressof(key);
    }
  }
};

template<typename Key, typename Value>
requires(!std::is_lvalue_reference_v<Key> || std::is_const_v<std::remove_reference_t<Key>>)
struct StringInternerElementTraits<std::pair<Key, Value>> {
  using KeyType = std::remove_cvref_t<Key>;
  // Keys are immutable. For reference keys, the const qualifier is preserved by
  // reference collapsing; for value keys, it prevents mutation through entries.
  using Element = std::pair<const Key, Value>;
  using Entry = std::optional<Element>;

  template<typename Slot>
  static decltype(auto) GetElement(Slot& entry) noexcept {
    if (entry) {
      return *entry;
    }
    std::unreachable();  // Only published entries are exposed.
  }

  static const KeyType& GetKey(const Element& element) noexcept { return element.first; }

  template<typename... Args>
  static void Publish(Entry& entry, const KeyType& key, Args&&... args) {
    entry.emplace(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(std::forward<Args>(args)...));
  }
};

template<typename Container, typename Value>
using StringInternerMapElement = std::pair<
    std::conditional_t<
        std::same_as<StringInternerKey<Container>, std::string>,
        const std::string&,
        const std::string_view>,
    Value>;

template<typename Element>
using StringInternerEntriesFor = mbo::container::SegmentedVector<typename StringInternerElementTraits<Element>::Entry>;

// StringInternerBase:
// - implements an append-only owner for strings that are referenced by IDs.
// - they may refer to parents holding a parent set of IDs (every ancestor must outlive children).
// - intentionally exposes a minimal API surface to avoid accidental misuse.
// - is not a general-purpose string container.
// - the `Id` must be an integral type such as `std::uint32_t` or a `ConstStrongId`.
// - a string_view-keyed container requires separate `Storage`.
// - a string-keyed container requires `Storage = void` and stable key addresses across insertion.
//
// NOLINTBEGIN(readability-identifier-naming): StringInternerBase follows STL container vocabulary.
template<
    StringInternerId Id = StringIdU32<>,
    typename Element = const std::string_view,
    typename Hash = absl::DefaultHashContainerHash<std::string_view>,
    typename StringContainer = absl::flat_hash_map<std::string_view, Id, Hash>,
    typename Storage =
        std::conditional_t<std::same_as<StringInternerKey<StringContainer>, std::string>, void, ArenaStringStorage<>>,
    typename Entries = StringInternerEntriesFor<Element>,
    typename Policy = StringInternerPolicyHolder<>>
requires(
    StringInternerIdIndex<StringContainer, Id>
    && std::same_as<typename StringInternerElementTraits<Element>::KeyType, StringInternerKey<StringContainer>>
    && StringInternerEntries<Entries, typename StringInternerElementTraits<Element>::Entry>
    && ((std::same_as<Storage, void> && std::same_as<StringInternerKey<StringContainer>, std::string>
         && kHasStableKeyAddresses<StringContainer>)
        || (StringInternerStorage<Storage> && std::same_as<StringInternerKey<StringContainer>, std::string_view>)))
class StringInternerBase {
  using ContainerTraits = StringInternerContainerTraits<StringContainer>;
  using ElementTraits = StringInternerElementTraits<Element>;
 public:
  using id_type = Id;
  using key_type = ElementTraits::KeyType;
  using value_type = std::remove_const_t<Element>;
  using element_type = Element;
  using reference = element_type&;
  using const_reference = const element_type&;
  using size_type = std::size_t;  // MUST be unsigned.
  using insertion_result = typename Policy::template insertion_result<id_type>;
  using lookup_result = typename Policy::template lookup_result<element_type>;
  using const_lookup_result = typename Policy::template lookup_result<const element_type>;
  using policy = Policy;

 private:
  template<bool IsConst>
  class Iterator final {
    using Owner = std::conditional_t<IsConst, const StringInternerBase, StringInternerBase>;

    template<bool>
    friend class Iterator;

   public:
    using value_type = StringInternerBase::value_type;
    using reference = std::conditional_t<IsConst, StringInternerBase::const_reference, StringInternerBase::reference>;
    using pointer = std::add_pointer_t<std::remove_reference_t<reference>>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;

    Iterator() noexcept = default;

    template<bool OtherConst>
    requires(IsConst && !OtherConst)
    // NOLINTNEXTLINE(google-explicit-constructor): Mutable iterators convert to const iterators.
    Iterator(const Iterator<OtherConst>& other) noexcept : owner_(other.owner_), position_(other.position_) {}

    reference operator*() const noexcept { return ElementAt(*owner_, position_); }

    pointer operator->() const noexcept { return std::addressof(operator*()); }

    // The position already is the ID; no string lookup is involved.
    id_type id() const noexcept { return IdFromOrdinal(position_).value_or(id_type{}); }

    Iterator& operator++() noexcept {
      ++position_;
      return *this;
    }

    Iterator operator++(int) noexcept {
      auto before = *this;
      ++*this;
      return before;
    }

    Iterator& operator--() noexcept {
      --position_;
      return *this;
    }

    Iterator operator--(int) noexcept {
      auto before = *this;
      --*this;
      return before;
    }

    template<bool OtherConst>
    bool operator==(const Iterator<OtherConst>& other) const noexcept {
      return owner_ == other.owner_ && position_ == other.position_;
    }

   private:
    friend class StringInternerBase;

    Iterator(Owner* owner, size_type position) noexcept : owner_(owner), position_(position) {}

    Owner* owner_ = nullptr;
    size_type position_ = 0;
  };

 public:
  using iterator = Iterator<std::is_const_v<element_type>>;
  using const_iterator = Iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() noexcept { return iterator(this, 0); }

  const_iterator begin() const noexcept { return const_iterator(this, 0); }

  iterator end() noexcept { return iterator(this, size()); }

  const_iterator end() const noexcept { return const_iterator(this, size()); }

  const_iterator cbegin() const noexcept { return begin(); }

  const_iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  const_reverse_iterator crend() const noexcept { return rend(); }

  StringInternerBase() = default;

  // The forwarded arguments must construct an empty string container.
  template<typename... Args>
  requires std::constructible_from<StringContainer, Args...>
  explicit StringInternerBase(const StringInternerBase* parent, Args&&... args)
      : parent_(parent),
        first_local_id_(parent == nullptr ? 0 : parent->size()),
        strings_(std::forward<Args>(args)...) {}

  StringInternerBase(const StringInternerBase&) = delete;
  StringInternerBase& operator=(const StringInternerBase&) = delete;
  StringInternerBase(StringInternerBase&&) = delete;
  StringInternerBase& operator=(StringInternerBase&&) = delete;
  ~StringInternerBase() = default;

  size_type size() const noexcept { return first_local_id_ + local_size_; }

  size_type local_size() const noexcept { return local_size_; }

  bool empty() const noexcept { return size() == 0; }

  const StringInternerBase* parent() const noexcept { return parent_; }

  size_type first_local_id() const noexcept { return first_local_id_; }

  lookup_result get(id_type identifier) noexcept
  requires(!std::is_const_v<element_type>) {
    return GetById(*this, identifier);
  }

  const_lookup_result get(id_type identifier) const noexcept { return GetById(*this, identifier); }

  std::optional<std::string_view> get_string(id_type identifier) const noexcept {
    auto entry = get(identifier);
    if (entry) {
      return std::string_view(ElementTraits::GetKey(Policy::Get(entry)));
    }
    return std::nullopt;
  }

  std::optional<id_type> find(std::string_view key) const { return FindForward(key, size()); }

  std::optional<id_type> rfind(std::string_view key) const {
    const auto* owner = this;
    auto limit = size();
    while (owner != nullptr) {
      if (auto found = owner->FindLocal(key, limit)) {
        return found;
      }
      limit = owner->first_local_id_;
      owner = owner->parent_;
    }
    return std::nullopt;
  }

  // Reuse a visible ID, or publish a new entry in this interner only.
  // The result itself never allocates, including on exhaustion. Storage and containers may allocate.
  [[nodiscard]] insertion_result insert(std::string_view key)
  requires std::is_const_v<element_type> {
    return Insert(key);
  }

 protected:
  template<typename... Args>
  [[nodiscard]] insertion_result Insert(std::string_view key, Args&&... args) {
    if (const auto existing = rfind(key)) {
      return Policy::Success(*existing, false);
    }
    const auto identifier = IdFromOrdinal(size());
    if (!identifier) {
      return Policy::template Failure<id_type>(StringInternerError::kIdExhausted);
    }
    // Keep at most one unpublished slot and reuse it after a failed attempt.
    if (entries_.size() == local_size_ && !TryAppendEntry()) {
      return Policy::template Failure<id_type>(StringInternerError::kEntryStorageExhausted);
    }
    auto stored_key = key;
    if constexpr (!std::same_as<Storage, void>) {
      const auto stored = storage_.try_store(key);
      if (!stored) {
        return Policy::template Failure<id_type>(StringInternerError::kCharacterStorageExhausted);
      }
      stored_key = *stored;
    }
    const auto [position, inserted] = [&] {
      if constexpr (ContainerTraits::kIsMap) {
        return strings_.emplace(StringInternerKey<StringContainer>(stored_key), *identifier);
      } else {
        return strings_.emplace(StringInternerKey<StringContainer>(stored_key));
      }
    }();
    if (!inserted) {
      return Policy::template Failure<id_type>(StringInternerError::kIndexExhausted);
    }
    // If a caller's value constructor throws, discard only the unpublished index entry.
    // Character storage and the reserved dense slot remain append-only.
    absl::Cleanup erase_unpublished = [&] { strings_.erase(position); };
    ElementTraits::Publish(entries_.back(), ContainerTraits::GetKey(*position), std::forward<Args>(args)...);
    std::move(erase_unpublished).Cancel();
    ++local_size_;
    return Policy::Success(*identifier, true);
  }

 private:
  template<typename Self>
  static auto GetById(Self& self, id_type identifier) noexcept
      -> std::conditional_t<std::is_const_v<Self>, const_lookup_result, lookup_result> {
    const auto ordinal = IdOrdinal(identifier);
    if (!ordinal || *ordinal >= self.size()) {
      return std::nullopt;
    }
    return Policy::Make(ElementAt(self, *ordinal));
  }

  template<typename Self>
  static decltype(auto) ElementAt(Self& self, size_type position) noexcept {
    using Owner = std::conditional_t<std::is_const_v<Self>, const StringInternerBase, StringInternerBase>;
    auto* owner = static_cast<Owner*>(&self);
    while (position < owner->first_local_id_) {
      // The parent link is const to prevent changing the parent object through the
      // public API. Mutable views still share mapped values with mutable ancestors.
      owner = const_cast<Owner*>(owner->parent_);
    }
    auto& entry = *std::ranges::next(
        owner->entries_.begin(),
        static_cast<std::ranges::range_difference_t<Entries>>(position - owner->first_local_id_));
    return ElementTraits::GetElement(entry);
  }

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

  [[nodiscard]] bool TryAppendEntry() {
    if constexpr (requires { entries_.try_emplace_back(); }) {
      return entries_.try_emplace_back().has_value();
    } else {
      entries_.emplace_back();
      return true;
    }
  }

  std::optional<id_type> FindLocal(std::string_view key, size_type limit) const {
    const auto found = strings_.find(key);
    if (found == strings_.end()) {
      return std::nullopt;
    }
    if constexpr (ContainerTraits::kIsMap) {
      const auto identifier = found->second;
      const auto ordinal = IdOrdinal(identifier);
      if (ordinal && *ordinal < limit) {
        return identifier;
      }
    } else {
      const std::string_view stored_key = ContainerTraits::GetKey(*found);
      auto ordinal = first_local_id_;
      for (const auto& entry : entries_) {
        if (ordinal >= limit) {
          break;
        }
        if (ElementTraits::GetKey(ElementTraits::GetElement(entry)) == stored_key) {
          return IdFromOrdinal(ordinal);
        }
        ++ordinal;
      }
    }
    return std::nullopt;
  }

  std::optional<id_type> FindForward(std::string_view key, size_type limit) const {
    if (parent_ != nullptr) {
      if (auto found = parent_->FindForward(key, first_local_id_)) {
        return found;
      }
    }
    return FindLocal(key, limit);
  }

  const StringInternerBase* const parent_ = nullptr;
  const size_type first_local_id_ = 0;
  size_type local_size_ = 0;
  [[no_unique_address]] std::conditional_t<std::same_as<Storage, void>, std::monostate, Storage> storage_;
  StringContainer strings_;
  Entries entries_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings::strings_internal

#endif  // MBO_STRINGS_INTERNAL_STRING_INTERNER_BASE_H_
