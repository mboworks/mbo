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

#ifndef MBO_HASH_HASH_TEMBO_H_
#define MBO_HASH_HASH_TEMBO_H_

// IWYU pragma: private, include "mbo/hash/hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mbo/hash/hash_internal_util.h"
#include "mbo/hash/hash_types.h"

namespace mbo::hash::tembo {

// NOLINTBEGIN(*-magic-numbers,*-pointer-arithmetic,*-constant-array-index,*-easily-swappable-parameters,readability-identifier-length,*-avoid-unchecked-container-access)

inline constexpr uint64_t kDefaultSeed = ::mbo::hash::kDefaultSeed;

namespace tembo_internal {

using hash_internal::Load64;
using hash_internal::Mul128Fold64;
using hash_internal::Mult128;

// The below constants are the 64-bit fractional parts of the square roots of the first prime numbers,
// which are the standard "nothing-up-my-sleeve" numbers used in cryptographic hash functions like
// SHA-512 (primes 1–8) and SHA-384 (primes 9–16). We skip all even results as they are not useful.
inline constexpr std::array<uint64_t, 40> kSecretCodes = {
    0x6A09E667F3BCC909,  // Prime   2
    0xBB67AE8584CAA73B,  // Prime   3
    0x3C6EF372FE94F82B,  // Prime   5
    0xA54FF53A5F1D36F1,  // Prime   7
    0x510E527FADE682D1,  // Prime  11
    0x9B05688C2B3E6C1F,  // Prime  13
    0x1F83D9ABFB41BD6B,  // Prime  17
    0x5BE0CD19137E2179,  // Prime  19
    0xCBBB9D5DC1059ED9,  // Prime  23
    0x629A292A367CD50D,  // Prime  29
    0x9159015A3070DD17,  // Prime  31
    0x152FECD8F70E5939,  // Prime  37
    0x67332667FFC00B31,  // Prime  41
    0x8EB44A8768581511,  // Prime  43
    0xDB0C2E0D64F98FA7,  // Prime  47
    // 0x47B5481DBEFA4FA4,  // Prime  53 skipped: even
    // 0xCA60C24F5A7316C4,  // Prime  53 skipped: even
    // 0xD6A1D877B133A92A,  // Prime  61 skipped: even
    0x14A21A297A13B45B,  // Prime  67
    0x27B70A8546A14091,  // Prime  71
    // 0x2E1B21385C22722C,  // Prime  73 skipped: even
    // 0x4D20A101C0B3921E,  // Prime  79 skipped: even
    0x521A1A86A8D2A111,  // Prime  83
    // 0x7A2D73403A2E8098,  // Prime  89 skipped: even
    0x913A17B805C42171,  // Prime  97
    0x9B1A28A61C2053E9,  // Prime 101
    // 0xB0C383173167123A,  // Prime 103 skipped: even
    0xB8429B4D5D4E8193,  // Prime 107
    // 0xC3A1A2750C0C43B2,  // Prime 109 skipped: even
    // 0xD3B7A812C4653A2C,  // Prime 113 skipped: even
    // 0xE1C09A184D7183B0,  // Prime 127 skipped: even
    // 0xE7A927C15F2E1410,  // Prime 131 skipped: even
    0xF3B971485906D9E7,  // Prime 137
    0x0283E1A639B0C399,  // Prime 139
    0x22ADCE0A2126AD69,  // Prime 149
    0x2C1014382DF863B7,  // Prime 151
    0x4E74711D7B1B6195,  // Prime 157
    0x618E4E73A6C8BA15,  // Prime 163
    0x789230190539E111,  // Prime 167
    0x90993952D1FE8B05,  // Prime 173
    0xAF17E6B1D0E31C01,  // Prime 179
    0xC7289568B309A239,  // Prime 181
    0xD3033F531B5467E3,  // Prime 191
    0xF1E2D75102AD2B89,  // Prime 193
    0x0A97E981D92C979D,  // Prime 197
    0x18FCD50B737A47B1,  // Prime 199
    0x303E8AC7337B753D,  // Prime 211
    0x4DC3EF41539D2C3D,  // Prime 223
    0x643B2338A9C1B203,  // Prime 227
    0x43063A1D4EBB6BA5,  // Prime 233
    0x717316A3DF5D26E3,  // Prime 239
};

// True if the first `count` entries of `arr` are pairwise distinct. Guards the
// per-instantiation secret selections below against transcription errors: a
// duplicated index silently weakens mixing (this exact bug shipped once - an
// if-ladder rewrite used the same secret twice).
template<std::size_t N>
constexpr bool FirstEntriesDistinct(const std::array<uint64_t, N>& arr, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      if (arr[i] == arr[j]) {
        return false;
      }
    }
  }
  return true;
}

