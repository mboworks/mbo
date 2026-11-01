// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_POLICIES_H_
#define MBO_STRINGS_STRING_INTERNER_POLICIES_H_

#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "mbo/strings/string_interner_error.h"
#include "mbo/types/optional_ref.h"

namespace mbo::strings {

// Index policies own hash/probe preparation. A prepared value may contain a hash, bucket, and
// container-specific probe state; it must remain valid until Find or Insert is called.
struct StringInternerIndexStlPolicy {
  template<typename Container>
  using prepared_type = std::string_view;

  template<typename Container>
  static prepared_type<Container> Prepare(const Container&, std::string_view key) noexcept {
    return key;
  }

  template<typename Container>
  static auto Find(const Container& container, prepared_type<Container> prepared) {
    return container.find(prepared);
  }

  template<typename Container, typename Id>
  static auto Insert(Container& container, prepared_type<Container> prepared, Id identifier) {
    if constexpr (requires { typename Container::mapped_type; }) {
      return container.emplace(typename Container::key_type(prepared), identifier);
    } else {
      return container.emplace(typename Container::key_type(prepared));
    }
  }
};

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
    typename LookupPolicy = StringInternerLookupMboPolicy,
    typename IndexPolicy = StringInternerIndexStlPolicy>
struct StringInternerPolicyHolder
    : ErrorPolicy
    , LookupPolicy {
  using index_policy = IndexPolicy;
  using ErrorPolicy::Failure;
  using ErrorPolicy::Success;
  using LookupPolicy::Get;
  using LookupPolicy::Make;

  template<typename Id>
  using insertion_result = typename ErrorPolicy::template insertion_result<Id>;

  template<typename T>
  using lookup_result = typename LookupPolicy::template result<T>;
};

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_POLICIES_H_
