// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_LAYOUT_H_
#define MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_LAYOUT_H_

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace mbo::container::container_internal {

template<typename Header, typename Entry, typename Child>
struct HamtPackedNodeLayout final {
  std::size_t data_offset;
  std::size_t child_offset;
  std::size_t size;
  std::size_t alignment;

  // Computes a single allocation containing the header, dense entries, and child handles.
  // Every offset is relative to an allocation aligned to `alignment`; overflow fails before IO.
  [[nodiscard]] static constexpr std::optional<HamtPackedNodeLayout> TryMake(
      std::size_t data_count,
      std::size_t child_count) noexcept {
    const auto data_offset = Align(sizeof(Header), alignof(Entry));
    if (!data_offset || data_count > (std::numeric_limits<std::size_t>::max() - *data_offset) / sizeof(Entry)) {
      return std::nullopt;
    }
    const std::size_t data_end = *data_offset + (data_count * sizeof(Entry));
    const auto child_offset = Align(data_end, alignof(Child));
    if (!child_offset || child_count > (std::numeric_limits<std::size_t>::max() - *child_offset) / sizeof(Child)) {
      return std::nullopt;
    }
    return HamtPackedNodeLayout{
        .data_offset = *data_offset,
        .child_offset = *child_offset,
        .size = *child_offset + (child_count * sizeof(Child)),
        .alignment = std::max({alignof(Header), alignof(Entry), alignof(Child)}),
    };
  }

 private:
  static constexpr std::optional<std::size_t> Align(std::size_t value, std::size_t alignment) noexcept {
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
      return value;
    }
    const std::size_t padding = alignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - padding) {
      return std::nullopt;
    }
    return value + padding;
  }
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_LAYOUT_H_
