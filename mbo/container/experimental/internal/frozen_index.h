// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_INDEX_H_
#define MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_INDEX_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>

#include "mbo/config/require.h"
#include "mbo/container/experimental/frozen_options.h"

namespace mbo::container::experimental::frozen_internal {

// The index knows only hashes and element positions. Both frozen and future mutable storage can use it.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// bounded index construction and lookup.
template<FrozenOptions Options>
struct FrozenIndex {
  static constexpr std::size_t kCapacity = Options.capacity;
  static constexpr std::size_t kSlots = Options.slots == 0 ? 2 * kCapacity : Options.slots;
  static constexpr std::uint32_t kEmpty = std::numeric_limits<std::uint32_t>::max();
  static constexpr std::uint32_t kDirect = std::uint32_t{1} << 31;
  using Hashes = std::array<std::uint64_t, kCapacity>;

  std::array<std::uint32_t, kSlots> slots{};
  std::array<std::uint32_t, kCapacity> seeds{};

  constexpr FrozenIndex() noexcept {
    slots.fill(kEmpty);
    seeds.fill(kEmpty);
  }

  static constexpr void Spend(std::size_t& work) {
    MBO_CONFIG_REQUIRE(work < Options.max_work, "Frozen construction work budget exhausted");
    ++work;
  }

  static constexpr std::size_t Primary(std::uint64_t hash) noexcept {
    return hash % std::max<std::size_t>(1, kCapacity);
  }

  static constexpr std::size_t Displaced(std::uint64_t hash, std::uint32_t seed) noexcept {
    return Mix(hash + (0x9e3779b97f4a7c15ULL * (std::uint64_t{seed} + 1))) % std::max<std::size_t>(1, kSlots);
  }

  constexpr std::size_t Bucket(std::uint64_t hash) const noexcept {
    if constexpr (kSlots == 0 || kCapacity == 0) {
      return 0;
    } else {
      const auto seed = seeds[Primary(hash)];
      if (seed == kEmpty) {
        return hash % kSlots;
      }
      return (seed & kDirect) != 0 ? seed & ~kDirect : Displaced(hash, seed);
    }
  }

  constexpr void Build(const Hashes& hashes, std::size_t size, std::size_t& work) {
    slots.fill(kEmpty);
    seeds.fill(kEmpty);
    if (size == 0) {
      return;
    }
    Scratch scratch;
    for (std::size_t index = 0; index < size; ++index) {
      const auto bucket = Primary(hashes[index]);
      scratch.next[index] = scratch.heads[bucket];
      scratch.heads[bucket] = static_cast<std::uint32_t>(index);
      ++scratch.counts[bucket];
    }
    std::array<std::uint32_t, kCapacity> order{};
    std::size_t bucket_count = 0;
    for (std::size_t index = 0; index < kCapacity; ++index) {
      if (scratch.counts[index] != 0) {
        order[bucket_count++] = static_cast<std::uint32_t>(index);
      }
    }
    const auto active_buckets = std::span(order).first(bucket_count);
    std::ranges::sort(active_buckets, [&scratch](auto lhs, auto rhs) {
      return scratch.counts[lhs] == scratch.counts[rhs] ? lhs < rhs : scratch.counts[lhs] > scratch.counts[rhs];
    });
    std::size_t free_slot = 0;
    for (const auto bucket : active_buckets) {
      Place(hashes, scratch, bucket, free_slot, work);
    }
    // Verify occupied slots themselves: distinct full hashes alone are insufficient.
    for (std::size_t index = 0; index < size; ++index) {
      MBO_CONFIG_REQUIRE(slots[Bucket(hashes[index])] == index, "Frozen occupied-slot verification failed");
    }
  }

 private:
  struct Scratch {
    std::array<std::uint32_t, kCapacity> heads{};
    std::array<std::uint32_t, kCapacity> next{};
    std::array<std::uint32_t, kCapacity> counts{};
    std::array<std::uint32_t, kCapacity> trial{};

    constexpr Scratch() noexcept { heads.fill(kEmpty); }
  };

  constexpr bool TrySeed(
      const Hashes& hashes,
      Scratch& scratch,
      std::uint32_t bucket,
      std::uint32_t seed,
      std::size_t& work) {
    std::size_t count = 0;
    for (auto index = scratch.heads[bucket]; index != kEmpty; index = scratch.next[index]) {
      Spend(work);
      const auto slot = Displaced(hashes[index], seed);
      if (slots[slot] != kEmpty) {
        return false;
      }
      for (std::size_t previous = 0; previous < count; ++previous) {
        Spend(work);
        if (scratch.trial[previous] == slot) {
          return false;
        }
      }
      scratch.trial[count++] = static_cast<std::uint32_t>(slot);
    }
    std::size_t pos = 0;
    for (auto index = scratch.heads[bucket]; index != kEmpty; index = scratch.next[index]) {
      slots[scratch.trial[pos++]] = index;
    }
    seeds[bucket] = seed;
    return true;
  }

  constexpr void Place(
      const Hashes& hashes,
      Scratch& scratch,
      std::uint32_t bucket,
      std::size_t& free_slot,
      std::size_t& work) {
    if (scratch.counts[bucket] == 1) {
      while (slots[free_slot] != kEmpty) {
        ++free_slot;
      }
      seeds[bucket] = kDirect | static_cast<std::uint32_t>(free_slot);
      slots[free_slot] = scratch.heads[bucket];
      return;
    }
    for (std::uint32_t seed = 0; seed < Options.max_seed; ++seed) {
      if (TrySeed(hashes, scratch, bucket, seed, work)) {
        return;
      }
    }
    MBO_CONFIG_REQUIRE(false, "Frozen perfect hash seed search exhausted; increase slots or max_seed");
  }
};

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

}  // namespace mbo::container::experimental::frozen_internal

#endif  // MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_INDEX_H_
