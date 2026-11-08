// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MBO_STATUS_STATUS_MACROS_H_
#define MBO_STATUS_STATUS_MACROS_H_

#include <utility>  // IWYU pragma: keep

#include "absl/status/status.h"         // IWYU pragma: keep
#include "absl/status/statusor.h"       // IWYU pragma: keep
#include "mbo/status/status.h"          // IWYU pragma: export
#include "mbo/status/status_builder.h"  // IWYU pragma: export

// Macro that allows to return from a non-OkStatus in a single line.
// This is pure syntactical sugar for readability:
//
// Instead of:
//
// ```c++
// auto result = FooBar();
// if (!result.ok()) {
//   return result;
// }
// ```
//
// The same can be written as:
//
// ```c++
// MBO_RETURN_IF_ERROR(FooBar());
// ```
//
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_RETURN_IF_ERROR(expr)                                                          \
  MBO_PRIVATE_STATUS_STATUS_MACROS_ELSE_BLOCKER_                                           \
  if (const auto var = (expr); var.ok()) { /* NOLINT(*-unnecessary-copy-initialization) */ \
    /* GOOD */                                                                             \
  } else                                                                                   \
    return mbo::status::StatusBuilder(var)

// Similar to MBO_RETURN_IF_ERROR but this assigns the result of an `absl::StatusOr<T>`, to a target
// which can be an existing variable or a new variable declaration. The assignment is by move.
//
// Instead of:
//
// ```c++
// absl::StatusOr<T> var_or = FooBar();
// if (!var_or.ok()) {
//   return var_or.status();
// }
// T var = *std::move(var_or);
// ```
//
// This can be simplified to:
//
// ```c++
// MBO_ASSIGN_OR_RETURN(T var, FooBar());
// ```
//
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_ASSIGN_OR_RETURN(res, expr)              \
  MBO_PRIVATE_STAUS_STATUS_MACROS_ASSIGN_OR_RETURN_( \
      MBO_PRIVATE_STAUS_STATUS_MACROS_VAR_CAT_(_status_or_macro_var_, __LINE__), expr, res)

// Variant of `MBO_ASSIGN_OR_RETURN` that allows to assign `absl::StatusOr<T>` expressions to
// a `T` even if that requires comms. In particular this allows for strcutured bindings.
//
// IMPORTANT: This macro is different from `MBO_ASSIGN_OR_RETURN`. Here the expression comes first,
// because C++20 macros can easily handle expressions with commas where brackts surround them. But
// it is not (reasonably) possible to extract the rightmost macro argument. So by moving the target
// to the left, even complex assignments requiring non expression bound commas are possible.
//
// Example:
//
// ```
// absl::StatusOr<std::pair<int, int>> Function(std::pair<int, int> val) {
//   return absl::StatusOr<std::pair<int, int>>(val);
// }
// MBO_MOVE_TO_OR_RETURN(Function(std::make_pair(17, 25)), auto [first, second]);
// ```
//
// In the above the expression is `Function(std::make_pair(17, 25))`. As mentioned above that works
// as the comma is in a bracketed term. The result of the expression is moved into the target, here
// `auto [first, second]`. That works as the macro simply takes all two parts `auto [first` and
// `second]` and comma joins them again.
//
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_MOVE_TO_OR_RETURN(expr, ...)             \
  MBO_PRIVATE_STAUS_STATUS_MACROS_ASSIGN_OR_RETURN_( \
      MBO_PRIVATE_STAUS_STATUS_MACROS_VAR_CAT_(_status_or_macro_var_, __LINE__), expr, __VA_ARGS__)

// PRIVATE MACRO - DO NOT USE.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_PRIVATE_STAUS_STATUS_MACROS_ASSIGN_OR_RETURN_(var, expr, ...) \
  auto var = (expr);                                                      \
  if (!var.ok()) {                                                        \
    return var.status();                                                  \
  }                                                                       \
  __VA_ARGS__ = *std::move(var)

// PRIVATE MACRO - DO NOT USE.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_PRIVATE_STAUS_STATUS_MACROS_VAR_CAT_IMPL_(var, line) var##line

// PRIVATE MACRO - DO NOT USE.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_PRIVATE_STAUS_STATUS_MACROS_VAR_CAT_(var, line) MBO_PRIVATE_STAUS_STATUS_MACROS_VAR_CAT_IMPL_(var, line)

// PRIVATE MACRO - DO NOT USE.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MBO_PRIVATE_STATUS_STATUS_MACROS_ELSE_BLOCKER_ \
  switch (0)                                           \
  case 0:                                              \
  default:  // NOLINT

#endif  // MBO_STATUS_STATUS_MACROS_H_
