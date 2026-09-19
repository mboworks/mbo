// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_SOURCE_DOMAIN_H_
#define MBO_CONTAINER_INTERNAL_HAMT_SOURCE_DOMAIN_H_

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// Keeps even nonmovable block sources at a stable address across snapshot copies.
// Containers must destroy their roots before releasing their domain handle.
template<mbo::memory::BlockSource Source>
requires std::is_nothrow_destructible_v<Source>
class HamtSourceDomain final {
 private:
  using ReleaseControl = void (*)(void*, mbo::memory::MemoryBlock) noexcept;

  struct Control final {
    template<typename... Args>
    explicit Control(mbo::memory::MemoryBlock storage, void* owner, ReleaseControl release, Args&&... args) noexcept
        : block(storage), storage_owner(owner), release_storage(release), source(std::forward<Args>(args)...) {}

    std::atomic<std::size_t> references{1};
    mbo::memory::MemoryBlock block;
    void* storage_owner;
    ReleaseControl release_storage;
    Source source;
  };

 public:
  HamtSourceDomain() noexcept = default;

  HamtSourceDomain(const HamtSourceDomain& other) noexcept : control_(other.control_) {
    if (control_ != nullptr) {
      auto count = control_->references.load(std::memory_order_relaxed);
      while (true) {
        if (count == std::numeric_limits<std::size_t>::max()) {
          std::terminate();
        }
        if (control_->references.compare_exchange_weak(count, count + 1, std::memory_order_relaxed)) {
          break;
        }
      }
    }
  }

  HamtSourceDomain& operator=(const HamtSourceDomain& other) noexcept {
    HamtSourceDomain copy(other);
    swap(copy);
    return *this;
  }

  HamtSourceDomain(HamtSourceDomain&& other) noexcept : control_(std::exchange(other.control_, nullptr)) {}

  HamtSourceDomain& operator=(HamtSourceDomain&& other) noexcept {
    if (this != std::addressof(other)) {
      HamtSourceDomain moved(std::move(other));
      swap(moved);
    }
    return *this;
  }

  ~HamtSourceDomain() {
    if (control_ != nullptr && control_->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      const auto block = control_->block;
      auto* const owner = control_->storage_owner;
      const auto release = control_->release_storage;
      std::destroy_at(control_);
      release(owner, block);
    }
  }

  template<typename... Args>
  requires std::is_nothrow_constructible_v<Source, Args...>
  [[nodiscard]] static std::optional<HamtSourceDomain> TryCreate(Args&&... args) noexcept {
    static mbo::memory::NewDeleteBlockSource storage;
    return TryCreateIn(storage, std::forward<Args>(args)...);
  }

  // The control-block source is borrowed and must outlive every domain handle.
  template<mbo::memory::BlockSource ControlSource, typename... Args>
  requires std::is_nothrow_constructible_v<Source, Args...>
  [[nodiscard]] static std::optional<HamtSourceDomain> TryCreateIn(ControlSource& storage, Args&&... args) noexcept {
    const auto block = storage.TryAcquire(sizeof(Control), alignof(Control));
    if (!block) {
      return std::nullopt;
    }
    if (block->data == nullptr || block->size < sizeof(Control) || block->alignment < alignof(Control)
        || std::bit_cast<std::uintptr_t>(block->data) % alignof(Control) != 0) {
      storage.Release(*block);
      return std::nullopt;
    }
    HamtSourceDomain domain;
    void* const control_storage = block->data;
    domain.control_ = std::construct_at(
        static_cast<Control*>(control_storage), *block, std::addressof(storage),
        [](void* owner, mbo::memory::MemoryBlock released) noexcept {
          static_cast<ControlSource*>(owner)->Release(released);
        },
        std::forward<Args>(args)...);
    return domain;
  }

  // NOLINTBEGIN(readability-identifier-naming): STL-style handle accessors.
  [[nodiscard]] Source* get() const noexcept {
    return control_ == nullptr ? nullptr : std::addressof(control_->source);
  }

  void swap(HamtSourceDomain& other) noexcept { std::swap(control_, other.control_); }

  friend void swap(HamtSourceDomain& first, HamtSourceDomain& second) noexcept { first.swap(second); }

  // NOLINTEND(readability-identifier-naming)

 private:
  Control* control_ = nullptr;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_SOURCE_DOMAIN_H_
