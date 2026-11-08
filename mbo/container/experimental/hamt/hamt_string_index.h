// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INDEX_H_
#define MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INDEX_H_

#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mbo/container/experimental/hamt/hamt_flat_map.h"
#include "mbo/strings/string_id.h"

namespace mbo::container::experimental::hamt {

// Container adapter for the generic StringInterner. Keys borrow character storage.
// Keep one transient tree: appending to an interner does not need a persistent
// snapshot of the previous index on every insertion. Iterators expose const keys
// and IDs, so reads never request mutable ownership or detach shared nodes.
// NOLINTBEGIN(readability-identifier-naming): container vocabulary.
template<
    typename Id = mbo::strings::StringIdU32<>,
    typename Hash = std::hash<std::string_view>,
    typename Equal = std::equal_to<>,
    HamtOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
    typename MapBackend = HamtFlatMap<std::string_view, Id, Hash, Equal, Options, Source>>
class HamtStringIndex final {
  using Map = MapBackend;

 public:
  using key_type = std::string_view;
  using mapped_type = Id;
  using value_type = Map::value_type;
  using size_type = Map::size_type;
  using iterator = Map::const_iterator;
  using const_iterator = iterator;
  using insertion_result = std::expected<std::pair<iterator, bool>, HamtError>;

  HamtStringIndex() = default;

  explicit HamtStringIndex(Hash hash, Equal equal = Equal{}) noexcept
  requires std::is_nothrow_default_constructible_v<Source>
      : map_(Map(std::move(hash), std::move(equal)).transient()) {}

  HamtStringIndex(const HamtStringIndex&) = delete;
  HamtStringIndex& operator=(const HamtStringIndex&) = delete;
  HamtStringIndex(HamtStringIndex&&) noexcept = default;
  HamtStringIndex& operator=(HamtStringIndex&&) noexcept = default;
  ~HamtStringIndex() = default;

  template<typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtStringIndex> try_create(
      Hash hash,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto map = Map::try_create(std::move(hash), std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!map) {
      return std::nullopt;
    }
    return HamtStringIndex(std::move(*map));
  }

  template<mbo::memory::BlockSource ControlSource, typename... SourceArgs>
  requires std::is_nothrow_constructible_v<Source, SourceArgs...>
  [[nodiscard]] static std::optional<HamtStringIndex> try_create_in(
      ControlSource& storage,
      Hash hash,
      Equal equal,
      SourceArgs&&... source_args) noexcept {
    auto map = Map::try_create_in(storage, std::move(hash), std::move(equal), std::forward<SourceArgs>(source_args)...);
    if (!map) {
      return std::nullopt;
    }
    return HamtStringIndex(std::move(*map));
  }

  size_type size() const noexcept { return map_.size(); }

  bool empty() const noexcept { return map_.empty(); }

  const_iterator begin() const noexcept { return map_.begin(); }

  const_iterator end() const noexcept { return map_.end(); }

  const_iterator find(std::string_view key) const noexcept { return map_.find(key); }

  [[nodiscard]] insertion_result try_emplace(std::string_view key, Id identifier) noexcept {
    if (transaction_active_) {
      return TryEmplaceTransactional(key, identifier);
    }
    auto result = map_.try_insert(value_type(key, identifier));
    if (const auto* error = std::get_if<HamtError>(&result)) {
      return std::unexpected(*error);
    }
    const auto& inserted = std::get<std::pair<typename Map::transient_type::iterator, bool>>(result);
    return std::pair<iterator, bool>(inserted.first, inserted.second);
  }

  std::pair<iterator, bool> emplace(std::string_view key, Id identifier) noexcept {
    auto result = try_emplace(key, identifier);
    if (!result) {
      std::terminate();
    }
    return *result;
  }

  void erase(iterator position) noexcept {
    // Ordinary erasure retains the source's allocation-failure behavior.
    // Exception-safe publication uses InsertionTransaction instead.
    const auto key = position->first;
    map_.erase(key);
  }

  auto structural_diagnostics() const noexcept { return map_.structural_diagnostics(); }

  template<typename Visitor>
  requires requires(const Map::transient_type& map, Visitor& visitor) { map.visit_node_diagnostics(visitor); }
  void visit_node_diagnostics(Visitor&& visitor) const noexcept {
    map_.visit_node_diagnostics(std::forward<Visitor>(visitor));
  }

  // A publication guard retains the old root, not a copy of every entry. While
  // active, insertion path-copies through the persistent API so preparing a
  // mutable iterator cannot clone the entire tree. Restore only ownership on
  // failure: rollback must not allocate from an already exhausted source.
  class InsertionTransaction final {
   public:
    InsertionTransaction(const InsertionTransaction&) = delete;
    InsertionTransaction& operator=(const InsertionTransaction&) = delete;
    InsertionTransaction(InsertionTransaction&&) = delete;
    InsertionTransaction& operator=(InsertionTransaction&&) = delete;

    ~InsertionTransaction() {
      if (index_ != nullptr) {
        index_->map_ = std::move(previous_).transient();
        index_->transaction_active_ = false;
      }
    }

    void Commit() noexcept {
      index_->transaction_active_ = false;
      index_ = nullptr;
    }

   private:
    friend class HamtStringIndex;

    explicit InsertionTransaction(HamtStringIndex& index) noexcept
        : index_(std::addressof(index)), previous_(std::move(index.map_).persistent()) {
      index.map_ = previous_.transient();
      index.transaction_active_ = true;
    }

    HamtStringIndex* index_;
    Map previous_;
  };

  // Policy hook: the index must not be moved or reentered while this guard lives.
  [[nodiscard]] InsertionTransaction BeginInsertion() noexcept { return InsertionTransaction(*this); }

 private:
  explicit HamtStringIndex(Map&& map) noexcept : map_(std::move(map).transient()) {}

  insertion_result TryEmplaceTransactional(std::string_view key, Id identifier) noexcept {
    auto snapshot = std::move(map_).persistent();
    auto result = snapshot.try_insert(value_type(key, identifier));
    if (const auto* error = std::get_if<HamtError>(&result)) {
      map_ = std::move(snapshot).transient();
      return std::unexpected(*error);
    }
    auto& inserted = std::get<std::pair<Map, bool>>(result);
    map_ = std::move(inserted.first).transient();
    return std::pair<iterator, bool>(std::as_const(map_).find(key), inserted.second);
  }

  Map::transient_type map_{Map{}};
  bool transaction_active_ = false;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::experimental::hamt

#endif  // MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_STRING_INDEX_H_
