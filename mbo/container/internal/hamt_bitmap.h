// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_BITMAP_H_
#define MBO_CONTAINER_INTERNAL_HAMT_BITMAP_H_

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace mbo::container::container_internal {

// Compact occupancy and rank primitive shared by HAMT map/set and flat/node layouts.
template<std::size_t FragmentBits>
requires(FragmentBits >= 4 && FragmentBits <= 7)
class HamtBitmap final {
 public:
  static constexpr std::size_t kSlotCount = std::size_t{1} << FragmentBits;
  static constexpr std::size_t kWordCount = (kSlotCount + 63) / 64;

  constexpr bool Contains(std::size_t slot) const noexcept { return (words_.at(Word(slot)) & Bit(slot)) != 0; }

  constexpr std::size_t Rank(std::size_t slot) const noexcept {
    const std::size_t word = Word(slot);
    std::size_t result = 0;
    for (std::size_t pos = 0; pos < word; ++pos) {
      result += std::popcount(words_.at(pos));
    }
    return result + std::popcount(words_.at(word) & MaskBefore(slot));
  }

  constexpr bool Set(std::size_t slot) noexcept {
    const std::uint64_t bit = Bit(slot);
    const bool inserted = (words_.at(Word(slot)) & bit) == 0;
    words_.at(Word(slot)) |= bit;
    return inserted;
  }

  constexpr bool Reset(std::size_t slot) noexcept {
    const std::uint64_t bit = Bit(slot);
    const bool erased = (words_.at(Word(slot)) & bit) != 0;
    words_.at(Word(slot)) &= ~bit;
    return erased;
  }

  constexpr std::size_t Size() const noexcept {
    std::size_t result = 0;
    for (const std::uint64_t word : words_) {
      result += std::popcount(word);
    }
    return result;
  }

 private:
  static constexpr std::size_t Word(std::size_t slot) noexcept { return slot / 64; }

  static constexpr std::uint64_t Bit(std::size_t slot) noexcept { return std::uint64_t{1} << (slot % 64); }

  static constexpr std::uint64_t MaskBefore(std::size_t slot) noexcept {
    const std::size_t offset = slot % 64;
    return offset == 0 ? 0 : (std::uint64_t{1} << offset) - 1;
  }

  std::array<std::uint64_t, kWordCount> words_{};
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_BITMAP_H_
