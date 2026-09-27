// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_DIRECTORY_H_
#define MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_DIRECTORY_H_

#include <bit>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace mbo::container::container_internal {

// Experimental pointer directory, not a public ring container. Only the deque's private
// representation depends on this interface. Coordinates are segment numbers; capacity is a
// power of two, and callers keep at most that many consecutive live segment coordinates.
// Unused entries are null. This object owns pointer storage, never the pointed-to segments.
template<typename Pointer, typename Allocator = std::allocator<Pointer>>
requires std::is_pointer_v<Pointer>
class ExperimentalCircularDirectory final {
 public:
  constexpr ExperimentalCircularDirectory() noexcept(std::is_nothrow_default_constructible_v<Allocator>) = default;

  constexpr explicit ExperimentalCircularDirectory(const Allocator& allocator) noexcept : slots_(allocator) {}

  constexpr ExperimentalCircularDirectory(const ExperimentalCircularDirectory&) = default;
  constexpr ExperimentalCircularDirectory& operator=(const ExperimentalCircularDirectory&) = default;
  constexpr ExperimentalCircularDirectory(ExperimentalCircularDirectory&&) noexcept = default;
  constexpr ExperimentalCircularDirectory& operator=(ExperimentalCircularDirectory&&) noexcept(
      std::is_nothrow_move_assignable_v<std::vector<Pointer, Allocator>>) = default;
  constexpr ~ExperimentalCircularDirectory() = default;

  constexpr ExperimentalCircularDirectory(const ExperimentalCircularDirectory& other, const Allocator& allocator)
      : slots_(other.slots_, allocator) {}

  constexpr Allocator GetAllocator() const noexcept { return slots_.get_allocator(); }

  constexpr std::size_t SlotCount() const noexcept { return slots_.size(); }

  constexpr Pointer& At(std::size_t coordinate) noexcept {
    // The container proves a nonempty directory and masks the coordinate to an acquired slot.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return slots_[coordinate & (slots_.size() - 1)];
  }

  constexpr Pointer At(std::size_t coordinate) const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return slots_[coordinate & (slots_.size() - 1)];
  }

  // required <= maximum, maximum is a power of two. The live interval may wrap unsigned zero.
  // Allocation happens before any old pointer changes, so growth provides the strong guarantee.
  constexpr void Grow(std::size_t required, std::size_t maximum, std::size_t first, std::size_t live_count) {
    if (required <= SlotCount()) {
      return;
    }
    const std::size_t count = std::bit_ceil(required);
    ExperimentalCircularDirectory replacement(GetAllocator());
    replacement.slots_.resize(count < maximum ? count : maximum, nullptr);
    for (std::size_t index = 0; index < live_count; ++index) {
      const std::size_t coordinate = first + index;
      replacement.At(coordinate) = At(coordinate);
    }
    Swap(replacement);
  }

  // Callers transfer segment ownership before clearing or swapping the pointer storage.
  constexpr void Clear() noexcept { slots_.clear(); }

  constexpr void Swap(ExperimentalCircularDirectory& other) noexcept(noexcept(slots_.swap(other.slots_))) {
    slots_.swap(other.slots_);
  }

 private:
  std::vector<Pointer, Allocator> slots_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_DIRECTORY_H_
