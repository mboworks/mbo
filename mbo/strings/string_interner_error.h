// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_INTERNER_ERROR_H_
#define MBO_STRINGS_STRING_INTERNER_ERROR_H_

#include <expected>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace mbo::strings {

enum class StringInternerError { kIdExhausted, kCharacterStorageExhausted, kEntryStorageExhausted, kIndexExhausted };

[[nodiscard]] constexpr absl::StatusCode ToStatusCode(StringInternerError error) noexcept {
  // IDs exhaust their representable range; all storage failures exhaust a resource.
  return error == StringInternerError::kIdExhausted ? absl::StatusCode::kOutOfRange
                                                    : absl::StatusCode::kResourceExhausted;
}

template<typename Id>
[[nodiscard]] absl::StatusOr<std::pair<Id, bool>> ToStatusOr(
    std::expected<std::pair<Id, bool>, StringInternerError> result) {
  if (result) {
    return std::move(*result);
  }
  return absl::Status(ToStatusCode(result.error()), {});
}

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_INTERNER_ERROR_H_
