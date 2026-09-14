// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_VALUE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_VALUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "mbo/container/internal/hamt_source_domain.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// Separately owned payloads keep addresses stable when packed nodes are copied.
// The payload retains its allocation domain, independently of any container.
template<typename Value, mbo::memory::BlockSource Source>
requires std::is_nothrow_destructible_v<Value>
class HamtNodeValue final {
 private:
  using Domain = HamtSourceDomain<Source>;

  struct Control final {
    template<typename... Args>
    Control(Domain source, mbo::memory::MemoryBlock storage, Args&&... args) noexcept
        : domain(std::move(source)), block(storage), value(std::forward<Args>(args)...) {}

    std::atomic<std::size_t> references{1};
    Domain domain;
    mbo::memory::MemoryBlock block;
    Value value;
  };

 public:
  HamtNodeValue() noexcept = default;

  HamtNodeValue(const HamtNodeValue& other) noexcept : control_(other.control_) {
    if (control_ != nullptr) {
      auto count = control_->references.load(std::memory_order_relaxed);
      do {
        if (count == std::numeric_limits<std::size_t>::max()) {
          std::terminate();
        }
      } while (!control_->references.compare_exchange_weak(count, count + 1, std::memory_order_relaxed));
    }
  }

  HamtNodeValue& operator=(const HamtNodeValue& other) noexcept {
    HamtNodeValue copied(other);
    swap(copied);
    return *this;
  }

  HamtNodeValue(HamtNodeValue&& other) noexcept : control_(std::exchange(other.control_, nullptr)) {}

  HamtNodeValue& operator=(HamtNodeValue&& other) noexcept {
    if (this != std::addressof(other)) {
      HamtNodeValue moved(std::move(other));
      swap(moved);
    }
    return *this;
  }

  ~HamtNodeValue() {
    if (control_ != nullptr && control_->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      auto domain = std::move(control_->domain);
      const auto block = control_->block;
      std::destroy_at(control_);
      domain.get()->Release(block);
    }
  }

  template<typename... Args>
  requires std::is_nothrow_constructible_v<Value, Args...>
  [[nodiscard]] static std::optional<HamtNodeValue> TryCreate(const Domain& domain, Args&&... args) noexcept {
    if (domain.get() == nullptr) {
      return std::nullopt;
    }
    const auto block = domain.get()->TryAcquire(sizeof(Control), alignof(Control));
    if (!block) {
      return std::nullopt;
    }
    if (block->data == nullptr || block->size < sizeof(Control) || block->alignment < alignof(Control)
        || std::bit_cast<std::uintptr_t>(block->data) % alignof(Control) != 0) {
      domain.get()->Release(*block);
      return std::nullopt;
    }
    HamtNodeValue result;
    result.control_ =
        std::construct_at(std::bit_cast<Control*>(block->data), domain, *block, std::forward<Args>(args)...);
    return result;
  }

  // NOLINTBEGIN(readability-identifier-naming): owning handle vocabulary.
  [[nodiscard]] std::optional<Value*> try_get_mutable() noexcept
  requires std::is_nothrow_copy_constructible_v<Value>
  {
    if (control_ == nullptr) {
      return static_cast<Value*>(nullptr);
    }
    if (!is_unique()) {
      auto copied = TryCreate(control_->domain, control_->value);
      if (!copied) {
        return std::nullopt;
      }
      swap(*copied);
    }
    return std::addressof(control_->value);
  }

  const Value* get() const noexcept { return control_ == nullptr ? nullptr : std::addressof(control_->value); }

  bool is_unique() const noexcept {
    return control_ != nullptr && control_->references.load(std::memory_order_acquire) == 1;
  }

  void swap(HamtNodeValue& other) noexcept { std::swap(control_, other.control_); }

  friend void swap(HamtNodeValue& first, HamtNodeValue& second) noexcept { first.swap(second); }

  // NOLINTEND(readability-identifier-naming)

 private:
  Control* control_ = nullptr;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_VALUE_H_
