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

#ifndef MBO_CONFIG_REQUIRE_H_
#define MBO_CONFIG_REQUIRE_H_

#include <stdexcept>  // IWYU pragma: keep

#include "absl/log/absl_log.h"  // IWYU pragma: keep
#include "mbo/config/config.h"  // IWYU pragma: keep

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#define MBO_PRIVATE_CONFIG_CAT_CAT_(line) #line
#define MBO_PRIVATE_CONFIG_NUM2STR_(line) MBO_PRIVATE_CONFIG_CAT_CAT_(line)

#ifdef MBO_CONFIG_REQUIRE
# undef MBO_CONFIG_REQUIRE
#endif

#if __cpp_exceptions
# define MBO_CONFIG_REQUIRE(condition, message)                                          \
   if constexpr (::mbo::config::kRequireThrows) {                                        \
     if (!(condition))                                                                   \
       throw std::runtime_error(                                                         \
           __FILE__ ":" MBO_PRIVATE_CONFIG_NUM2STR_(__LINE__) " : Required (" #condition \
                                                              ") "                       \
                                                              ": " message);             \
   } else                                                                                \
     /* NOLINTNEXTLINE(bugprone-switch-missing-default-case) */                          \
     ABSL_LOG_IF(FATAL, !(condition)) << message

#else  // __cpp_exceptions

# define MBO_CONFIG_REQUIRE(condition, message)               \
   /* NOLINTNEXTLINE(bugprone-switch-missing-default-case) */ \
   ABSL_LOG_IF(FATAL, !(condition)) << message

#endif  // __cpp_exceptions

#ifdef NDEBUG
// Debug only requirement log.
# define MBO_CONFIG_REQUIRE_DEBUG(condition, message) ABSL_DLOG(INFO)
#else  // NDEBUG
// Debug only requirement log.
# define MBO_CONFIG_REQUIRE_DEBUG(condition, message) MBO_CONFIG_REQUIRE(condition, message)
#endif  // NDEBUG

// NOLINTEND(cppcoreguidelines-macro-usage)

#endif  // MBO_CONFIG_REQUIRE_H_
