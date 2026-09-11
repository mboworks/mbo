// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_HASH_PATH_H_
#define MBO_CONTAINER_INTERNAL_HAMT_HASH_PATH_H_

#include <concepts>
#include <cstddef>
#include <limits>

namespace mbo::container::container_internal {

template<std::unsigned_integral Hash, std::size_t FragmentBits>
requires(FragmentBits >= 4 && FragmentBits <= 7)
class HamtHashPath final {
 public:
  static constexpr std::size_t kHashBits = std::numeric_limits<Hash>::digits;
  static constexpr std::size_t kLevels = (kHashBits + FragmentBits - 1) / FragmentBits;
  static constexpr Hash kFragmentMask = (Hash{1} << FragmentBits) - 1;

  constexpr explicit HamtHashPath(Hash hash) noexcept : hash_(hash) {}

  // level must be smaller than kLevels. Bits beyond the hash width in the final
  // fragment are always zero.
  constexpr std::size_t fragment(std::size_t level) const noexcept {
    return static_cast<std::size_t>((hash_ >> (level * FragmentBits)) & kFragmentMask);
  }

  constexpr Hash hash() const noexcept { return hash_; }

 private:
  Hash hash_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_HASH_PATH_H_
