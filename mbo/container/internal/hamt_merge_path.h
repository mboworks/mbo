// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_MERGE_PATH_H_
#define MBO_CONTAINER_INTERNAL_HAMT_MERGE_PATH_H_

#include <concepts>
#include <cstddef>

#include "mbo/container/internal/hamt_hash_path.h"

namespace mbo::container::container_internal {

struct HamtMergePath final {
  std::size_t common_levels;
  std::size_t existing_fragment;
  std::size_t inserted_fragment;
  bool full_hash_collision;
};

// The caller has already established that the fragments before start_level are
// equal. start_level must not exceed HamtHashPath<Hash, FragmentBits>::kLevels;
// equality is allowed and represents an already exhausted hash path.
template<std::unsigned_integral Hash, std::size_t FragmentBits>
requires(FragmentBits >= 4 && FragmentBits <= 7)
constexpr HamtMergePath FindHamtMergePath(Hash existing, Hash inserted, std::size_t start_level = 0) noexcept {
  const HamtHashPath<Hash, FragmentBits> existing_path(existing);
  const HamtHashPath<Hash, FragmentBits> inserted_path(inserted);
  std::size_t level = start_level;
  while (level < existing_path.kLevels && existing_path.Fragment(level) == inserted_path.Fragment(level)) {
    ++level;
  }
  if (level == existing_path.kLevels) {
    return {
        .common_levels = level - start_level,
        .existing_fragment = 0,
        .inserted_fragment = 0,
        .full_hash_collision = true,
    };
  }
  return {
      .common_levels = level - start_level,
      .existing_fragment = existing_path.Fragment(level),
      .inserted_fragment = inserted_path.Fragment(level),
      .full_hash_collision = false,
  };
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_MERGE_PATH_H_
