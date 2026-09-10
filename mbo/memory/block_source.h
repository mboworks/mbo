// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_MEMORY_BLOCK_SOURCE_H_
#define MBO_MEMORY_BLOCK_SOURCE_H_

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace mbo::memory {

struct MemoryBlock final {
  std::byte* data = nullptr;
  std::size_t size = 0;
  std::size_t alignment = 0;

  friend constexpr bool operator==(const MemoryBlock&, const MemoryBlock&) = default;
};

template<typename Source>
concept BlockSource = requires(Source& source, MemoryBlock block, std::size_t size, std::size_t alignment) {
  { source.TryAcquire(size, alignment) } -> std::same_as<std::optional<MemoryBlock>>;
  { source.Release(block) } noexcept;
  { source.max_alignment() } noexcept -> std::convertible_to<std::size_t>;
};

// NOLINTBEGIN(readability-identifier-naming): block-source accessors follow allocator/STL spelling.

class NewDeleteBlockSource final {
 public:
  static constexpr std::size_t max_alignment() noexcept {
    return std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
  }

  static std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0 || alignment == 0 || alignment > max_alignment() || (alignment & (alignment - 1)) != 0) {
      return std::nullopt;
    }
    const auto effective_alignment = std::max(alignment, alignof(std::max_align_t));
    auto* const data =
        static_cast<std::byte*>(::operator new(size, std::align_val_t{effective_alignment}, std::nothrow));
    if (data == nullptr) {
      return std::nullopt;
    }
    return MemoryBlock{.data = data, .size = size, .alignment = effective_alignment};
  }

  static void Release(MemoryBlock block) noexcept { ::operator delete(block.data, std::align_val_t{block.alignment}); }
};

class FixedBlockSource final {
 public:
  explicit constexpr FixedBlockSource(
      std::span<std::byte> storage,
      std::size_t max_alignment = alignof(std::max_align_t)) noexcept
      : storage_(storage), max_alignment_(max_alignment) {}

  constexpr std::size_t max_alignment() const noexcept { return max_alignment_; }

  constexpr std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (acquired_ || size == 0 || size > storage_.size() || alignment == 0 || alignment > max_alignment_
        || (alignment & (alignment - 1)) != 0 || std::bit_cast<std::uintptr_t>(storage_.data()) % alignment != 0) {
      return std::nullopt;
    }
    acquired_ = true;
    return MemoryBlock{.data = storage_.data(), .size = storage_.size(), .alignment = max_alignment_};
  }

  constexpr void Release(MemoryBlock block) noexcept {
    if (block.data == storage_.data()) {
      acquired_ = false;
    }
  }

 private:
  std::span<std::byte> storage_;
  std::size_t max_alignment_;
  bool acquired_ = false;
};

template<std::size_t Size, std::size_t Alignment = alignof(std::max_align_t)>
requires(Size > 0 && Alignment > 0 && (Alignment & (Alignment - 1)) == 0)
class InlineBlockSource final {
 public:
  InlineBlockSource() = default;
  InlineBlockSource(const InlineBlockSource&) = delete;
  InlineBlockSource& operator=(const InlineBlockSource&) = delete;
  InlineBlockSource(InlineBlockSource&&) = delete;
  InlineBlockSource& operator=(InlineBlockSource&&) = delete;
  ~InlineBlockSource() = default;

  static constexpr std::size_t max_alignment() noexcept { return Alignment; }

  constexpr std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (acquired_ || size == 0 || size > Size || alignment == 0 || alignment > Alignment
        || (alignment & (alignment - 1)) != 0) {
      return std::nullopt;
    }
    acquired_ = true;
    return MemoryBlock{.data = storage_.data(), .size = Size, .alignment = Alignment};
  }

  constexpr void Release(MemoryBlock block) noexcept {
    if (block.data == storage_.data()) {
      acquired_ = false;
    }
  }

 private:
  alignas(Alignment) std::array<std::byte, Size> storage_{};
  bool acquired_ = false;
};

static_assert(BlockSource<NewDeleteBlockSource>);
static_assert(BlockSource<FixedBlockSource>);
static_assert(BlockSource<InlineBlockSource<4'096>>);

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::memory

#endif  // MBO_MEMORY_BLOCK_SOURCE_H_
