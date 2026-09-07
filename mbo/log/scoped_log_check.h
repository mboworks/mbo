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

#ifndef MBO_LOG_SCOPED_LOG_CHECK_H_
#define MBO_LOG_SCOPED_LOG_CHECK_H_

#include <concepts>  // IWYU pragma: keep
#include <iostream>
#include <source_location>
#include <sstream>
#include <string_view>
#include <variant>

#include "mbo/log/scoped_stream.h"

#if defined(__GNUC__) || defined(__clang__)
# define MBO_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
# define MBO_FORCE_INLINE __forceinline
#else
# define MBO_FORCE_INLINE inline
#endif

namespace mbo::log {

// *** Only use for depnstration purpose! ***
namespace log_internal {

using FatalScopedStream = ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, std::ostream, std::string_view>;

// *** Only use for depnstration purpose! ***
using ScopedStreamVariants = std::variant<
    std::ostream,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, std::ostream, std::string_view>,
    FatalScopedStream,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, std::ostream, std::string_view>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, std::ostream, std::string_view>,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, VoidStream, std::string_view>,
    ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, VoidStream, std::string_view>,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, VoidStream, std::string_view>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, VoidStream, std::string_view>,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, Voidifier, std::string_view>,
    ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, Voidifier, std::string_view>,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, Voidifier, std::string_view>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, Voidifier, std::string_view>,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, std::ostream, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, std::ostream, Voidifier>,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, std::ostream, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, std::ostream, Voidifier>,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, VoidStream, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, VoidStream, Voidifier>,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, VoidStream, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, VoidStream, Voidifier>,
    ScopedStream<ScopedStreamMode::kContinue, std::stringstream, Voidifier, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, Voidifier, Voidifier>,
    ScopedStream<ScopedStreamMode::kContinue, VoidStream, Voidifier, Voidifier>,
    ScopedStream<ScopedStreamMode::kQuickExit, VoidStream, Voidifier, Voidifier>,
    VoidStream>;

// *** Only use for depnstration purpose! ***
struct ScopedStreamer : ScopedStreamVariants {
  ScopedStreamer() = delete;

  explicit ScopedStreamer(VoidStream str) : ScopedStreamVariants(str) {}

  // LCOV_EXCL_FUNC_LINE: quick_exit prevents this process from flushing its coverage data.
  template<typename ScopedStreamType>
  ScopedStreamer(
      std::in_place_type_t<ScopedStreamType> in_place_t,
      const std::source_location& loc,
      ScopedStreamType::OStreamField out,
      const ScopedStreamType::MessageField& msg)
      : ScopedStreamVariants{in_place_t, loc, out, msg} {}

  friend ScopedStreamer&& operator<<(ScopedStreamer&& out, std::string_view val) {
    out.Output(val);
    return std::move(out);
  }

  // LCOV_MERGE_FUNC_LINE: Count the shared template definition once.
  template<typename T>
  friend ScopedStreamer&& operator<<(ScopedStreamer&& out, const T& val) {
    out.Output(val);
    return std::move(out);
  }

  friend ScopedStreamer& operator<<(ScopedStreamer& out, std::string_view val) {
    out.Output(val);
    return out;
  }

  // LCOV_MERGE_FUNC_LINE: Count the shared template definition once.
  template<typename T>
  friend ScopedStreamer& operator<<(ScopedStreamer& out, const T& val) {
    out.Output(val);
    return out;
  }

  // LCOV_MERGE_FUNC_LINE: Count the shared template definition once.
  template<typename T>
  void Output(const T& val) {
    // LCOV_MERGE_FUNC_LINE: Each variant alternative instantiates this same visitor body.
    std::visit([val]<typename OS>(OS& os) { os << val; }, *this);
  }
};
}  // namespace log_internal

// *** Only use for depnstration purpose! ***
MBO_FORCE_INLINE log_internal::ScopedStreamer ScopedLogCheck(
    bool check,
    std::string_view what,
    const std::source_location& loc = std::source_location::current()) {
  if (check) {
    return log_internal::ScopedStreamer{VoidStream{}};
  }
  return {
      std::in_place_type_t<
          ScopedStream<ScopedStreamMode::kQuickExit, std::stringstream, std::ostream, std::string_view>>{},
      loc, std::cerr, what};
}

// *** Only use for depnstration purpose! ***
template<typename Disallowed>
void ScopedLogCheck(bool, std::string_view, const Disallowed&&) {  // NOLINT(*-named-parameter)
  static_assert(false, "Must call ScopedStreamVoid with exactly two arguments");
}

}  // namespace mbo::log

#undef MBO_FORCE_INLINE

// A local optimized check implementation, it only processes the log stream if its `check` is false.
#define MBO_LOG_CHECK(check)                            \
  while (!(check))                                      \
  mbo::log::ScopedStream<ScopedStreamMode::kQuickExit>( \
      std::source_location::current(), std::cerr, "Failed MBO_LOG_CHECK(" #check ")")

#endif  // MBO_LOG_SCOPED_LOG_CHECK_H_