// The table itself: every secret must be odd (an even multiplier operand
// throws away a low bit in the widening multiply) and pairwise distinct.
static_assert(
    [] constexpr {
      for (const auto& secret_code : kSecretCodes) {
        if ((secret_code & 1U) == 0U) {
          return false;
        }
      }
      return FirstEntriesDistinct(kSecretCodes, kSecretCodes.size());
    }(),
    "kSecretCodes entries must be odd and pairwise distinct");

#if defined(__GNUC__) || defined(__clang__)
# define MBO_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
# define MBO_FORCE_INLINE __forceinline
#else
# define MBO_FORCE_INLINE inline
#endif

template<uint64_t kNumber, uint64_t kMin, uint64_t kMax>
concept NumberInRangeInclusive = requires { requires(kNumber >= kMin && kMax >= kNumber); };

template<uint64_t kNumLanes, uint64_t kMin, uint64_t kMax>
concept NumLanesInRangeInclusive = NumberInRangeInclusive<kNumLanes, kMin, kMax>;

template<uint64_t kNumConsts, uint64_t kMin, uint64_t kMax>
concept NumConstsInRangeInclusive = NumberInRangeInclusive<kNumConsts, kMin, kMax>;

using u64 = uint64_t;  // NOLINT(readability-identifier-naming)

MBO_FORCE_INLINE constexpr u64 MUM2FX(const char* ptr1, u64 xor1, const char* ptr2, u64 xor2) {
  return Mul128Fold64(Load64(ptr1) ^ xor1, Load64(ptr2) ^ xor2);
}

MBO_FORCE_INLINE constexpr u64 MUM2(u64 v0, u64 v1) {
  return Mul128Fold64(v0, v1);
}

MBO_FORCE_INLINE constexpr u64 MUM3(u64 v0, u64 v1, u64 v2) {
  return Mul128Fold64(v0 ^ v2, v1);
}

MBO_FORCE_INLINE constexpr u64 MUM4(u64 v0, u64 v1, u64 v2, u64 v3) {
  return MUM2(v0 ^ v2, v1 ^ v3);
}

MBO_FORCE_INLINE constexpr u64 MUM5(u64 v0, u64 v1, u64 v2, u64 v3, u64 v4) {
  return MUM2(MUM2(v0, v2) ^ v4, v1 ^ v3);
}

MBO_FORCE_INLINE constexpr u64 MUM6(u64 v0, u64 v1, u64 v2, u64 v3, u64 v4, u64 v5) {
  return MUM2(MUM2(v0, v2) ^ v4, MUM2(v1, v3) ^ v5);
}

MBO_FORCE_INLINE constexpr u64 MUM7(u64 v0, u64 v1, u64 v2, u64 v3, u64 v4, u64 v5, u64 v6) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6), MUM2(v1, v3) ^ v5);
}

MBO_FORCE_INLINE constexpr u64 MUM9(u64 v0, u64 v1, u64 v2, u64 v3, u64 v4, u64 v5, u64 v6, u64 v7, u64 v8) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6), MUM2(v1, v3) ^ MUM2(v5, v7) ^ v8);
}

MBO_FORCE_INLINE constexpr u64 MUM10(u64 v0, u64 v1, u64 v2, u64 v3, u64 v4, u64 v5, u64 v6, u64 v7, u64 v8, u64 v9) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6) ^ v8, MUM2(v1, v3) ^ MUM2(v5, v7) ^ v9);
}

