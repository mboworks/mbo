// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_HAMT_OPTIONS_H_
#define MBO_CONTAINER_HAMT_OPTIONS_H_

#include <cstddef>
#include <limits>

namespace mbo::container {

struct HamtOptions final {
  std::size_t fragment_bits = 5;
  std::size_t maximum_size = std::numeric_limits<std::size_t>::max();

  constexpr bool IsValid() const noexcept { return fragment_bits >= 4 && fragment_bits <= 7 && maximum_size > 0; }
};

template<HamtOptions Options>
concept ValidHamtOptions = Options.IsValid();

enum class HamtError {
  kAllocationExhausted,
  kMaxSizeExceeded,
};

}  // namespace mbo::container

#endif  // MBO_CONTAINER_HAMT_OPTIONS_H_
