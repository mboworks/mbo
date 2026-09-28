// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_SEGMENTED_OPTIONS_H_
#define MBO_CONTAINER_SEGMENTED_OPTIONS_H_

#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

#include "mbo/memory/block_source.h"

namespace mbo::container {

// Shared storage bounds for SegmentedVector and SegmentedDeque.
struct SegmentedOptions final {
  std::size_t segment_size = 256;
  // Maximum number of segment slots. SIZE_MAX selects unbounded directory growth.
  std::size_t segment_capacity = std::numeric_limits<std::size_t>::max();
  // Initial number of segment-directory slots to reserve.
  std::size_t segment_reservation = 1;

  constexpr bool IsValid() const noexcept {
    return std::has_single_bit(segment_size)
           && (segment_capacity == std::numeric_limits<std::size_t>::max() || std::has_single_bit(segment_capacity))
           && (segment_reservation == 0 || std::has_single_bit(segment_reservation))
           && (segment_capacity == std::numeric_limits<std::size_t>::max() || segment_reservation <= segment_capacity);
  }
};

template<SegmentedOptions Options>
concept ValidSegmentedOptions = Options.IsValid();

template<typename T>
concept SegmentedElement = std::is_object_v<T> && !std::is_array_v<T> && std::same_as<T, std::remove_cv_t<T>>
                           && requires { sizeof(T); } && std::destructible<T>;

namespace container_internal {

template<SegmentedElement T>
constexpr std::size_t SegmentedRepresentationCapacityLimit() noexcept {
  constexpr auto kDifferenceLimit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  constexpr std::size_t kObjectLimit = std::numeric_limits<std::size_t>::max() / sizeof(T);
  return kDifferenceLimit < kObjectLimit ? kDifferenceLimit : kObjectLimit;
}

}  // namespace container_internal

template<typename T, SegmentedOptions Options>
concept RepresentableSegmentedOptions =
    SegmentedElement<T> && ValidSegmentedOptions<Options>
    && Options.segment_size
           <= (static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) - sizeof(mbo::memory::MemoryBlock)
               - (2 * sizeof(std::size_t)) - (alignof(T) - 1)
               - ((alignof(T) < alignof(mbo::memory::MemoryBlock) ? alignof(mbo::memory::MemoryBlock) : alignof(T))
                  - 1))
                  / sizeof(T)
    && (Options.segment_capacity == std::numeric_limits<std::size_t>::max()
        || Options.segment_capacity
               <= container_internal::SegmentedRepresentationCapacityLimit<T>() / Options.segment_size)
    && Options.segment_reservation
           <= container_internal::SegmentedRepresentationCapacityLimit<T>() / Options.segment_size;

}  // namespace mbo::container

#endif  // MBO_CONTAINER_SEGMENTED_OPTIONS_H_
