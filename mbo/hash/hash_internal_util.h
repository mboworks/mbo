// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MBO_HASH_HASH_INTERNAL_UTIL_H_
#define MBO_HASH_HASH_INTERNAL_UTIL_H_

// IWYU pragma: private, include "mbo/hash/hash.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "mbo/hash/hash_types.h"

#if defined(__GNUC__) || defined(__clang__)
# define MBO_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
# define MBO_FORCE_INLINE __forceinline
#else
# define MBO_FORCE_INLINE inline
#endif

namespace mbo::hash::hash_internal {

// NOLINTBEGIN(*-magic-numbers,*-pointer-arithmetic,*-identifier-naming)

// Final mixer (MurmurHash3 `fmix64`) that spreads entropy across all 64 bits.
MBO_FORCE_INLINE constexpr uint64_t Fmix64(uint64_t val) noexcept {
  val ^= val >> 33U;
  val *= 0xff51afd7ed558ccdULL;
  val ^= val >> 33U;
  val *= 0xc4ceb9fe1a85ec53ULL;
  val ^= val >> 33U;
  return val;
}

// Loads 8 bytes as a **little-endian** `uint64_t`, endian-independently, so hash
// values match across platforms.
//
// At run time on little-endian targets this is a plain `memcpy`, which every
// compiler folds into a single load. The byte-assembly loop remains for constant
// evaluation (where `memcpy` is not allowed) and for big-endian targets. Clang
// folds the loop into a single load as well, but gcc does NOT (verified on
// gcc 13: 8x movzx+or, ~3x slower hashing) -- hence the explicit `memcpy` path.
// Both paths produce identical values; the ConstexprMatchesRuntime test guards
// this equality.
MBO_FORCE_INLINE constexpr uint64_t Load64(const char* ptr) noexcept {
  // Coverage runs on little-endian machines and cannot observe compile-time
  // execution. GCC nevertheless emits a branch pair for both alternatives at
  // every always-inline call site. ConstexprMatchesRuntime and the direct load
  // tests cover their behavior without counting those unreachable records.
  if (!std::is_constant_evaluated()) {  // LCOV_EXCL_BR_LINE
    if constexpr (std::endian::native == std::endian::little) {
      uint64_t result = 0;
      std::memcpy(&result, ptr, 8);
      return result;
    }
  }
  uint64_t result = 0;
  for (std::size_t i = 0; i < 8; ++i) {  // LCOV_EXCL_LINE
    result |= static_cast<uint64_t>(static_cast<uint8_t>(ptr[i])) << (i * 8U);
  }
  return result;
}

// Loads 4 bytes as a **big-endian** `uint32_t`. The message-digest
// specifications (SHA-1/SHA-2, FIPS 180-4; see mbo/digest) are big-endian,
// unlike the hash algorithms in this library; the primitive lives here so the
// dual-path byte-load logic exists exactly once. The runtime path is `memcpy`
// plus a byteswap on little-endian targets (same rationale as `Load64`).
MBO_FORCE_INLINE constexpr uint32_t Load32BE(const char* ptr) noexcept {
  if (!std::is_constant_evaluated()) {
    uint32_t result = 0;
    std::memcpy(&result, ptr, 4);
    if constexpr (std::endian::native == std::endian::little) {
      result = std::byteswap(result);
    }
    return result;
  }
  return (static_cast<uint32_t>(static_cast<uint8_t>(ptr[0])) << 24U)
         | (static_cast<uint32_t>(static_cast<uint8_t>(ptr[1])) << 16U)
         | (static_cast<uint32_t>(static_cast<uint8_t>(ptr[2])) << 8U)
         | static_cast<uint32_t>(static_cast<uint8_t>(ptr[3]));
}

// Loads 8 bytes as a **big-endian** `uint64_t` (same rationale as `Load32BE`;
// used by the 64-bit-word digest specifications, e.g. SHA-512).
MBO_FORCE_INLINE constexpr uint64_t Load64BE(const char* ptr) noexcept {
  if (!std::is_constant_evaluated()) {
    uint64_t result = 0;
    std::memcpy(&result, ptr, 8);
    if constexpr (std::endian::native == std::endian::little) {
      result = std::byteswap(result);
    }
    return result;
  }
  uint64_t result = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    result = (result << 8U) | static_cast<uint64_t>(static_cast<uint8_t>(ptr[i]));
  }
  return result;
}

