// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_CONTRACT_H_
#define MBO_CONTAINER_INTERNAL_HAMT_CONTRACT_H_

#include <exception>

namespace mbo::container::container_internal {

inline void RequireHamtValue(const void* value) noexcept {
  if (value == nullptr) {
    std::terminate();
  }
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_CONTRACT_H_