MBO_FORCE_INLINE constexpr u64 MUM11(
    u64 v0,
    u64 v1,
    u64 v2,
    u64 v3,
    u64 v4,
    u64 v5,
    u64 v6,
    u64 v7,
    u64 v8,
    u64 v9,
    u64 v10) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6) ^ MUM2(v8, v10), MUM2(v1, v3) ^ MUM2(v5, v7) ^ v9);
}

MBO_FORCE_INLINE constexpr u64 MUM13(
    u64 v0,
    u64 v1,
    u64 v2,
    u64 v3,
    u64 v4,
    u64 v5,
    u64 v6,
    u64 v7,
    u64 v8,
    u64 v9,
    u64 v10,
    u64 v11,
    u64 v12) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6) ^ MUM2(v8, v10) ^ v12, MUM2(v1, v3) ^ MUM2(v5, v7) ^ MUM2(v9, v11));
}

MBO_FORCE_INLINE constexpr u64 MUM14(
    u64 v0,
    u64 v1,
    u64 v2,
    u64 v3,
    u64 v4,
    u64 v5,
    u64 v6,
    u64 v7,
    u64 v8,
    u64 v9,
    u64 v10,
    u64 v11,
    u64 v12,
    u64 v13) {
  return MUM2(MUM2(v0, v2) ^ MUM2(v4, v6) ^ MUM2(v8, v10) ^ v12, MUM2(v1, v3) ^ MUM2(v5, v7) ^ MUM2(v9, v11) ^ v13);
}

MBO_FORCE_INLINE constexpr u64 MUM15(
    u64 v0,
    u64 v1,
    u64 v2,
    u64 v3,
    u64 v4,
    u64 v5,
    u64 v6,
    u64 v7,
    u64 v8,
    u64 v9,
    u64 v10,
    u64 v11,
    u64 v12,
    u64 v13,
    u64 v14) {
  return MUM2(
      MUM2(v0, v2) ^ MUM2(v4, v6) ^ MUM2(v8, v10) ^ MUM2(v12, v14), MUM2(v1, v3) ^ MUM2(v5, v7) ^ MUM2(v9, v11) ^ v13);
}

MBO_FORCE_INLINE constexpr u64 MUM17(
    u64 v0,
    u64 v1,
    u64 v2,
    u64 v3,
    u64 v4,
    u64 v5,
    u64 v6,
    u64 v7,
    u64 v8,
    u64 v9,
    u64 v10,
    u64 v11,
    u64 v12,
    u64 v13,
    u64 v14,
    u64 v15,
    u64 v16) {
  return MUM2(
      MUM2(v0, v2) ^ MUM2(v4, v6) ^ MUM2(v8, v10) ^ MUM2(v12, v14) ^ v16,
      MUM2(v1, v3) ^ MUM2(v5, v7) ^ MUM2(v9, v11) ^ MUM2(v13, v15));
}

}  // namespace tembo_internal

template<uint64_t kNumLanes, uint64_t kNumConsts, bool kSecretLoopInit>
requires(
    tembo_internal::NumLanesInRangeInclusive<kNumLanes, 2, 16>
    && tembo_internal::NumConstsInRangeInclusive<kNumConsts, 3, tembo_internal::kSecretCodes.size()>)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