// Loads 4 bytes as a **little-endian** `uint32_t` (same rationale as `Load64`).
MBO_FORCE_INLINE constexpr uint32_t Load32(const char* ptr) noexcept {
  // See Load64: GCC duplicates these unreachable constexpr/big-endian branch
  // records for every always-inline call site.
  if (!std::is_constant_evaluated()) {  // LCOV_EXCL_BR_LINE
    if constexpr (std::endian::native == std::endian::little) {
      uint32_t result = 0;
      std::memcpy(&result, ptr, 4);
      return result;
    }
  }
  uint32_t result = 0;
  for (std::size_t i = 0; i < 4; ++i) {  // LCOV_EXCL_LINE
    result |= static_cast<uint32_t>(static_cast<uint8_t>(ptr[i])) << (i * 8U);
  }
  return result;
}

// Loads the 1..8 remaining tail bytes as a little-endian `uint64_t` without any
// out-of-bounds reads.
//
// The runtime path for >= 4 bytes uses two overlapping 4-byte loads instead of a
// per-byte loop: `lo` covers bytes [0..3], `hi << ((n-4)*8)` covers bytes
// [n-4..n-1], and in the overlap both operands carry the same byte at the same
// bit position, so the OR reproduces the exact little-endian value (the
// canonical known-answer tests verify path equality).
MBO_FORCE_INLINE constexpr uint64_t LoadTail(const char* ptr, std::size_t remaining) noexcept {
  if (!std::is_constant_evaluated()) {
    if constexpr (std::endian::native == std::endian::little) {
      if (remaining >= 4) {
        const uint64_t low = Load32(ptr);
        const uint64_t high = Load32(ptr + remaining - 4);
        return low | (high << ((remaining - 4) * 8U));
      }
    }
  }
  uint64_t result = 0;
  for (std::size_t i = 0; i < remaining; ++i) {
    result |= static_cast<uint64_t>(static_cast<uint8_t>(ptr[i])) << (i * 8U);
  }
  return result;
}

// Folds the two 64-bit lanes into one (offsetting `h2` so equal lanes do not
// cancel) and finalizes the result.
MBO_FORCE_INLINE constexpr uint64_t Hash128To64(Hash128 hash) noexcept {
  const uint64_t combined = hash.h1 ^ (hash.h2 + 0x9e3779b97f4a7c15ULL);
  return Fmix64(combined);
}

#if defined(__cpp_lib_int_128)
using mbo_uint128_t = std::uint128_t;
inline constexpr bool has_128_bit = true;
#elif defined(__SIZEOF_INT128__)
// __extension__: the compiler advertised __int128 via __SIZEOF_INT128__, so using
// it is deliberate; the keyword keeps -Wpedantic quiet at exactly this one site.
__extension__ typedef unsigned __int128 mbo_uint128_t;  // NOLINT(modernize-use-using): __extension__ needs typedef.
inline constexpr bool has_128_bit = true;
#else
// Fallback to 64-bit if compiling for a 32-bit system
using mbo_uint128_t = void;  // Placeholder type, not usable
inline constexpr bool has_128_bit = false;
#endif

// Full 64x64->128 multiply; returns the low half in `h1` and the high half in
// `h2`. Uses `__uint128_t` where the compiler provides it (gcc/clang,
// constexpr-legal); the portable 32-bit schoolbook multiply otherwise.
MBO_FORCE_INLINE constexpr Hash128 Mult128(uint64_t lhs, uint64_t rhs) noexcept {
  if constexpr (has_128_bit) {
    const auto product = static_cast<mbo_uint128_t>(lhs) * static_cast<mbo_uint128_t>(rhs);
    return {.h1 = static_cast<uint64_t>(product), .h2 = static_cast<uint64_t>(product >> 64U)};
  } else {
    const uint64_t lo_lo = (lhs & 0xFFFFFFFFULL) * (rhs & 0xFFFFFFFFULL);
    const uint64_t hi_lo = (lhs >> 32U) * (rhs & 0xFFFFFFFFULL);
    const uint64_t lo_hi = (lhs & 0xFFFFFFFFULL) * (rhs >> 32U);
    const uint64_t hi_hi = (lhs >> 32U) * (rhs >> 32U);
    const uint64_t cross = (lo_lo >> 32U) + (hi_lo & 0xFFFFFFFFULL) + lo_hi;
    return {
        .h1 = (cross << 32U) | (lo_lo & 0xFFFFFFFFULL),
        .h2 = (hi_lo >> 32U) + (cross >> 32U) + hi_hi,
    };
  }
}

