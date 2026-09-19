// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_MEMORY_ARENA_BLOCK_SOURCE_H_
#define MBO_MEMORY_ARENA_BLOCK_SOURCE_H_

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

#include "mbo/memory/arena.h"
#include "mbo/memory/block_source.h"

namespace mbo::memory {

struct ArenaBlockSourceOptions final {
  std::size_t minimum_block_size = 32;
  std::size_t maximum_block_size = 64 * 1'024;
  std::size_t maximum_alignment = alignof(std::max_align_t);

  constexpr bool IsValid() const noexcept {
    return std::has_single_bit(minimum_block_size) && std::has_single_bit(maximum_block_size)
           && minimum_block_size >= sizeof(void*) && maximum_block_size >= minimum_block_size
           && std::has_single_bit(maximum_alignment) && maximum_alignment >= alignof(void*);
  }
};

template<ArenaBlockSourceOptions Options>
concept ValidArenaBlockSourceOptions = Options.IsValid();

template<typename ArenaType>
concept RecoverableArena = requires(ArenaType& arena, std::size_t size, std::size_t alignment) {
  { arena.TryAllocate(size, alignment) } -> std::same_as<std::byte*>;
  { arena.source().max_alignment() } -> std::convertible_to<std::size_t>;
};

// Adapts a caller-owned arena to multi-block users such as HAMT nodes. Released
// power-of-two blocks are retained in exact-size free lists, making rollback
// reusable instead of permanently consuming monotonic arena capacity.
template<RecoverableArena ArenaType, ArenaBlockSourceOptions Options = {}>
requires ValidArenaBlockSourceOptions<Options>
class ArenaBlockSource final {
 private:
  struct FreeBlock final {
    FreeBlock* next = nullptr;
  };

  static constexpr std::size_t kMinimumExponent = std::countr_zero(Options.minimum_block_size);
  static constexpr std::size_t kMaximumExponent = std::countr_zero(Options.maximum_block_size);
  static constexpr std::size_t kClassCount = kMaximumExponent - kMinimumExponent + 1;

  static constexpr std::optional<std::size_t> ClassFor(std::size_t requested) noexcept {
    requested = std::max(requested, sizeof(FreeBlock));
    if (requested > Options.maximum_block_size) {
      return std::nullopt;
    }
    const std::size_t rounded = std::max(Options.minimum_block_size, std::bit_ceil(requested));
    return std::countr_zero(rounded) - kMinimumExponent;
  }

  static constexpr std::size_t SizeForClass(std::size_t size_class) noexcept {
    return Options.minimum_block_size << size_class;
  }

 public:
  static constexpr bool supports_recoverable_failure = true;

  explicit ArenaBlockSource(ArenaType& arena) noexcept : arena_(std::addressof(arena)) {}

  std::size_t max_alignment() const noexcept {
    return std::min(Options.maximum_alignment, static_cast<std::size_t>(arena_->source().max_alignment()));
  }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0 || !std::has_single_bit(alignment) || alignment > max_alignment()) {
      return std::nullopt;
    }
    const auto size_class = ClassFor(size);
    if (!size_class) {
      return std::nullopt;
    }
    const std::size_t block_size = SizeForClass(*size_class);
    if (FreeBlock* const available = free_[*size_class]; available != nullptr) {
      free_[*size_class] = available->next;
      std::destroy_at(available);
      return MemoryBlock{
          .data = reinterpret_cast<std::byte*>(available),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
          .size = block_size,
          .alignment = max_alignment()};
    }
    std::byte* const storage = arena_->TryAllocate(block_size, max_alignment());
    if (storage == nullptr) {
      return std::nullopt;
    }
    return MemoryBlock{.data = storage, .size = block_size, .alignment = max_alignment()};
  }

  void Release(MemoryBlock block) noexcept {
    const auto size_class = ClassFor(block.size);
    if (!size_class || block.data == nullptr || block.size != SizeForClass(*size_class)
        || block.alignment < alignof(FreeBlock)) {
      return;
    }
    auto* const released = std::construct_at(
        reinterpret_cast<FreeBlock*>(block.data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        FreeBlock{.next = free_[*size_class]});
    free_[*size_class] = released;
  }

  ArenaType& arena() const noexcept { return *arena_; }

 private:
  ArenaType* arena_;
  std::array<FreeBlock*, kClassCount> free_{};
};

static_assert(BlockSource<ArenaBlockSource<Arena<>>>);

}  // namespace mbo::memory

#endif  // MBO_MEMORY_ARENA_BLOCK_SOURCE_H_
