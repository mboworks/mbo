// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_TYPES_H_
#define MBO_STRINGS_STRING_INTERNER_TYPES_H_

#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mbo/strings/string_id.h"
#include "mbo/types/optional_ref.h"

namespace mbo::strings {

enum class StringInternerError { kIdExhausted, kCharacterStorageExhausted, kEntryStorageExhausted, kIndexExhausted };

[[nodiscard]] constexpr absl::StatusCode ToStatusCode(StringInternerError error) noexcept {
  switch (error) {
    case StringInternerError::kCharacterStorageExhausted:
    case StringInternerError::kEntryStorageExhausted: return absl::StatusCode::kResourceExhausted;
    case StringInternerError::kIdExhausted: return absl::StatusCode::kOutOfRange;
    case StringInternerError::kIndexExhausted: return absl::StatusCode::kResourceExhausted;
  }
  std::unreachable();
}

// Explicit conversion for status-based callers. Error messages remain empty.
template<typename Id>
[[nodiscard]] absl::StatusOr<std::pair<Id, bool>> ToStatusOr(
    std::expected<std::pair<Id, bool>, StringInternerError> result) {
  if (result) {
    return std::move(*result);
  }
  return absl::Status(ToStatusCode(result.error()), {});
}

template<typename Id>
concept StringInternerId = IsStringIdRepresentation<Id> || mbo::types::IsConstStrongId<Id>;

struct StringInternerErrorStlPolicy {
  template<typename Id>
  using insertion_result = std::expected<std::pair<Id, bool>, StringInternerError>;

  template<typename Id>
  static insertion_result<Id> Success(Id identifier, bool inserted) {
    return std::pair<Id, bool>(identifier, inserted);
  }

  template<typename Id>
  static insertion_result<Id> Failure(StringInternerError error) {
    return std::unexpected(error);
  }

};

struct StringInternerErrorStatusPolicy {
  template<typename Id>
  using insertion_result = absl::StatusOr<std::pair<Id, bool>>;

  template<typename Id>
  static insertion_result<Id> Success(Id identifier, bool inserted) {
    return std::pair<Id, bool>(identifier, inserted);
  }

  template<typename Id>
  static insertion_result<Id> Failure(StringInternerError error) {
    return absl::Status(ToStatusCode(error), {});
  }
};

// Optional insertion results are useful when callers only distinguish success from failure.
// Unlike the status and expected policies, this policy intentionally discards the specific error.
struct StringInternerErrorOptionalPolicy {
  template<typename Id>
  using insertion_result = std::optional<std::pair<Id, bool>>;

  template<typename Id>
  static insertion_result<Id> Success(Id identifier, bool inserted) {
    return std::pair<Id, bool>(identifier, inserted);
  }

  template<typename Id>
  static insertion_result<Id> Failure(StringInternerError /*error*/) {
    return std::nullopt;
  }
};

struct StringInternerLookupMboPolicy {
  template<typename T>
  using result = mbo::types::OptionalRef<T>;

  template<typename T>
  static result<T> Make(T& value) {
    return value;
  }

  template<typename T>
  static T& Get(result<T>& value) {
    return value.value();
  }
};

struct StringInternerLookupStlPolicy {
  template<typename T>
  using result = std::optional<std::reference_wrapper<T>>;

  template<typename T>
  static result<T> Make(T& value) {
    return std::ref(value);
  }

  template<typename T>
  static T& Get(result<T>& value) {
    return value->get();
  }
};

template<
    typename ErrorPolicy = StringInternerErrorStlPolicy,
    typename LookupPolicy = StringInternerLookupMboPolicy>
struct StringInternerPolicyHolder : ErrorPolicy, LookupPolicy {
  using ErrorPolicy::Failure;
  using ErrorPolicy::Success;
  using LookupPolicy::Get;
  using LookupPolicy::Make;

  template<typename Id>
  using insertion_result = typename ErrorPolicy::template insertion_result<Id>;

  template<typename T>
  using lookup_result = typename LookupPolicy::template result<T>;
};

// Whether insertion preserves the addresses of existing key objects, including short strings.
// Unknown containers default to false. Custom containers may specialize this trait to true only
// when every insertion preserves this guarantee; declare the specialization before interner use.
template<typename Container>
inline constexpr bool kHasStableKeyAddresses = false;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::map<Args...>> = true;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::set<Args...>> = true;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::unordered_map<Args...>> = true;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<std::unordered_set<Args...>> = true;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<absl::node_hash_map<Args...>> = true;

template<typename... Args>
inline constexpr bool kHasStableKeyAddresses<absl::node_hash_set<Args...>> = true;

template<typename Container>
struct StringInternerContainerTraits {
  using KeyType = std::remove_const_t<typename Container::key_type>;
  static constexpr bool kIsMap = requires { typename Container::mapped_type; };

  template<typename Entry>
  static constexpr decltype(auto) GetKey(Entry&& entry) noexcept {
    if constexpr (kIsMap) {
      return (std::forward<Entry>(entry).first);
    } else {
      return std::forward<Entry>(entry);
    }
  }
};

template<typename Container>
using StringInternerKey = StringInternerContainerTraits<Container>::KeyType;

// Key validation is independent of the mapped value.
template<typename Container>
concept StringInternerStringContainer = requires(const Container& read, std::string_view key) {
  typename Container::key_type;
  requires(
      std::same_as<StringInternerKey<Container>, std::string_view>
      || std::same_as<StringInternerKey<Container>, std::string>);
  requires std::is_const_v<std::remove_reference_t<decltype(StringInternerContainerTraits<Container>::GetKey(
      *std::declval<typename Container::iterator>()))>>;
  { read.find(key) } -> std::same_as<typename Container::const_iterator>;
  { read.end() } -> std::same_as<typename Container::const_iterator>;
};

template<typename Container, typename Id>
concept StringInternerIdIndex =
    StringInternerStringContainer<Container>
    && requires(Container& container, Container::iterator position) { container.erase(position); }
    && ((StringInternerContainerTraits<Container>::kIsMap && requires(Container& container, std::string_view key, Id identifier) {
          requires std::same_as<typename Container::mapped_type, Id>;
          {
            container.emplace(StringInternerKey<Container>(key), identifier)
          } -> std::same_as<std::pair<typename Container::iterator, bool>>;
        }) || (!StringInternerContainerTraits<Container>::kIsMap && requires(Container& container, std::string_view key) {
          {
            container.emplace(StringInternerKey<Container>(key))
          } -> std::same_as<std::pair<typename Container::iterator, bool>>;
        }));

template<typename Storage>
concept StringInternerStorage = requires(Storage& storage, std::string_view text) {
  { storage.try_store(text) } noexcept -> std::same_as<std::optional<std::string_view>>;
};

template<typename Entries, typename Entry>
concept StringInternerEntries =
    std::ranges::bidirectional_range<Entries> && std::ranges::bidirectional_range<const Entries>
    && std::same_as<typename Entries::value_type, Entry> && requires(Entries& entries, const Entries& read) {
         { read.size() } -> std::convertible_to<std::size_t>;
         entries.emplace_back();
         { entries.back() } -> std::same_as<Entry&>;
       };

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_TYPES_H_