MBO_FORCE_INLINE constexpr uint64_t GetHash64(std::string_view str, uint64_t seed = kDefaultSeed) noexcept {
  using namespace tembo_internal;
  using namespace hash_internal;

  constexpr uint64_t kLoopBaseConstIndex = 0;
  constexpr uint64_t kIfLadderConstIndex = kLoopBaseConstIndex + kNumLanes;
  constexpr uint64_t kLoopInitConstIndex = kIfLadderConstIndex + kNumLanes;  // Might be unused (kSecretLoopInit)

  // Bulk window of `kNumLanes` lanes each 16 bytes, so 4 lanes * 16 bytes = 64 bytes.
  // Controls the number of registers we need and unltimately whether we get full ILP or run into register starvation.
  constexpr std::size_t kBulkWindow = kNumLanes * 16;

  size_t len = str.size();

  const char* ptr = str.data();

  // Seed mixing is completely independent of length for short keys.
  // Every bit of the seed reliably permutes the state without length-based cancellation.
  seed ^= Mul128Fold64(seed ^ kSecretCodes[0], kSecretCodes[1]);

  if (len <= 16) {
    // 16 optimization that passes SMHasher3 as a short input optimisation.
    // This guaranteeds that we can later back-read 16 bytes at least.
    const SmallInput input = LoadSmall(ptr, len);
    const Hash128 product = Mult128(input.a ^ kSecretCodes[2], input.b ^ seed);
    // Length is injected exactly once here at the very end to protect
    // against trailing zero-byte variations.
    return Mul128Fold64(product.h1 ^ kSecretCodes[3 % kNumConsts], product.h2 ^ len);
  }

  // Bulk tier entry
  if (len >= kBulkWindow) {
    // NOLINTBEGIN(misc-const-correctness): whether a lane is mutated depends on
    // kNumLanes per instantiation; lanes above kNumLanes stay untouched by design.
    [[maybe_unused]] uint64_t lane_0 = seed ^ len;
    [[maybe_unused]] uint64_t lane_1 = seed;
    [[maybe_unused]] uint64_t lane_2 = seed;
    [[maybe_unused]] uint64_t lane_3 = seed;
    [[maybe_unused]] uint64_t lane_4 = seed;
    [[maybe_unused]] uint64_t lane_5 = seed;
    [[maybe_unused]] uint64_t lane_6 = seed;
    [[maybe_unused]] uint64_t lane_7 = seed;
    [[maybe_unused]] uint64_t lane_8 = seed;
    [[maybe_unused]] uint64_t lane_9 = seed;
    [[maybe_unused]] uint64_t lane_a = seed;
    [[maybe_unused]] uint64_t lane_b = seed;
    [[maybe_unused]] uint64_t lane_c = seed;
    [[maybe_unused]] uint64_t lane_d = seed;
    [[maybe_unused]] uint64_t lane_e = seed;
    [[maybe_unused]] uint64_t lane_f = seed;
    // NOLINTEND(misc-const-correctness)
    // Each lane is independent, so the compiler can schedule the four Mul128Fold64 operations concurrently, allowing
    // for better instruction-level parallelism and throughput. This works due to the temp vars.
    // Must use length at least once.
    if constexpr (kSecretLoopInit) {
      lane_0 ^= kSecretCodes[(kLoopInitConstIndex + 0x00) % kNumConsts];
      if constexpr (kNumLanes > 0x01) {
        lane_1 ^= kSecretCodes[(kLoopInitConstIndex + 0x01) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x02) {
        lane_2 ^= kSecretCodes[(kLoopInitConstIndex + 0x02) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x03) {
        lane_3 ^= kSecretCodes[(kLoopInitConstIndex + 0x03) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x04) {
        lane_4 ^= kSecretCodes[(kLoopInitConstIndex + 0x04) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x05) {
        lane_5 ^= kSecretCodes[(kLoopInitConstIndex + 0x05) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x06) {
        lane_6 ^= kSecretCodes[(kLoopInitConstIndex + 0x06) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x07) {
        lane_7 ^= kSecretCodes[(kLoopInitConstIndex + 0x07) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x08) {
        lane_8 ^= kSecretCodes[(kLoopInitConstIndex + 0x08) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x09) {
        lane_9 ^= kSecretCodes[(kLoopInitConstIndex + 0x09) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0a) {
        lane_a ^= kSecretCodes[(kLoopInitConstIndex + 0x0a) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0b) {
        lane_b ^= kSecretCodes[(kLoopInitConstIndex + 0x0b) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0c) {
        lane_c ^= kSecretCodes[(kLoopInitConstIndex + 0x0c) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0d) {
        lane_d ^= kSecretCodes[(kLoopInitConstIndex + 0x0d) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0e) {
        lane_e ^= kSecretCodes[(kLoopInitConstIndex + 0x0e) % kNumConsts];
      }
      if constexpr (kNumLanes > 0x0f) {
        lane_f ^= kSecretCodes[(kLoopInitConstIndex + 0x0f) % kNumConsts];
      }
    }
    constexpr std::array<uint64_t, kNumLanes> kLaneSecrets = [&] constexpr {
      std::array<uint64_t, kNumLanes> secrets;  // NOLINT(*-member-init)
      secrets[0x00] = kSecretCodes[(kLoopBaseConstIndex + 0x00) % kNumConsts];
      if constexpr (kNumLanes > 1) {
        secrets[0x01] = kSecretCodes[(kLoopBaseConstIndex + 0x01) % kNumConsts];
      }
      if constexpr (kNumLanes > 2) {
        secrets[0x02] = kSecretCodes[(kLoopBaseConstIndex + 0x02) % kNumConsts];
      }
      if constexpr (kNumLanes > 3) {
        secrets[0x03] = kSecretCodes[(kLoopBaseConstIndex + 0x03) % kNumConsts];
      }
      if constexpr (kNumLanes > 4) {
        secrets[0x04] = kSecretCodes[(kLoopBaseConstIndex + 0x04) % kNumConsts];
      }
      if constexpr (kNumLanes > 5) {
        secrets[0x05] = kSecretCodes[(kLoopBaseConstIndex + 0x05) % kNumConsts];
      }
      if constexpr (kNumLanes > 6) {
        secrets[0x06] = kSecretCodes[(kLoopBaseConstIndex + 0x06) % kNumConsts];
      }
      if constexpr (kNumLanes > 7) {
        secrets[0x07] = kSecretCodes[(kLoopBaseConstIndex + 0x07) % kNumConsts];
      }
      if constexpr (kNumLanes > 8) {
        secrets[0x08] = kSecretCodes[(kLoopBaseConstIndex + 0x08) % kNumConsts];
      }
      if constexpr (kNumLanes > 9) {
        secrets[0x09] = kSecretCodes[(kLoopBaseConstIndex + 0x09) % kNumConsts];
      }
      if constexpr (kNumLanes > 10) {
        secrets[0x0a] = kSecretCodes[(kLoopBaseConstIndex + 0x0a) % kNumConsts];
      }
      if constexpr (kNumLanes > 11) {
        secrets[0x0b] = kSecretCodes[(kLoopBaseConstIndex + 0x0b) % kNumConsts];
      }
      if constexpr (kNumLanes > 12) {
        secrets[0x0c] = kSecretCodes[(kLoopBaseConstIndex + 0x0c) % kNumConsts];
      }
      if constexpr (kNumLanes > 13) {
        secrets[0x0d] = kSecretCodes[(kLoopBaseConstIndex + 0x0d) % kNumConsts];
      }
      if constexpr (kNumLanes > 14) {
        secrets[0x0e] = kSecretCodes[(kLoopBaseConstIndex + 0x0e) % kNumConsts];
      }
      if constexpr (kNumLanes > 15) {
        secrets[0x0f] = kSecretCodes[(kLoopBaseConstIndex + 0x0f) % kNumConsts];
      }
      return secrets;
    }();
    // With fewer constants than lanes the selection wraps by design; distinctness
    // can only be required for the first min(kNumLanes, kNumConsts) entries.
    static_assert(
        FirstEntriesDistinct(kLaneSecrets, kNumLanes < kNumConsts ? kNumLanes : kNumConsts),
        "bulk-loop lane secrets must not repeat within one constant window");

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while)
    do {
      lane_0 = MUM2FX(ptr + 0, kLaneSecrets[0x00], ptr + 8, lane_0);
      if constexpr (kNumLanes > 0x01) {
        lane_1 = MUM2FX(ptr + 16, kLaneSecrets[0x01], ptr + 24, lane_1);
      }
      if constexpr (kNumLanes > 0x02) {
        lane_2 = MUM2FX(ptr + 32, kLaneSecrets[0x02], ptr + 40, lane_2);
      }
      if constexpr (kNumLanes > 0x03) {
        lane_3 = MUM2FX(ptr + 48, kLaneSecrets[0x03], ptr + 56, lane_3);
      }
      if constexpr (kNumLanes > 0x04) {  // TODO(helly25): consider reordering 6-4-7-5
        lane_4 = MUM2FX(ptr + 64, kLaneSecrets[0x04], ptr + 72, lane_4);
      }
      if constexpr (kNumLanes > 0x05) {
        lane_5 = MUM2FX(ptr + 80, kLaneSecrets[0x05], ptr + 88, lane_5);
      }
      if constexpr (kNumLanes > 0x06) {
        lane_6 = MUM2FX(ptr + 96, kLaneSecrets[0x06], ptr + 104, lane_6);
      }
      if constexpr (kNumLanes > 0x07) {
        lane_7 = MUM2FX(ptr + 112, kLaneSecrets[0x07], ptr + 120, lane_7);
      }
      if constexpr (kNumLanes > 0x08) {
        lane_8 = MUM2FX(ptr + 128, kLaneSecrets[0x08], ptr + 136, lane_8);
      }
      if constexpr (kNumLanes > 0x09) {
        lane_9 = MUM2FX(ptr + 144, kLaneSecrets[0x09], ptr + 152, lane_9);
      }
      if constexpr (kNumLanes > 0x0a) {
        lane_a = MUM2FX(ptr + 160, kLaneSecrets[0x0a], ptr + 168, lane_a);
      }
      if constexpr (kNumLanes > 0x0b) {
        lane_b = MUM2FX(ptr + 176, kLaneSecrets[0x0b], ptr + 184, lane_b);
      }
      if constexpr (kNumLanes > 0x0c) {
        lane_c = MUM2FX(ptr + 192, kLaneSecrets[0x0c], ptr + 200, lane_c);
      }
      if constexpr (kNumLanes > 0x0d) {
        lane_d = MUM2FX(ptr + 208, kLaneSecrets[0x0d], ptr + 216, lane_d);
      }
      if constexpr (kNumLanes > 0x0e) {
        lane_e = MUM2FX(ptr + 224, kLaneSecrets[0x0e], ptr + 232, lane_e);
      }
      if constexpr (kNumLanes > 0x0f) {
        lane_f = MUM2FX(ptr + 240, kLaneSecrets[0x0f], ptr + 248, lane_f);
      }

      ptr += kBulkWindow;
      len -= kBulkWindow;
    } while (len >= kBulkWindow);
    if constexpr (kNumLanes == 0x01) {
      seed = lane_0;
    } else if constexpr (kNumLanes == 0x02) {
      seed = MUM2(lane_0, lane_1);
    } else if constexpr (kNumLanes == 0x03) {
      seed = MUM3(lane_0, lane_1, lane_2);
    } else if constexpr (kNumLanes == 0x04) {
      seed = MUM4(lane_0, lane_1, lane_2, lane_3);
    } else if constexpr (kNumLanes == 0x05) {
      seed = MUM5(lane_0, lane_1, lane_2, lane_3, lane_4);
    } else if constexpr (kNumLanes == 0x06) {
      seed = MUM6(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5);
    } else if constexpr (kNumLanes == 0x07) {
      seed = MUM7(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6);
    } else if constexpr (kNumLanes == 0x08) {
      seed = MUM9(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, kSecretCodes[3]);
    } else if constexpr (kNumLanes == 0x09) {
      seed = MUM9(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8);
    } else if constexpr (kNumLanes == 0x0a) {
      seed = MUM10(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9);
    } else if constexpr (kNumLanes == 0x0b) {
      seed = MUM11(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a);
    } else if constexpr (kNumLanes == 0x0c) {
      seed = MUM13(
          lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a, lane_b,
          kSecretCodes[2]);
    } else if constexpr (kNumLanes == 0x0d) {
      seed =
          MUM13(lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a, lane_b, lane_c);
    } else if constexpr (kNumLanes == 0x0e) {
      seed = MUM14(
          lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a, lane_b, lane_c,
          lane_d);
    } else if constexpr (kNumLanes == 0x0f) {
      seed = MUM15(
          lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a, lane_b, lane_c,
          lane_d, lane_e);
    } else if constexpr (kNumLanes == 0x10) {
      seed = MUM17(
          lane_0, lane_1, lane_2, lane_3, lane_4, lane_5, lane_6, lane_7, lane_8, lane_9, lane_a, lane_b, lane_c,
          lane_d, lane_e, lane_f, kSecretCodes[1]);
    }
  }

  // No longer update `ptr` or `len`.
  // If we we were at actual bulk size, then we would use another bulk process step.
  // We support kBulkWindow size minus the final backwards read size.
  if (len > 16) {
    // The first here is `+2` and not `+0`. Otherwise `fambo` fails SMHasher.
    // Technically we need kNumLaneCount - 1, but it is simpler to go up to kLanecount.
    constexpr std::array<uint64_t, kNumLanes> kILSecrets = [&] constexpr {
      std::array<uint64_t, kNumLanes> secrets;  // NOLINT(*-member-init)
      secrets[0x00] = kSecretCodes[(kIfLadderConstIndex + 0x02) % kNumConsts];
      if constexpr (kNumLanes > 1) {
        secrets[0x01] = kSecretCodes[(kIfLadderConstIndex + 0x01) % kNumConsts];
      }
      if constexpr (kNumLanes > 2) {
        secrets[0x02] = kSecretCodes[(kIfLadderConstIndex + 0x00) % kNumConsts];
      }
      if constexpr (kNumLanes > 3) {
        secrets[0x03] = kSecretCodes[(kIfLadderConstIndex + 0x03) % kNumConsts];
      }
      if constexpr (kNumLanes > 4) {
        secrets[0x04] = kSecretCodes[(kIfLadderConstIndex + 0x04) % kNumConsts];
      }
      if constexpr (kNumLanes > 5) {
        secrets[0x05] = kSecretCodes[(kIfLadderConstIndex + 0x05) % kNumConsts];
      }
      if constexpr (kNumLanes > 6) {
        secrets[0x06] = kSecretCodes[(kIfLadderConstIndex + 0x06) % kNumConsts];
      }
      if constexpr (kNumLanes > 7) {
        secrets[0x07] = kSecretCodes[(kIfLadderConstIndex + 0x07) % kNumConsts];
      }
      if constexpr (kNumLanes > 8) {
        secrets[0x08] = kSecretCodes[(kIfLadderConstIndex + 0x08) % kNumConsts];
      }
      if constexpr (kNumLanes > 9) {
        secrets[0x09] = kSecretCodes[(kIfLadderConstIndex + 0x09) % kNumConsts];
      }
      if constexpr (kNumLanes > 10) {
        secrets[0x0a] = kSecretCodes[(kIfLadderConstIndex + 0x0a) % kNumConsts];
      }
      if constexpr (kNumLanes > 11) {
        secrets[0x0b] = kSecretCodes[(kIfLadderConstIndex + 0x0b) % kNumConsts];
      }
      if constexpr (kNumLanes > 12) {
        secrets[0x0c] = kSecretCodes[(kIfLadderConstIndex + 0x0c) % kNumConsts];
      }
      if constexpr (kNumLanes > 13) {
        secrets[0x0d] = kSecretCodes[(kIfLadderConstIndex + 0x0d) % kNumConsts];
      }
      if constexpr (kNumLanes > 14) {
        secrets[0x0e] = kSecretCodes[(kIfLadderConstIndex + 0x0e) % kNumConsts];
      }
      if constexpr (kNumLanes > 15) {
        secrets[0x0f] = kSecretCodes[(kIfLadderConstIndex + 0x0f) % kNumConsts];
      }
      return secrets;
    }();
    // The if-ladder consumes at most kNumLanes - 1 secrets; the same wrap
    // caveat as for the bulk-loop secrets applies.
    static_assert(
        FirstEntriesDistinct(kILSecrets, kNumLanes < kNumConsts ? kNumLanes : kNumConsts),
        "if-ladder secrets must not repeat within one constant window");
    // Maximum number of MUM2FX = lane_count -1
    // Read 1st lane, 16 bytes
    seed = MUM2FX(ptr, kILSecrets[0], ptr + 8, seed);
    if constexpr (kNumLanes > 2) {
      if (len > 32) {
        seed = MUM2FX(ptr + 16, kILSecrets[1], ptr + 24, seed);
        if constexpr (kNumLanes > 3) {
          if (len > 48) {
            seed = MUM2FX(ptr + 32, kILSecrets[2], ptr + 40, seed);
            if constexpr (kNumLanes > 4) {
              if (len > 64) {
                seed = MUM2FX(ptr + 48, kILSecrets[3], ptr + 56, seed);
                if constexpr (kNumLanes > 5) {
                  if (len > 80) {
                    seed = MUM2FX(ptr + 64, kILSecrets[4], ptr + 72, seed);
                    if constexpr (kNumLanes > 6) {
                      if (len > 96) {
                        seed = MUM2FX(ptr + 80, kILSecrets[5], ptr + 88, seed);
                        if constexpr (kNumLanes > 7) {
                          if (len > 112) {
                            seed = MUM2FX(ptr + 96, kILSecrets[6], ptr + 104, seed);
                            if constexpr (kNumLanes > 8) {
                              if (len > 128) {
                                seed = MUM2FX(ptr + 112, kILSecrets[7], ptr + 120, seed);
                                if constexpr (kNumLanes > 9) {
                                  if (len > 144) {
                                    seed = MUM2FX(ptr + 128, kILSecrets[8], ptr + 136, seed);
                                    if constexpr (kNumLanes > 10) {
                                      if (len > 160) {
                                        seed = MUM2FX(ptr + 144, kILSecrets[9], ptr + 152, seed);
                                        if constexpr (kNumLanes > 11) {
                                          if (len > 176) {
                                            seed = MUM2FX(ptr + 160, kILSecrets[0x0a], ptr + 168, seed);
                                            if constexpr (kNumLanes > 12) {
                                              if (len > 192) {
                                                seed = MUM2FX(ptr + 176, kILSecrets[0x0b], ptr + 184, seed);
                                                if constexpr (kNumLanes > 13) {
                                                  if (len > 208) {
                                                    seed = MUM2FX(ptr + 192, kILSecrets[0x0c], ptr + 200, seed);
                                                    if constexpr (kNumLanes > 14) {
                                                      if (len > 224) {
                                                        seed = MUM2FX(ptr + 208, kILSecrets[0x0d], ptr + 216, seed);
                                                        if constexpr (kNumLanes > 15) {
                                                          if (len > 240) {
                                                            seed = MUM2FX(ptr + 224, kILSecrets[0x0e], ptr + 232, seed);
                                                          }
                                                        }
                                                      }
                                                    }
                                                  }
                                                }
                                              }
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // The remaining offset (len) from before the if-ladder is needed for the trailing block computation.
  const uint64_t val_a = Load64(ptr + len - 16) ^ kSecretCodes[0];
  const uint64_t val_b = Load64(ptr + len - 8) ^ seed;

  const Hash128 product = Mult128(val_a, val_b);
  return Mul128Fold64(product.h1 ^ kSecretCodes[1], product.h2 ^ kSecretCodes[2] ^ len);
}

// The algorithm struct (see `mbo::hash::IsHashAlgorithm` in hash.h). Weakly
// seeded: the seed folds into the initial state and rides the MUM chain.
template<uint64_t kNumLanes, uint64_t kNumConsts, bool kSecretLoopInit = false>
requires(
    tembo_internal::NumLanesInRangeInclusive<kNumLanes, 2, 16>
    && tembo_internal::NumConstsInRangeInclusive<kNumConsts, 3, tembo_internal::kSecretCodes.size()>)
struct Algorithm {
  static constexpr uint64_t GetHash64(std::string_view data, uint64_t seed = 0) noexcept {
    return mbo::hash::tembo::GetHash64<kNumLanes, kNumConsts, kSecretLoopInit>(data, seed);
  }
};

// NOLINTEND(*-magic-numbers,*-pointer-arithmetic,*-constant-array-index,*-easily-swappable-parameters,readability-identifier-length,*-avoid-unchecked-container-access)

}  // namespace mbo::hash::tembo

#undef MBO_FORCE_INLINE

#endif  // MBO_HASH_HASH_TEMBO_H_
