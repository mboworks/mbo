// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_VALUE_POP_H_
#define MBO_CONTAINER_INTERNAL_VALUE_POP_H_

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace mbo::container::container_internal {

// P3182R1: construct the return object directly, then remove the source element.
// Callers check their endpoint preconditions before supplying the reference and
// a nonthrowing removal callback. A failed move may alter value but never pops it.
template<std::move_constructible T, typename Pop>
requires std::is_nothrow_invocable_r_v<void, Pop&>
constexpr T PopValue(T& value, Pop pop) noexcept(std::is_nothrow_move_constructible_v<T>) {
  // NOLINTNEXTLINE(misc-const-correctness): throwing element types disarm the guard on failure.
  struct PopOnSuccess final {
    constexpr explicit PopOnSuccess(Pop& action) noexcept : action(action) {}

    PopOnSuccess(const PopOnSuccess&) = delete;
    PopOnSuccess& operator=(const PopOnSuccess&) = delete;
    PopOnSuccess(PopOnSuccess&&) = delete;
    PopOnSuccess& operator=(PopOnSuccess&&) = delete;

    constexpr ~PopOnSuccess() noexcept {
      if (active) {
        std::invoke(action);
      }
    }

    Pop& action;
    bool active = true;
  } guard{pop};
#if __cpp_exceptions
  if constexpr (!std::is_nothrow_move_constructible_v<T>) {
    try {
      return std::move(value);
    } catch (...) {
      guard.active = false;
      throw;
    }
  } else
#endif
  {
    return std::move(value);
  }
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_VALUE_POP_H_
