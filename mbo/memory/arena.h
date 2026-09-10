// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_MEMORY_ARENA_H_
#define MBO_MEMORY_ARENA_H_

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "mbo/config/require.h"
#include "mbo/memory/block_source.h"

namespace mbo::memory {

struct ArenaOptions final {
  std::size_t initial_block_size = 4'096;
  std::size_t maximum_block_size = std::size_t{1'024} * 1'024;
  std::size_t growth_numerator = 2;
  std::size_t growth_denominator = 1;

  consteval bool IsValid() const noexcept {
    return initial_block_size > sizeof(void*) && maximum_block_size >= initial_block_size
           && growth_numerator >= growth_denominator && growth_denominator != 0;
  }
};

template<auto Options>
concept ValidArenaOptions = std::same_as<std::remove_cv_t<decltype(Options)>, ArenaOptions> && Options.IsValid();

template<BlockSource Source = NewDeleteBlockSource, ArenaOptions Options = {}>
requires ValidArenaOptions<Options>
class Arena final {
 private:
  struct Block final {
    Block* next = nullptr;
    MemoryBlock memory{};
    std::size_t begin = 0;
    std::size_t cursor = 0;
    bool oversized = false;
  };

  static constexpr std::size_t kHeaderAlignment = alignof(Block);

  static constexpr bool IsPowerOfTwo(std::size_t value) noexcept { return value != 0 && (value & (value - 1)) == 0; }

  static constexpr bool Add(std::size_t lhs, std::size_t rhs, std::size_t& result) noexcept {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
      return false;
    }
    result = lhs + rhs;
    return true;
  }

  static constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
  }

 public:
  // NOLINTBEGIN(readability-identifier-naming): Arena models STL/PMR naming.
  constexpr Arena() noexcept(std::is_nothrow_default_constructible_v<Source>) = default;

  constexpr explicit Arena(Source source) noexcept(std::is_nothrow_move_constructible_v<Source>)
      : source_(std::move(source)) {}

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  constexpr Arena(Arena&& other) noexcept(std::is_nothrow_move_constructible_v<Source>)
  requires std::move_constructible<Source>
      : source_(std::move(other.source_)),
        first_(std::exchange(other.first_, nullptr)),
        current_(std::exchange(other.current_, nullptr)),
        bytes_used_(std::exchange(other.bytes_used_, 0)),
        bytes_reserved_(std::exchange(other.bytes_reserved_, 0)),
        block_count_(std::exchange(other.block_count_, 0)),
        next_block_size_(std::exchange(other.next_block_size_, Options.initial_block_size)) {}

  constexpr Arena& operator=(Arena&& other) noexcept(
      std::is_nothrow_move_assignable_v<Source> && std::is_nothrow_move_constructible_v<Source>)
  requires std::move_constructible<Source> && std::is_move_assignable_v<Source>
  {
    if (this != &other) {
      Release();
      source_ = std::move(other.source_);
      first_ = std::exchange(other.first_, nullptr);
      current_ = std::exchange(other.current_, nullptr);
      bytes_used_ = std::exchange(other.bytes_used_, 0);
      bytes_reserved_ = std::exchange(other.bytes_reserved_, 0);
      block_count_ = std::exchange(other.block_count_, 0);
      next_block_size_ = std::exchange(other.next_block_size_, Options.initial_block_size);
    }
    return *this;
  }

  constexpr ~Arena() { Release(); }

