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
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
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
  typename std::bool_constant<Source::supports_recoverable_failure>;
  { source.TryAcquire(size, alignment) } -> std::same_as<std::optional<MemoryBlock>>;
  { source.Release(block) } noexcept;
  { source.max_alignment() } noexcept -> std::convertible_to<std::size_t>;
};

template<typename Source>
concept CopyableBlockSource = BlockSource<Source> && requires(const Source& source) {
  { source.CopyForContainer() } -> std::same_as<Source>;
};

// NOLINTBEGIN(readability-identifier-naming): block-source accessors follow allocator/STL spelling.

class NewDeleteBlockSource final {
 public:
  static constexpr bool supports_recoverable_failure = true;

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

  static constexpr NewDeleteBlockSource CopyForContainer() noexcept { return {}; }
};

class FixedBlockSource final {
 public:
  static constexpr bool supports_recoverable_failure = true;

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
  static constexpr bool supports_recoverable_failure = true;

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

template<typename Allocator = std::allocator<std::max_align_t>>
class AllocatorBlockSource final {
 private:
  using Traits = std::allocator_traits<Allocator>;
  using Value = Traits::value_type;

  static_assert(alignof(Value) >= alignof(std::max_align_t));

 public:
#if __cpp_exceptions
  static constexpr bool supports_recoverable_failure = true;
#else
  static constexpr bool supports_recoverable_failure = false;
#endif

  constexpr AllocatorBlockSource() = default;

  constexpr explicit AllocatorBlockSource(Allocator allocator) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
      : allocator_(std::move(allocator)) {}

  static constexpr std::size_t max_alignment() noexcept { return alignof(Value); }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) {
    if (size == 0 || alignment == 0 || alignment > max_alignment() || (alignment & (alignment - 1)) != 0) {
      return std::nullopt;
    }
    const auto count = (size / sizeof(Value)) + (size % sizeof(Value) != 0 ? 1 : 0);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
      return std::nullopt;
    }
#if __cpp_exceptions
    try {
#endif
      auto* const data = Traits::allocate(allocator_, count);
      return MemoryBlock{
          .data = reinterpret_cast<std::byte*>(data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
          .size = count * sizeof(Value),
          .alignment = alignof(Value),
      };
#if __cpp_exceptions
    } catch (const std::bad_alloc&) {
      return std::nullopt;
    }
#endif
  }

  void Release(MemoryBlock block) noexcept {
    const auto count = block.size / sizeof(Value);
    Traits::deallocate(
        allocator_,
        reinterpret_cast<Value*>(block.data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        count);
  }

  constexpr Allocator& allocator() noexcept { return allocator_; }

  constexpr const Allocator& allocator() const noexcept { return allocator_; }

  constexpr AllocatorBlockSource CopyForContainer() const {
    return AllocatorBlockSource(Traits::select_on_container_copy_construction(allocator_));
  }

 private:
  [[no_unique_address]] Allocator allocator_{};
};

class PmrBlockSource final {
 public:
#if __cpp_exceptions
  static constexpr bool supports_recoverable_failure = true;
#else
  static constexpr bool supports_recoverable_failure = false;
#endif

  explicit PmrBlockSource(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) noexcept
      : resource_(resource) {}

  static constexpr std::size_t max_alignment() noexcept {
    return std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
  }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) {
    if (resource_ == nullptr || size == 0 || alignment == 0 || alignment > max_alignment()
        || (alignment & (alignment - 1)) != 0) {
      return std::nullopt;
    }
#if __cpp_exceptions
    try {
#endif
      return MemoryBlock{
          .data = static_cast<std::byte*>(resource_->allocate(size, alignment)),
          .size = size,
          .alignment = alignment,
      };
#if __cpp_exceptions
    } catch (const std::bad_alloc&) {
      return std::nullopt;
    }
#endif
  }

  void Release(MemoryBlock block) noexcept { resource_->deallocate(block.data, block.size, block.alignment); }

  std::pmr::memory_resource* resource() const noexcept { return resource_; }

  PmrBlockSource CopyForContainer() const noexcept { return PmrBlockSource(resource_); }

 private:
  std::pmr::memory_resource* resource_;
};

static_assert(BlockSource<NewDeleteBlockSource>);
static_assert(BlockSource<FixedBlockSource>);
static_assert(BlockSource<InlineBlockSource<4'096>>);
static_assert(BlockSource<AllocatorBlockSource<>>);
static_assert(BlockSource<PmrBlockSource>);
static_assert(CopyableBlockSource<NewDeleteBlockSource>);
static_assert(CopyableBlockSource<AllocatorBlockSource<>>);
static_assert(CopyableBlockSource<PmrBlockSource>);
static_assert(!CopyableBlockSource<FixedBlockSource>);
static_assert(!CopyableBlockSource<InlineBlockSource<4'096>>);

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::memory

#endif  // MBO_MEMORY_BLOCK_SOURCE_H_