// Full 64x64->128 multiply folded to 64 bits by XORing the halves (the core
// mixer of the xxh3/wyhash algorithm family). Uses `__uint128_t` where the
// compiler provides it (gcc/clang, constexpr-legal); the portable 32-bit
// schoolbook multiply otherwise.
MBO_FORCE_INLINE constexpr uint64_t Mul128Fold64(uint64_t lhs, uint64_t rhs) noexcept {
  if constexpr (has_128_bit) {
    const auto product = static_cast<mbo_uint128_t>(lhs) * static_cast<mbo_uint128_t>(rhs);
    return static_cast<uint64_t>(product) ^ static_cast<uint64_t>(product >> 64U);
  } else {
    const uint64_t lo_lo = (lhs & 0xFFFFFFFFULL) * (rhs & 0xFFFFFFFFULL);
    const uint64_t hi_lo = (lhs >> 32U) * (rhs & 0xFFFFFFFFULL);
    const uint64_t lo_hi = (lhs & 0xFFFFFFFFULL) * (rhs >> 32U);
    const uint64_t hi_hi = (lhs >> 32U) * (rhs >> 32U);
    const uint64_t cross = (lo_lo >> 32U) + (hi_lo & 0xFFFFFFFFULL) + lo_hi;
    const uint64_t upper = (hi_lo >> 32U) + (cross >> 32U) + hi_hi;
    const uint64_t lower = (cross << 32U) | (lo_lo & 0xFFFFFFFFULL);
    return lower ^ upper;
  }
}

// Loads the (0..16 byte) small-key input into two words; see the structure
// notes in the header comment.
struct SmallInput {
  uint64_t a = 0;
  uint64_t b = 0;
};

MBO_FORCE_INLINE constexpr SmallInput LoadSmall(const char* ptr, std::size_t len) noexcept {
  // NOLINTNEXTLINE(readability-identifier-length): byte-widening helper.
  const auto u8 = [](char chr) constexpr { return static_cast<uint64_t>(static_cast<uint8_t>(chr)); };
  // If-ladder, common 4..16 range gated first: the dense switch compiled to a
  // jump table (indirect branch + table load) that dominated the small-key
  // cost; here 9..16 (the bulk of hashed string keys, and of the SSO range)
  // resolve on the first compare. Values are byte-identical to the previous
  // switch at every length.
  if (len >= 4) {
    if (len >= 9) {  // 9..16: two 64-bit loads overlapping the end.
      return {.a = Load64(ptr), .b = Load64(ptr + len - 8)};
    }
    if (len == 8) {
      const uint64_t val = Load64(ptr);
      return {.a = val, .b = val};
    }
    return {.a = Load32(ptr), .b = Load32(ptr + len - 4)};  // 4..7 (len 4 -> both equal)
  }
  if (len == 3) {
    const uint64_t val = (u8(ptr[0]) << 45U) | (u8(ptr[1]) << 8U) | u8(ptr[2]);
    return {.a = val, .b = val};
  }
  if (len == 2) {
    const uint64_t val = (u8(ptr[0]) << 45U) | (u8(ptr[1]) << 8U) | u8(ptr[0]);
    return {.a = val, .b = val};
  }
  if (len == 1) {
    const uint64_t val = u8(ptr[0]);
    return {.a = (val << 45U) | val, .b = (val << 45U) | val};
  }
  return {.a = 0, .b = 0};
}

// NOLINTEND(*-magic-numbers,*-pointer-arithmetic,*-identifier-naming)

}  // namespace mbo::hash::hash_internal

#undef MBO_FORCE_INLINE

#endif  // MBO_HASH_HASH_INTERNAL_UTIL_H_