  constexpr std::byte* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
    auto* const result = TryAllocate(size, alignment);
    MBO_CONFIG_REQUIRE(result != nullptr, "Arena allocation failed");
    return result;
  }

  constexpr std::byte* TryAllocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
    MBO_CONFIG_REQUIRE(size > 0, "Arena allocation size must be greater than zero");
    MBO_CONFIG_REQUIRE(IsPowerOfTwo(alignment), "Arena alignment must be a nonzero power of two");
    if (alignment > source_.max_alignment()) {
      return nullptr;
    }

    for (auto* block = current_; block != nullptr; block = block->next) {
      if (auto* const result = TryAllocateFrom(*block, size, alignment); result != nullptr) {
        current_ = block;
        return result;
      }
    }

    auto* const block = TryAddBlock(size, alignment);
    if (block == nullptr) {
      return nullptr;
    }
    current_ = block;
    return TryAllocateFrom(*block, size, alignment);
  }

  constexpr void Reset() noexcept {
    for (auto* block = first_; block != nullptr; block = block->next) {
      block->cursor = block->begin;
    }
    current_ = first_;
    bytes_used_ = 0;
  }

  constexpr void Release() noexcept {
    auto* block = first_;
    while (block != nullptr) {
      auto* const next = block->next;
      const auto memory = block->memory;
      std::destroy_at(block);
      source_.Release(memory);
      block = next;
    }
    first_ = nullptr;
    current_ = nullptr;
    bytes_used_ = 0;
    bytes_reserved_ = 0;
    block_count_ = 0;
    next_block_size_ = Options.initial_block_size;
  }

  constexpr std::size_t bytes_used() const noexcept { return bytes_used_; }

  constexpr std::size_t bytes_reserved() const noexcept { return bytes_reserved_; }

  constexpr std::size_t block_count() const noexcept { return block_count_; }

  constexpr Source& source() noexcept { return source_; }

  constexpr const Source& source() const noexcept { return source_; }

  // NOLINTEND(readability-identifier-naming)

 private:
  constexpr std::byte* TryAllocateFrom(Block& block, std::size_t size, std::size_t alignment) noexcept {
    if (block.memory.alignment < alignment) {
      return nullptr;
    }
    std::size_t aligned = 0;
    if (!Add(block.cursor, alignment - 1, aligned)) {
      return nullptr;
    }
    aligned = AlignUp(aligned - (alignment - 1), alignment);
    std::size_t end = 0;
    if (!Add(aligned, size, end) || end > block.memory.size) {
      return nullptr;
    }
    bytes_used_ += end - block.cursor;
    block.cursor = end;
    return block.memory.data + aligned;
  }

  constexpr Block* TryAddBlock(std::size_t size, std::size_t alignment) noexcept {
    const std::size_t effective_alignment = std::max(alignment, kHeaderAlignment);
    std::size_t overhead = 0;
    if (!Add(sizeof(Block), effective_alignment - 1, overhead)) {
      return nullptr;
    }
    std::size_t required = 0;
    if (!Add(overhead, size, required)) {
      return nullptr;
    }
    std::size_t reserved_after = 0;
    if (!Add(bytes_reserved_, std::max(required, next_block_size_), reserved_after)
        || block_count_ == std::numeric_limits<std::size_t>::max()) {
      return nullptr;
    }
    const bool oversized = required > next_block_size_;
    const std::size_t acquisition_size = oversized ? required : next_block_size_;
    auto memory = source_.TryAcquire(acquisition_size, effective_alignment);
    std::size_t actual_reserved_after = 0;
    if (!memory || memory->data == nullptr || memory->size < required || memory->alignment < effective_alignment
        || std::bit_cast<std::uintptr_t>(memory->data) % effective_alignment != 0
        || !Add(bytes_reserved_, memory->size, actual_reserved_after)) {
      if (memory && memory->data != nullptr) {
        source_.Release(*memory);
      }
      return nullptr;
    }

    const std::size_t begin = AlignUp(sizeof(Block), effective_alignment);
    // The block header begins its lifetime in suitably aligned raw storage owned by the source.
    auto* const block = std::construct_at(
        reinterpret_cast<Block*>(memory->data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        Block{.next = nullptr, .memory = *memory, .begin = begin, .cursor = begin, .oversized = oversized});
    if (first_ == nullptr) {
      first_ = block;
    } else {
      auto* tail = first_;
      while (tail->next != nullptr) {
        tail = tail->next;
      }
      tail->next = block;
    }
    bytes_reserved_ = actual_reserved_after;
    ++block_count_;
    if (!oversized) {
      AdvanceGrowth();
    }
    return block;
  }

  constexpr void AdvanceGrowth() noexcept {
    const auto quotient = next_block_size_ / Options.growth_denominator;
    const auto remainder = next_block_size_ % Options.growth_denominator;
    std::size_t grown = Options.maximum_block_size;
    if (quotient <= std::numeric_limits<std::size_t>::max() / Options.growth_numerator) {
      grown = quotient * Options.growth_numerator;
      if (remainder <= std::numeric_limits<std::size_t>::max() / Options.growth_numerator) {
        const auto extra = remainder * Options.growth_numerator / Options.growth_denominator;
        if (extra <= std::numeric_limits<std::size_t>::max() - grown) {
          grown += extra;
        }
      }
    }
    next_block_size_ = std::clamp(grown, next_block_size_, Options.maximum_block_size);
  }

  Source source_{};
  Block* first_ = nullptr;
  Block* current_ = nullptr;
  std::size_t bytes_used_ = 0;
  std::size_t bytes_reserved_ = 0;
  std::size_t block_count_ = 0;
  std::size_t next_block_size_ = Options.initial_block_size;
};

}  // namespace mbo::memory

#endif  // MBO_MEMORY_ARENA_H_
