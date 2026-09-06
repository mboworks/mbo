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

#include "mbo/hash/hash.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/hash/hash_test_util.h"

// Shrink the heavy collision test under sanitizers (kept from the original test).
#undef MBO_HASH_TEST_SANITIZER
#if defined(__has_feature)
# if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
#  define MBO_HASH_TEST_SANITIZER
# endif
#elif defined(__SANITIZE_ADDRESS__)
# define MBO_HASH_TEST_SANITIZER
#endif

// Generated known-answer vectors for the in-house hashes (-> mbo::hash::kat).
#include "mbo/hash/hash_test_vectors.inc"  // NOLINT(bugprone-suspicious-include): generated data table

namespace mbo::hash {
namespace {

using ::testing::Eq;
using ::testing::Gt;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Ne;
using ::testing::SizeIs;

// NOLINTBEGIN(*-magic-numbers)

constexpr uint64_t kSeed = 5'381;

// ---------------------------------------------------------------------------
// Templated framework: every test below runs once per hash-algorithm descriptor
// (see hash_test_util.h). Add an algorithm to `HashAlgorithms` to test it.
// ---------------------------------------------------------------------------
template<typename Algo>
class HashTest : public ::testing::Test {};

// Derive the gtest type list from the central algo::AllAlgorithms tuple, so a
// descriptor added there is automatically tested here.
template<typename Tuple>
struct TupleToGtestTypes;

template<typename... Ts>
struct TupleToGtestTypes<std::tuple<Ts...>> {
  using type = ::testing::Types<Ts...>;
};

using HashAlgorithms = TupleToGtestTypes<algo::AllAlgorithms>::type;
TYPED_TEST_SUITE(HashTest, HashAlgorithms);

// Empty fixtures for the non-typed suites below (TEST_F convention).
struct CombineHashesTest : ::testing::Test {};

struct Hash128Test : ::testing::Test {};

struct Hash64To32Test : ::testing::Test {};

struct HasherTest : ::testing::Test {};

struct KnownAnswerTest : ::testing::Test {};

struct MumboHashTest : ::testing::Test {};

struct Murmur3HashTest : ::testing::Test {};

struct StreamerTest : ::testing::Test {};

// The framework detects 64- vs 128-bit algorithms from the descriptor.
TYPED_TEST(HashTest, BitWidthDetection) {
  EXPECT_THAT((algo::kHashBits<TypeParam>), Eq(HasGetHash128<TypeParam> ? 128 : 64));
}

// Compile-time and run-time evaluation must agree. This also guards the loads'
// dual implementation (constexpr byte assembly vs runtime memcpy): the probes
// cover the tiny, single-lane, and multi-stripe input tiers.
TYPED_TEST(HashTest, ConstexprMatchesRuntime) {
  static constexpr std::array<std::string_view, 3> kProbes{
      "abc",
      "constexpr-vs-runtime",
      "a long probe that spans multiple 32-byte stripes of the large-input hashing loop",
  };
  constexpr std::array<uint64_t, 3> kCompile{
      TypeParam::GetHash64(kProbes.at(0), kSeed),
      TypeParam::GetHash64(kProbes.at(1), kSeed),
      TypeParam::GetHash64(kProbes.at(2), kSeed),
  };
  for (std::size_t i = 0; i < kProbes.size(); ++i) {
    std::string_view runtime = kProbes.at(i);  // NOLINT(misc-const-correctness): non-const forces the runtime path
    EXPECT_THAT(kCompile.at(i), Eq(TypeParam::GetHash64(runtime, kSeed))) << "probe: \"" << runtime << "\"";
  }
  if constexpr (HasGetHash128<TypeParam>) {
    constexpr Hash128 kCompile128 = TypeParam::GetHash128(kProbes.at(2), kSeed);
    std::string_view runtime = kProbes.at(2);  // NOLINT(misc-const-correctness): non-const forces the runtime path
    EXPECT_THAT(kCompile128, Eq(TypeParam::GetHash128(runtime, kSeed)));
  }
}

TYPED_TEST(HashTest, EmptyEqualsNullAndIsNontrivial) {
  const uint64_t empty = TypeParam::GetHash64(std::string_view(""), kSeed);
  const uint64_t null_view = TypeParam::GetHash64(std::string_view(), kSeed);
  EXPECT_THAT(empty, Eq(null_view));
  EXPECT_THAT(empty, Ne(uint64_t{0}));
  EXPECT_THAT(empty, Ne(~uint64_t{0}));
}

TYPED_TEST(HashTest, DistinctForDistinctShortInputs) {
  constexpr std::array<std::string_view, 10> kData{
      "1", "12", "123", "1234", "12345", "123456", "1234567", "12345678", "123456789", "1234567890",
  };
  std::set<uint64_t> hashes;
  for (const std::string_view data : kData) {
    hashes.insert(TypeParam::GetHash64(data, kSeed));
  }
  EXPECT_THAT(hashes, SizeIs(kData.size()));
}

TYPED_TEST(HashTest, LowCollisionRate) {
  std::mt19937_64 rng(::testing::UnitTest::GetInstance()->random_seed());
#ifdef MBO_HASH_TEST_SANITIZER
  constexpr std::size_t kNumStrings = 200'000;
#else   // MBO_HASH_TEST_SANITIZER
  constexpr std::size_t kNumStrings = 2'000'000;
#endif  // MBO_HASH_TEST_SANITIZER
  std::set<std::string> inputs;
  while (inputs.size() < kNumStrings) {
    inputs.insert(algo::RandomString(rng, 30));
  }
  std::set<uint64_t> hashes;
  for (const std::string& input : inputs) {
    hashes.insert(TypeParam::GetHash64(input, kSeed));
  }
  EXPECT_THAT(inputs.size() - hashes.size(), Le(kNumStrings / 2'000'000));
}

// Avalanche: flipping a single input bit should flip ~half the output bits.
// Lengths chosen to exercise every input tier: tail-only (4), single-lane loop
// (12), stripes + remainder (100), and multiple stripes (300).
// NOLINTNEXTLINE(readability-function-cognitive-complexity): nested per-length trial loops read clearer inline.
TYPED_TEST(HashTest, Avalanche) {
  std::mt19937_64 rng(0xA5A5A5A5U);  // NOLINT(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): fixed seed
                                     // keeps this reproducible
  constexpr int kTrials = 8'000;
  for (const std::size_t length : std::array<std::size_t, 4>{4, 12, 100, 300}) {
    std::size_t flipped = 0;
    for (int trial = 0; trial < kTrials; ++trial) {
      const std::string base = algo::RandomString(rng, length);
      const uint64_t hash0 = TypeParam::GetHash64(base, kSeed);
      const std::size_t bit = rng() % (base.size() * 8);
      std::string mutated = base;
      const unsigned mask = 1U << (bit % 8U);
      mutated.at(bit / 8) =
          static_cast<char>(static_cast<unsigned>(static_cast<unsigned char>(mutated.at(bit / 8))) ^ mask);
      const uint64_t hash1 = TypeParam::GetHash64(mutated, kSeed);
      flipped += static_cast<std::size_t>(std::popcount(hash0 ^ hash1));
    }
    const double ratio = static_cast<double>(flipped) / (static_cast<double>(kTrials) * 64.0);
    if constexpr (TypeParam::kStrongAvalanche) {
      EXPECT_THAT(ratio, Gt(0.45)) << "len=" << length << " weak avalanche: " << ratio;
      EXPECT_THAT(ratio, Lt(0.55)) << "len=" << length << " biased avalanche: " << ratio;
    } else {
      // Weak algorithms only need to react at all; e.g. `dumbo` sits at ~0.07
      // for 4-byte inputs (a documented weakness of its short-input handling).
      EXPECT_THAT(ratio, Gt(0.05)) << "len=" << length << " barely reacts: " << ratio;
    }
  }
}

// Seed avalanche (SMHasher-style): flipping a single *seed* bit should flip
// ~half the output bits. Skipped for seedless algorithms; weak-avalanche
// algorithms only need to react (fnv1a's multiplies diffuse upward only, so
// high seed bits move few output bits).
// NOLINTNEXTLINE(readability-function-cognitive-complexity): nested per-length trial loops read clearer inline.
TYPED_TEST(HashTest, SeedAvalanche) {
  if constexpr (!TypeParam::kSeeded) {
    GTEST_SKIP() << TypeParam::Name() << " ignores the seed";
  } else {
    std::mt19937_64 rng(0x5EED5EEDU);  // NOLINT(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): fixed seed
                                       // keeps this reproducible
    constexpr int kTrials = 8'000;
    for (const std::size_t length : std::array<std::size_t, 4>{4, 12, 100, 300}) {
      std::size_t flipped = 0;
      for (int trial = 0; trial < kTrials; ++trial) {
        const std::string data = algo::RandomString(rng, length);
        const uint64_t seed = rng();
        const uint64_t seed_bit = uint64_t{1} << (rng() % 64U);
        flipped += static_cast<std::size_t>(
            std::popcount(TypeParam::GetHash64(data, seed) ^ TypeParam::GetHash64(data, seed ^ seed_bit)));
      }
      const double ratio = static_cast<double>(flipped) / (static_cast<double>(kTrials) * 64.0);
      if constexpr (TypeParam::kStrongAvalanche) {
        EXPECT_THAT(ratio, Gt(0.45)) << "len=" << length << " weak seed avalanche: " << ratio;
        EXPECT_THAT(ratio, Lt(0.55)) << "len=" << length << " biased seed avalanche: " << ratio;
      } else {
        EXPECT_THAT(ratio, Gt(0.05)) << "len=" << length << " seed barely reacts: " << ratio;
      }
    }
  }
}

// Structured/sparse keys (SMHasher-inspired): degenerate input families where
// weak mixing hides from random-string tests. All inputs are pairwise distinct,
// so a 64-bit hash is expected to produce zero collisions across the ~1200 keys
// (birthday bound ~2^-44).
TYPED_TEST(HashTest, StructuredKeysAreDistinct) {
  std::set<std::string> inputs;
  // All-zero inputs of every length 0..256: only the length distinguishes them.
  for (std::size_t len = 0; len <= 256; ++len) {
    inputs.insert(std::string(len, '\0'));
  }
  // Single-bit keys: exactly one bit set, at every position.
  for (const std::size_t len : std::array<std::size_t, 2>{16, 64}) {
    for (std::size_t bit = 0; bit < len * 8; ++bit) {
      std::string key(len, '\0');
      key.at(bit / 8) = static_cast<char>(1U << (bit % 8U));
      inputs.insert(std::move(key));
    }
  }
  // Cyclic patterns: short periods repeated to a fixed length.
  for (std::size_t period = 1; period <= 16; ++period) {
    std::string pattern;
    for (std::size_t i = 0; i < 48; ++i) {
      pattern.push_back(static_cast<char>('a' + (i % period)));
    }
    inputs.insert(std::move(pattern));
  }
  std::set<uint64_t> hashes;
  for (const std::string& input : inputs) {
    hashes.insert(TypeParam::GetHash64(input, kSeed));
  }
  EXPECT_THAT(hashes, SizeIs(inputs.size())) << "collisions among structured keys";
}

// Streaming: any chunking of the input must produce exactly the one-shot
// value. Lengths cross every dispatch tier; split points are random,
// including empty chunks. Skipped for algorithms without streaming support.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): random re-chunking trial loops read clearer inline.
TYPED_TEST(HashTest, StreamingMatchesOneShot) {
  if constexpr (!HasStreaming<TypeParam>) {
    GTEST_SKIP() << TypeParam::Name() << " has no streaming form";
  } else {
    // NOLINTNEXTLINE(cert-msc51-cpp,cert-msc32-c,bugprone-random-generator-seed): reproducible
    std::mt19937_64 rng(0x57AE0A1U);
    for (const std::size_t length : std::array<std::size_t, 8>{0, 5, 12, 32, 33, 64, 96, 300}) {
      const std::string data = algo::RandomString(rng, length);
      const uint64_t expected = TypeParam::GetHash64(data, kSeed);
      for (int trial = 0; trial < 50; ++trial) {
        Streamer<TypeParam> stream(kSeed);
        std::size_t pos = 0;
        while (pos < data.size()) {
          const std::size_t chunk = rng() % (data.size() - pos + 2);  // may be 0 or overshoot-clamped
          stream.Update(std::string_view(data).substr(pos, chunk));
          pos += std::min(chunk, data.size() - pos);
        }
        ASSERT_THAT(stream.Finalize(), Eq(expected)) << TypeParam::Name() << " len=" << length << " trial=" << trial;
      }
      // Byte-at-a-time, and everything-at-once.
      Streamer<TypeParam> byte_stream(kSeed);
      for (const char chr : data) {
        byte_stream.Update(std::string_view(&chr, 1));
      }
      EXPECT_THAT(byte_stream.Finalize(), Eq(expected)) << TypeParam::Name() << " len=" << length << " (byte-wise)";
      EXPECT_THAT(Streamer<TypeParam>(kSeed).Update(data).Finalize(), Eq(expected)) << TypeParam::Name();
    }
  }
}

// Streaming support is intentional per algorithm: canonical forms exist for
// xxh64 and siphash, mh defines its own; rapidhash has no canonical streaming.
static_assert(std::same_as<DefaultHashAlgorithm, mumbo::Algorithm>);
static_assert(std::same_as<Default128HashAlgorithm, jumbo::Algorithm>);

static_assert(HasStreaming<mumbo::Algorithm>);
static_assert(HasStreaming<xxh64::Algorithm>);
static_assert(HasStreaming<siphash::Algorithm>);
static_assert(!HasStreaming<rapidhash::Algorithm>);
static_assert(!HasStreaming<fnv1a::Algorithm> && !HasStreaming<dumbo::Algorithm>);
static_assert(!HasStreaming<xxh3::Algorithm> && !HasStreaming<murmur3::Algorithm>);

TEST_F(StreamerTest, NonDestructiveFinalizeAndConstexpr) {
  Streamer<mumbo::Algorithm> stream;
  stream.Update("part1-");
  const uint64_t partial = stream.Finalize();
  EXPECT_THAT(partial, Eq(mumbo::GetHash64("part1-")));
  stream.Update("part2");  // Continue after peeking.
  EXPECT_THAT(stream.Finalize(), Eq(mumbo::GetHash64("part1-part2")));
  // Fully constexpr streaming.
  constexpr uint64_t kStreamed = Streamer<siphash::Algorithm>(7).Update("const").Update("expr").Finalize();
  static_assert(kStreamed == siphash::Algorithm::GetHash64("constexpr", 7));
  EXPECT_THAT(kStreamed, Eq(siphash::Algorithm::GetHash64("constexpr", 7)));
}

// 128-bit-only checks; skipped for 64-bit-based algorithms via the detection trait.
// How GetHash64 relates to GetHash128 is algorithm-specific (mh folds, murmur3 truncates)
// and covered by per-algorithm tests below.
TYPED_TEST(HashTest, Hash128) {
  if constexpr (HasGetHash128<TypeParam>) {
    EXPECT_THAT(TypeParam::GetHash128("abc", kSeed), Eq(TypeParam::GetHash128("abc", kSeed)));  // deterministic
    EXPECT_THAT(TypeParam::GetHash128("abc", kSeed), Ne(TypeParam::GetHash128("abd", kSeed)));  // distinct
    constexpr std::string_view kLong = "this input is longer than the small-input cutoff.";
    const Hash128 hash = TypeParam::GetHash128(kLong, kSeed);
    EXPECT_THAT(hash.h1, Ne(hash.h2));
  } else {
    GTEST_SKIP() << TypeParam::Name() << " is 64-bit only";
  }
}

// The jumbo face is the same family: both directions delegate.
TEST_F(MumboHashTest, JumboIsTheSameFamily) {
  constexpr std::string_view kData = "mumbo jumbo";
  EXPECT_THAT(jumbo::GetHash128(kData, kSeed), Eq(mumbo::GetHash128(kData, kSeed)));
  EXPECT_THAT(jumbo::GetHash64(kData, kSeed), Eq(mumbo::GetHash64(kData, kSeed)));
  static_assert(HasStreaming<jumbo::Algorithm>);
}

// mumbo-specific: the 64-bit form and the 128-bit lanes are deliberately
// independent chains - none is a fold or truncation of another.
TEST_F(MumboHashTest, Hash64IsNotAFoldOfHash128) {
  constexpr std::string_view kLong = "this input is longer than the small-key cutoff, well past it.";
  const Hash128 hash128 = mumbo::GetHash128(kLong, kSeed);
  EXPECT_THAT(mumbo::GetHash64(kLong, kSeed), Ne(hash_internal::Hash128To64(hash128)));
  EXPECT_THAT(mumbo::GetHash64(kLong, kSeed), Ne(hash128.h1));
}

// murmur3-specific: the 64-bit variant is the canonical `h1` truncation.
TEST_F(Murmur3HashTest, Hash64IsH1) {
  EXPECT_THAT(murmur3::GetHash64("truncation-check"), Eq(murmur3::GetHash128("truncation-check").h1));
}

// The documented composition: a fold-mixed (non-canonical) 64-bit value can be
// derived from any 128-bit based algorithm via the public Hash128To64.
TEST_F(Murmur3HashTest, FoldedHash64ByComposition) {
  constexpr uint64_t kFolded = Hash128To64(murmur3::GetHash128("fold-check"));  // constexpr-safe
  EXPECT_THAT(kFolded, Eq(hash_internal::Hash128To64(murmur3::GetHash128("fold-check"))));
  EXPECT_THAT(kFolded, Ne(murmur3::GetHash64("fold-check")));  // distinct from the canonical truncation
}

// ---------------------------------------------------------------------------
// Known-answer tests: fnv1a / xxh64 / murmur3 must produce the canonical
// reference values (vectors generated with the reference implementations:
// python `xxhash` and `mmh3` packages, FNV-1a per its published spec).
// The inputs cover the block/tail boundaries of each algorithm.
// ---------------------------------------------------------------------------

// Positional rows read better than repeating field names in a homogeneous table.
// NOLINTBEGIN(modernize-use-designated-initializers)

struct KnownAnswer64 {
  uint64_t seed;
  std::string_view data;
  uint64_t expected;
};

TEST_F(KnownAnswerTest, Fnv1a) {
  // Canonical FNV-1a 64 with the standard offset basis.
  static constexpr std::array<KnownAnswer64, 6> kVectors{{
      {fnv1a::kOffsetBasis, "", 0xcbf29ce484222325ULL},
      {fnv1a::kOffsetBasis, "a", 0xaf63dc4c8601ec8cULL},
      {fnv1a::kOffsetBasis, "abc", 0xe71fa2190541574bULL},
      {fnv1a::kOffsetBasis, "12345678", 0x173932c41a90a42dULL},
      {fnv1a::kOffsetBasis, "1234567890123456", 0x3d30e6aa7c1d4bd3ULL},
      {fnv1a::kOffsetBasis, "The quick brown fox jumps over the lazy dog", 0xf3f9b7f5e7e47110ULL},
  }};
  static_assert(fnv1a::GetHash64("") == 0xcbf29ce484222325ULL);  // constexpr-safe & canonical
  for (const auto& [seed, data, expected] : kVectors) {
    EXPECT_THAT(fnv1a::GetHash64(data, seed), Eq(expected)) << "input: \"" << data << "\"";
  }
}

TEST_F(KnownAnswerTest, Xxh64) {
  static constexpr std::array<KnownAnswer64, 13> kVectors{{
      // seed 0; lengths cover <4, 4..7, 8..31, one 32-byte stripe, and beyond.
      {0, "", 0xef46db3751d8e999ULL},
      {0, "a", 0xd24ec4f1a98c6e5bULL},
      {0, "abc", 0x44bc2cf5ad770999ULL},
      {0, "1234", 0xd8316e61d84f6ba4ULL},
      {0, "12345678", 0xd2d02f08cf7cfd4aULL},
      {0, "123456789012345", 0xc377d78ade001a3cULL},
      {0, "1234567890123456", 0xb61c33dc6c59f270ULL},
      {0, "1234567890123456789012345678901", 0x8f367cb873a5376eULL},
      {0, "12345678901234567890123456789012", 0x40fd1aa52d98274cULL},
      {0, "The quick brown fox jumps over the lazy dog", 0x0b242d361fda71bcULL},
      // Non-zero seed.
      {5'381, "", 0xaf382146d5c3cacfULL},
      {5'381, "abc", 0x19d1c1b5c673451fULL},
      {5'381, "12345678901234567890123456789012", 0x38ea26e7c55f8095ULL},
  }};
  static_assert(xxh64::GetHash64("") == 0xef46db3751d8e999ULL);  // constexpr-safe & canonical
  for (const auto& [seed, data, expected] : kVectors) {
    EXPECT_THAT(xxh64::GetHash64(data, seed), Eq(expected)) << "input: \"" << data << "\", seed: " << seed;
  }
}

// Deterministic buffer matching the python generator: byte i = (i*131 + 7) % 256.
inline std::string PatternBuffer(std::size_t len) {
  std::string result(len, '\0');
  for (std::size_t i = 0; i < len; ++i) {
    result.at(i) = static_cast<char>(((i * 131) + 7) % 256);
  }
  return result;
}

TEST_F(KnownAnswerTest, Xxh3) {
  // Vectors from the reference implementation (python xxhash 3.8.0 / libxxhash
  // 0.8.x, XXH3_64bits[_withSeed]); lengths cover every dispatch class:
  // 0, 1..3, 4..8, 9..16, 17..128, 129..240, and >240 (with block boundaries),
  // plus the seeded custom-secret long path.
  struct Vector {
    uint64_t seed;
    std::size_t len;
    uint64_t expected;
  };

  // NOLINTBEGIN(modernize-use-designated-initializers)
  static constexpr std::array<Vector, 33> kVectors{{
      {0, 0, 0x2d06800538d394c2ULL},       {0, 1, 0x4c5cca45d0f4811fULL},         {0, 3, 0x6e3e2670e61106acULL},
      {0, 4, 0x5c4c63133443d03fULL},       {0, 8, 0xf9fd4dd0b04d78f5ULL},         {0, 9, 0x7c20df9712c26edfULL},
      {0, 16, 0x86abf6baccea0858ULL},      {0, 17, 0xb58bf5dc5022d071ULL},        {0, 32, 0xe3712ed84c04a66eULL},
      {0, 100, 0x5da67eac6d4093d5ULL},     {0, 128, 0x10d17f72c0ccba41ULL},       {0, 129, 0x1648bdc3db49d1a2ULL},
      {0, 240, 0xb6cfaf343fab81e6ULL},     {0, 241, 0x956cae592c67279eULL},       {0, 1'024, 0x70bd377d9574f4bbULL},
      {0, 1'025, 0x66c4487c41e127a7ULL},   {0, 3'000, 0x33a08283bba03e0cULL},     {5'381, 0, 0x0bdb294d2ae928f4ULL},
      {5'381, 1, 0xccbc30ca28c5b7e8ULL},   {5'381, 3, 0xc12a2fc1e85cfda3ULL},     {5'381, 4, 0x2a3e441637b99decULL},
      {5'381, 8, 0x3368bd3455763aa3ULL},   {5'381, 9, 0xbf055cc53ba01ac3ULL},     {5'381, 16, 0x22d53e6beb618654ULL},
      {5'381, 17, 0x6613984cb09ca12fULL},  {5'381, 32, 0xe3183cb5fe6a7771ULL},    {5'381, 100, 0xceb50f7a528f1274ULL},
      {5'381, 128, 0xb540c9613098d20fULL}, {5'381, 129, 0xc058e3a79595a255ULL},   {5'381, 240, 0x3fcdb3a19eba1245ULL},
      {5'381, 241, 0xb2cf4aaf8fc38710ULL}, {5'381, 1'024, 0xfb8787295ec29567ULL}, {5'381, 1'025, 0x70b3e67a34b687d0ULL},
  }};
  // NOLINTEND(modernize-use-designated-initializers)
  static_assert(xxh3::GetHash64("") == 0x2d06800538d394c2ULL);  // constexpr-safe & canonical
  for (const auto& [seed, len, expected] : kVectors) {
    EXPECT_THAT(xxh3::GetHash64(PatternBuffer(len), seed), Eq(expected)) << "len: " << len << ", seed: " << seed;
  }
  EXPECT_THAT(xxh3::GetHash64(PatternBuffer(3'000), 5'381), Eq(0x9ea762ada6fccc4cULL));
}

TEST_F(KnownAnswerTest, SipHash) {
  // Vectors generated with the reference implementation (python siphash24
  // bindings); key = spec key (k0=0x0706050403020100, k1=0x0F0E0D0C0B0A0908),
  // inputs = PatternBuffer. The empty-input value matches the vector table of
  // the SipHash paper.
  constexpr uint64_t kKey0 = 0x0706050403020100ULL;
  constexpr uint64_t kKey1 = 0x0F0E0D0C0B0A0908ULL;

  struct Vector {
    std::size_t len;
    uint64_t expected;
  };

  // NOLINTBEGIN(modernize-use-designated-initializers)
  static constexpr std::array<Vector, 13> kSip24{{
      {0, 0x726fdb47dd0e0e31ULL},
      {1, 0xe53c134f96dba15dULL},
      {3, 0xf22931640c6a9756ULL},
      {7, 0x40b76b06c9fe22c2ULL},
      {8, 0xc982d370511938afULL},
      {9, 0x2d83b3a41765b813ULL},
      {15, 0x7424220c84260544ULL},
      {16, 0xce2f33ccbe97d0f3ULL},
      {17, 0x2b35a8a6b2380a17ULL},
      {32, 0xdac6cb9cb68507b5ULL},
      {63, 0x13e4eb5b7c43382eULL},
      {64, 0x3e92c3a8f323d1ffULL},
      {100, 0x9b0b8b79ded13b59ULL},
  }};
  static constexpr std::array<Vector, 7> kSip13{{
      {0, 0xabac0158050fc4dcULL},
      {1, 0xe8f92ff638a47677ULL},
      {8, 0xfd2849775716d35aULL},
      {15, 0xd3bad9b8d4c76179ULL},
      {17, 0x28382ca83b7ec43fULL},
      {63, 0x03392909c6ff93b4ULL},
      {100, 0x7613ddf33a283941ULL},
  }};
  // NOLINTEND(modernize-use-designated-initializers)
  static_assert(siphash::GetHash64("", kKey0, kKey1) == 0x726fdb47dd0e0e31ULL);  // constexpr-safe & canonical
  for (const auto& [len, expected] : kSip24) {
    EXPECT_THAT(siphash::GetHash64(PatternBuffer(len), kKey0, kKey1), Eq(expected)) << "sip24 len: " << len;
  }
  for (const auto& [len, expected] : kSip13) {
    EXPECT_THAT(siphash::GetHash64Sip13(PatternBuffer(len), kKey0, kKey1), Eq(expected)) << "sip13 len: " << len;
  }
  // The Algorithm struct derives the key from the seed.
  EXPECT_THAT(
      siphash::Algorithm::GetHash64("derived", 42), Eq(siphash::GetHash64("derived", 42, hash_internal::Fmix64(42))));
}

TEST_F(KnownAnswerTest, Rapidhash) {
  // Vectors generated by compiling the reference rapidhash.h (V3, FAST
  // variant) and hashing PatternBuffer; lengths cover every dispatch branch:
  // 0, 1..3, 4..7, 8..16, the 17..112 ladder, the wide loop (113/224/225/448),
  // and long inputs.
  struct Vector {
    uint64_t seed;
    std::size_t len;
    uint64_t expected;
  };

  // NOLINTBEGIN(modernize-use-designated-initializers)
  static constexpr std::array<Vector, 36> kVectors{{
      {0, 0, 0x0338dc4be2cecdaeULL},       {0, 1, 0x5c2caf7d68f06d3eULL},         {0, 3, 0x453968562b8692e9ULL},
      {0, 4, 0xcfa4da2476b6636eULL},       {0, 7, 0x891cac4198e0c8cdULL},         {0, 8, 0x7b06ccf26409e9bcULL},
      {0, 16, 0x2a7a372c383b6b27ULL},      {0, 17, 0xf5896810dccb51a4ULL},        {0, 32, 0xdbadd79a9a59c738ULL},
      {0, 48, 0x236198ecde097648ULL},      {0, 80, 0x1ab23f91ad456b9aULL},        {0, 112, 0x710d7cc63a167a21ULL},
      {0, 113, 0xff65b963a2eb6fdbULL},     {0, 224, 0xb8e4f7c6687702a5ULL},       {0, 225, 0xe19212860084855fULL},
      {0, 448, 0x23dfc796c862b58dULL},     {0, 1'024, 0xd0a3c53d4a64095dULL},     {0, 3'000, 0x01e4b6bb408da728ULL},
      {5'381, 0, 0x94f1e09ed25cf3f4ULL},   {5'381, 1, 0x92f20d0156d7a148ULL},     {5'381, 3, 0x41385926e1bcb469ULL},
      {5'381, 4, 0x517c3de85a68262dULL},   {5'381, 7, 0x477af447cda27254ULL},     {5'381, 8, 0x4e7b2ce61973c653ULL},
      {5'381, 16, 0x6008b656be3852ecULL},  {5'381, 17, 0x87e8ebb1219ff461ULL},    {5'381, 32, 0x8ab32c6944aa2a92ULL},
      {5'381, 48, 0xfb262bd512bdf0afULL},  {5'381, 80, 0x1c37e6306543cc8eULL},    {5'381, 112, 0x0c951a7a6c0c1764ULL},
      {5'381, 113, 0x41c3b50cbc01a78aULL}, {5'381, 224, 0x6cd774026d09764aULL},   {5'381, 225, 0x511fe05e38bc3651ULL},
      {5'381, 448, 0x31142b32f6e0ac7aULL}, {5'381, 1'024, 0xf502455e7b222b24ULL}, {5'381, 3'000, 0xa11cb4ad99add5d8ULL},
  }};
  // NOLINTEND(modernize-use-designated-initializers)
  static_assert(rapidhash::GetHash64("") == 0x0338dc4be2cecdaeULL);  // constexpr-safe & canonical
  for (const auto& [seed, len, expected] : kVectors) {
    EXPECT_THAT(rapidhash::GetHash64(PatternBuffer(len), seed), Eq(expected)) << "len: " << len << ", seed: " << seed;
  }
}

TEST_F(KnownAnswerTest, Xxh3Hash128) {
  // Vectors from the reference implementation (python xxhash 3.8.0,
  // XXH3_128bits[_withSeed]); expected = {low64 (h1), high64 (h2)}. Lengths
  // cover every dispatch class and block boundary. For >240-byte inputs and
  // 1..3-byte inputs, h1 equals GetHash64 by construction.
  struct Vector {
    uint64_t seed;
    std::size_t len;
    uint64_t low;
    uint64_t high;
  };

  // NOLINTBEGIN(modernize-use-designated-initializers)
  static constexpr std::array<Vector, 34> kVectors{{
      {0, 0, 0x6001c324468d497fULL, 0x99aa06d3014798d8ULL},
      {0, 1, 0x4c5cca45d0f4811fULL, 0x495b62073ef70ca4ULL},
      {0, 3, 0x6e3e2670e61106acULL, 0x390cdc5b4a895dd7ULL},
      {0, 4, 0x3d668af6f2a44d77ULL, 0xaa6e2f274640a3f4ULL},
      {0, 8, 0x61ddbe7f31a6100dULL, 0x6a86a3bda6af4e3dULL},
      {0, 9, 0x8c7b67fd458a936bULL, 0x664c7ca18afd6255ULL},
      {0, 16, 0xe2ce54a7c19c730dULL, 0x7f9a218b0425449aULL},
      {0, 17, 0x8d96ef110fcdebb4ULL, 0x66fc23f6439dbd77ULL},
      {0, 32, 0xfd357cf6cb2dda18ULL, 0x49a11ee743d6d342ULL},
      {0, 100, 0x580b061a98a5a9b4ULL, 0x76b536586de98b82ULL},
      {0, 128, 0xff361dec1385710aULL, 0xaec730751478556cULL},
      {0, 129, 0x4545b3a09738e31aULL, 0x98cd36ccbb557926ULL},
      {0, 240, 0x3f2c53e72293711fULL, 0x5293e17bf553903dULL},
      {0, 241, 0x956cae592c67279eULL, 0xb53840fe3fedf161ULL},
      {0, 1'024, 0x70bd377d9574f4bbULL, 0xf69630613f24324dULL},
      {0, 1'025, 0x66c4487c41e127a7ULL, 0x621af7b8277effa4ULL},
      {0, 3'000, 0x33a08283bba03e0cULL, 0x90cabc4f5f596032ULL},
      {5'381, 0, 0xce9f957039fdfc5aULL, 0x51865a392947f8dbULL},
      {5'381, 1, 0xccbc30ca28c5b7e8ULL, 0x7622b2da7e568976ULL},
      {5'381, 3, 0xc12a2fc1e85cfda3ULL, 0xeb7681683a87cfa8ULL},
      {5'381, 4, 0xd74e8b038484beb8ULL, 0x30626fb0abe7e0a2ULL},
      {5'381, 8, 0x6501d98f6c92abb7ULL, 0x18e64d34e23f847cULL},
      {5'381, 9, 0x453e08063e480293ULL, 0xbe71aeab4444476fULL},
      {5'381, 16, 0x4ad713f8bcec42e9ULL, 0xd867bc71bf82ff3fULL},
      {5'381, 17, 0x7aea83eff72b6b01ULL, 0xd9dd05c9b72241deULL},
      {5'381, 32, 0x1c6b245f85ac99b9ULL, 0x59a4ce9e3a818c70ULL},
      {5'381, 100, 0x2bf8d43dc240d0dbULL, 0xce8901ad7e0e83c5ULL},
      {5'381, 128, 0xb84fff09698d0212ULL, 0xeec2a7402cec2480ULL},
      {5'381, 129, 0xe8bb102fa40f11d1ULL, 0x90e15ba935a9ee21ULL},
      {5'381, 240, 0xb46c9006ebb69097ULL, 0x27b92db776ed0086ULL},
      {5'381, 241, 0xb2cf4aaf8fc38710ULL, 0xbbc5d75a22a0c6a3ULL},
      {5'381, 1'024, 0xfb8787295ec29567ULL, 0xb1f1768e717216d8ULL},
      {5'381, 1'025, 0x70b3e67a34b687d0ULL, 0x7da1762539688ba6ULL},
      {5'381, 3'000, 0x9ea762ada6fccc4cULL, 0x12b04835843a0d28ULL},
  }};
  // NOLINTEND(modernize-use-designated-initializers)
  static_assert(xxh3::GetHash128("").h1 == 0x6001c324468d497fULL);  // constexpr-safe & canonical
  for (const auto& [seed, len, low, high] : kVectors) {
    const Hash128 hash = xxh3::GetHash128(PatternBuffer(len), seed);
    EXPECT_THAT(hash, Eq((Hash128{.h1 = low, .h2 = high}))) << "len: " << len << ", seed: " << seed;
  }
}

struct KnownAnswer128 {
  uint64_t seed;
  std::string_view data;
  Hash128 expected;
};

TEST_F(KnownAnswerTest, Murmur3) {
  static constexpr std::array<KnownAnswer128, 13> kVectors{{
      // seed 0; lengths cover empty, tail-only, one 16-byte block, block+tail.
      {0, "", {.h1 = 0x0000000000000000ULL, .h2 = 0x0000000000000000ULL}},
      {0, "a", {.h1 = 0x85555565f6597889ULL, .h2 = 0xe6b53a48510e895aULL}},
      {0, "abc", {.h1 = 0xb4963f3f3fad7867ULL, .h2 = 0x3ba2744126ca2d52ULL}},
      {0, "1234", {.h1 = 0x0897364d218fe7b4ULL, .h2 = 0x341e8bd92437fda5ULL}},
      {0, "12345678", {.h1 = 0x3b4a640638b1419cULL, .h2 = 0x913b0e676bd42557ULL}},
      {0, "123456789012345", {.h1 = 0x887001aea2afcfd6ULL, .h2 = 0x1ec326364f0801b3ULL}},
      {0, "1234567890123456", {.h1 = 0x4fbe5dc5c0e32cf8ULL, .h2 = 0xc0c8e96b60c322c1ULL}},
      {0, "1234567890123456789012345678901", {.h1 = 0x834b6c169e6c76d0ULL, .h2 = 0x9fb1ab235cc38195ULL}},
      {0, "12345678901234567890123456789012", {.h1 = 0xfb942466dfc9d856ULL, .h2 = 0x383859a6263746cfULL}},
      {0, "The quick brown fox jumps over the lazy dog", {.h1 = 0xe34bbc7bbc071b6cULL, .h2 = 0x7a433ca9c49a9347ULL}},
      // Non-zero seed (the reference takes a 32-bit seed; 5381 is in range).
      {5'381, "", {.h1 = 0xef2b598707a4bc70ULL, .h2 = 0x702e1ccf5cc2c6d8ULL}},
      {5'381, "abc", {.h1 = 0x5ff46b62f90afefaULL, .h2 = 0xa5d8a2001713a02eULL}},
      {5'381, "1234567890123456", {.h1 = 0x17e206afe56af6d7ULL, .h2 = 0xe73148f7ad58bae2ULL}},
  }};
  static_assert(murmur3::GetHash128("").h1 == 0);  // constexpr-safe & canonical
  for (const auto& [seed, data, expected] : kVectors) {
    EXPECT_THAT(murmur3::GetHash128(data, seed), Eq(expected)) << "input: \"" << data << "\", seed: " << seed;
  }
}

// Known-answer tests for the in-house mumbo/jumbo and dumbo, driven by the
// generated `hash_test_vectors.inc` (see that file). Unlike the canonical
// third-party KATs above, these pin the CURRENT in-house output, not a
// published reference: the -umbo family values are NOT stable across library
// versions (still evolving pre-1.0), so a deliberate algorithm change
// regenerates the .inc (and its .sha256). `InHouseVectors` re-derives every
// vector from the LIVE algorithm - so a data-or-algorithm change that is not a
// matched regeneration fails here - while the committed
// `hash_test_vectors.inc.sha256` independently proves the file is byte-for-byte
// unchanged (a `digest --check` sh_test verifies it; see BUILD.bazel).

// The vectors must (a) cover every in-house algorithm and (b) with enough cases
// each (all dispatch tiers, seeded and unseeded). A new in-house algorithm must
// be added here and to hash_test_vectors_gen.cc. An exact total would be
// meaningless; a per-algorithm floor is what actually matters.
constexpr std::size_t CountKatAlgo(kat::Algo algo) {
  std::size_t count = 0;
  for (const auto& vec : kat::kVectors) {
    if (vec.algo == algo) {
      ++count;
    }
  }
  return count;
}

static_assert(CountKatAlgo(kat::Algo::kMumbo) >= 20);
static_assert(CountKatAlgo(kat::Algo::kJumbo) >= 20);
static_assert(CountKatAlgo(kat::Algo::kDumbo) >= 20);
static_assert(CountKatAlgo(kat::Algo::kFambo) >= 20);
// Constexpr smoke (kVectors[0] is mumbo, seed 0, empty input).
static_assert(mumbo::GetHash64("", 0) == kat::kVectors.at(0).low);

// Compile-time guard for tembo: every input byte must influence the hash.
//
// This is the check that turns a dead-lane rewrite into a build failure. A
// 2026-08 rewrite of hash_tembo.h folded lanes 4..7 against lane3, so the
// lane4..6 sections of every non-final bulk window never reached the output:
// 48 of 240 input bytes were ignored, and nothing failed. tembo has constexpr
// evaluation paths, so the property is provable at compile time.
//
// The byte range is chunked because each static_assert is one constant
// evaluation and the full sweep of a large config exceeds clang's default
// constexpr step limit (-fconstexpr-steps, 2^20).

// Deterministic pattern long enough for two bulk windows plus tail.
template<std::size_t kLen>
constexpr std::array<char, kLen> TemboBytePattern() {
  std::array<char, kLen> data{};
  for (std::size_t i = 0; i < kLen; ++i) {
    data.at(i) = static_cast<char>((i * 7) + 3);  // NOLINT(*-magic-numbers)
  }
  return data;
}

// Flips each byte in [begin, end) once; true iff every flip changes the hash.
template<uint64_t kNumLanes, uint64_t kNumConsts, bool kSecretLoopInit, std::size_t kLen>
constexpr bool TemboEveryByteMatters(std::size_t begin, std::size_t end) {
  std::array<char, kLen> data = TemboBytePattern<kLen>();
  constexpr uint64_t kSeed = 42;
  const uint64_t base =
      tembo::GetHash64<kNumLanes, kNumConsts, kSecretLoopInit>(std::string_view(data.data(), kLen), kSeed);
  for (std::size_t i = begin; i < end; ++i) {
    data.at(i) = static_cast<char>(data.at(i) ^ 0x5A);  // NOLINT(*-magic-numbers)
    const uint64_t hash =
        tembo::GetHash64<kNumLanes, kNumConsts, kSecretLoopInit>(std::string_view(data.data(), kLen), kSeed);
    data.at(i) = static_cast<char>(data.at(i) ^ 0x5A);  // NOLINT(*-magic-numbers)
    if (hash == base) {
      return false;  // byte i is dead: it never reaches the output
    }
  }
  return true;
}

// NOLINTBEGIN(*-magic-numbers): lengths are 2 * kNumLanes * 16 + 16 per config.
static_assert(TemboEveryByteMatters<7, 8, true, 240>(0, 120));
static_assert(TemboEveryByteMatters<7, 8, true, 240>(120, 240));
static_assert(TemboEveryByteMatters<4, 4, false, 144>(0, 144));
static_assert(TemboEveryByteMatters<16, 8, true, 528>(0, 132));
static_assert(TemboEveryByteMatters<16, 8, true, 528>(132, 264));
static_assert(TemboEveryByteMatters<16, 8, true, 528>(264, 396));
static_assert(TemboEveryByteMatters<16, 8, true, 528>(396, 528));

// Compile-time KAT: pins the exact tembo output across the bulk loop, the
// if-ladder, and the tail (240 = 2 windows + 16 for the 7-lane config).
constexpr std::array<char, 240> kTemboKatPattern = TemboBytePattern<240>();
static_assert(
    tembo::GetHash64<7, 8, true>(std::string_view(kTemboKatPattern.data(), kTemboKatPattern.size()), 42)
    == 0xd2713e2ba0fdce94);

TEST_F(KnownAnswerTest, TemboMixesAllSupportedLargeLaneCountsAtRuntime) {
  const std::string data = PatternBuffer(256);
  EXPECT_THAT((tembo::GetHash64<10, 8, true>(data, 42)), Ne(tembo::GetHash64<10, 8, true>(data, 43)));
  EXPECT_THAT((tembo::GetHash64<11, 8, true>(data, 42)), Ne(tembo::GetHash64<11, 8, true>(data, 43)));
  EXPECT_THAT((tembo::GetHash64<14, 8, true>(data, 42)), Ne(tembo::GetHash64<14, 8, true>(data, 43)));
  EXPECT_THAT((tembo::GetHash64<15, 8, true>(data, 42)), Ne(tembo::GetHash64<15, 8, true>(data, 43)));
}

// NOLINTEND(*-magic-numbers)

// Every vector is re-derived from the LIVE algorithm and compared to the value
// pinned in the generated .inc. This fails if either the data or the algorithm
// changed without a matched regeneration (the diff_test in BUILD.bazel catches
// a stale .inc; the .sha256 sidecar proves the file is byte-for-byte unchanged).
TEST_F(KnownAnswerTest, InHouseVectors) {
  for (const auto& [algo, seed, len, low, high] : kat::kVectors) {
    const std::string data = PatternBuffer(len);
    switch (algo) {
      case kat::Algo::kMumbo:
        EXPECT_THAT(mumbo::GetHash64(data, seed), Eq(low)) << "mumbo len: " << len << ", seed: " << seed;
        break;
      case kat::Algo::kJumbo:
        EXPECT_THAT(jumbo::GetHash128(data, seed), Eq(Hash128{.h1 = low, .h2 = high}))
            << "jumbo len: " << len << ", seed: " << seed;
        break;
      case kat::Algo::kDumbo:
        EXPECT_THAT(dumbo::GetHash64(data, seed), Eq(low)) << "dumbo len: " << len << ", seed: " << seed;
        break;
      case kat::Algo::kFambo:
        EXPECT_THAT(fambo::GetHash64(data, seed), Eq(low)) << "fambo len: " << len << ", seed: " << seed;
        break;
    }
  }
}

// NOLINTEND(modernize-use-designated-initializers)

// ---------------------------------------------------------------------------
// Hash128 value-type behaviour (not per-algorithm).
// ---------------------------------------------------------------------------
TEST_F(Hash128Test, AbslStringifyRendersHex) {
  const Hash128 hash{.h1 = 0x0123456789abcdefULL, .h2 = 0xfedcba9876543210ULL};
  EXPECT_THAT(absl::StrCat(hash), Eq("0x0123456789abcdeffedcba9876543210"));
}

TEST_F(Hash128Test, AbslHashValueUsableAsKey) {
  absl::flat_hash_set<Hash128> seen;
  seen.insert(Hash128{.h1 = 1, .h2 = 2});
  seen.insert(Hash128{.h1 = 1, .h2 = 2});
  seen.insert(Hash128{.h1 = 3, .h2 = 4});
  EXPECT_THAT(seen, SizeIs(2));
}

TEST_F(Hash128Test, Ordering) {
  EXPECT_THAT((Hash128{.h1 = 1, .h2 = 2}), Eq((Hash128{.h1 = 1, .h2 = 2})));
  EXPECT_THAT((Hash128{.h1 = 1, .h2 = 2}), Lt((Hash128{.h1 = 1, .h2 = 3})));
  EXPECT_THAT((Hash128{.h1 = 1, .h2 = 9}), Lt((Hash128{.h1 = 2, .h2 = 0})));
}

// ---------------------------------------------------------------------------
// Algorithm structs, concepts, and the Hasher interface generator (hash.h).
// ---------------------------------------------------------------------------

// Fake algorithms exercising the Hasher fallback synthesis.
struct Fake64Only {
  static constexpr uint64_t GetHash64(std::string_view data, uint64_t seed) noexcept {
    return mumbo::GetHash64(data, seed) ^ 0xABCDEFULL;
  }
};

struct Fake128Only {
  static constexpr Hash128 GetHash128(std::string_view data, uint64_t seed) noexcept {
    return mumbo::GetHash128(data, seed);
  }
};

struct FakeWith32 {
  static constexpr uint64_t GetHash64(std::string_view data, uint64_t seed) noexcept {
    return mumbo::GetHash64(data, seed);
  }

  static constexpr uint32_t GetHash32(std::string_view /*data*/, uint64_t /*seed*/) noexcept {
    return 0xC0FFEE42U;  // sentinel: proves native pass-through
  }
};

// Concept detection over the public algorithm structs and the fakes.
static_assert(HasGetHash64<mumbo::Algorithm> && HasGetHash128<mumbo::Algorithm>);
static_assert(HasGetHash64<dumbo::Algorithm> && !HasGetHash128<dumbo::Algorithm>);
static_assert(HasGetHash64<fnv1a::Algorithm> && !HasGetHash128<fnv1a::Algorithm>);
static_assert(HasGetHash64<xxh64::Algorithm> && !HasGetHash128<xxh64::Algorithm>);
static_assert(HasGetHash64<murmur3::Algorithm> && HasGetHash128<murmur3::Algorithm>);
static_assert(!HasGetHash64<Fake128Only> && HasGetHash128<Fake128Only>);
static_assert(HasGetHash32<FakeWith32> && !HasGetHash32<mumbo::Algorithm>);
static_assert(IsHashAlgorithm<Fake64Only> && IsHashAlgorithm<Fake128Only>);
static_assert(!IsHashAlgorithm<int> && !IsHashAlgorithm<Hash128>);

TEST_F(HasherTest, NativeFunctionsPassThrough) {
  EXPECT_THAT(Hasher<mumbo::Algorithm>::GetHash64("pass", kSeed), Eq(mumbo::GetHash64("pass", kSeed)));
  EXPECT_THAT(Hasher<mumbo::Algorithm>::GetHash128("pass", kSeed), Eq(mumbo::GetHash128("pass", kSeed)));
  EXPECT_THAT(Hasher<murmur3::Algorithm>::GetHash64("pass", kSeed), Eq(murmur3::GetHash64("pass", kSeed)));
}

TEST_F(HasherTest, GetHash64FallsBackToFold) {
  EXPECT_THAT(Hasher<Fake128Only>::GetHash64("fold", kSeed), Eq(Hash128To64(Fake128Only::GetHash128("fold", kSeed))));
}

TEST_F(HasherTest, GetHash128FallsBackToTwoDecorrelatedPasses) {
  // The second lane skips the first up-to-8 bytes and injects them via the seed.
  constexpr std::string_view kData = "twopass-fallback";
  const Hash128 hash = Hasher<Fake64Only>::GetHash128(kData, kSeed);
  EXPECT_THAT(hash.h1, Eq(Fake64Only::GetHash64(kData, kSeed)));
  const uint64_t head = hash_internal::LoadTail(kData.data(), 8);  // NOLINT(*-stringview-data-usage): size passed
  EXPECT_THAT(hash.h2, Eq(Fake64Only::GetHash64(kData.substr(8), kSeed ^ kSeedFlip ^ head)));
  EXPECT_THAT(hash.h1, Ne(hash.h2));
  // Short inputs (< 8 bytes): the second lane hashes the empty remainder with
  // all bytes injected via the seed -- still covering every byte.
  const Hash128 tiny = Hasher<Fake64Only>::GetHash128("abc", kSeed);
  const uint64_t tiny_head = hash_internal::LoadTail("abc", 3);
  EXPECT_THAT(tiny.h2, Eq(Fake64Only::GetHash64("", kSeed ^ kSeedFlip ^ tiny_head)));
}

TEST_F(HasherTest, GetHash128FallbackDecorrelatesSeedIgnoringAlgorithms) {
  // `dumbo` ignores the seed entirely; a naive two-seed fallback would yield
  // h1 == h2. The lanes hash different data, so they still differ.
  const Hash128 hash = Hasher<dumbo::Algorithm>::GetHash128("longer than eight bytes", kSeed);
  EXPECT_THAT(hash.h1, Ne(hash.h2));
  // And inputs differing only past the first 8 bytes change both lanes.
  const Hash128 other = Hasher<dumbo::Algorithm>::GetHash128("longer than eight BYTES", kSeed);
  EXPECT_THAT(hash.h1, Ne(other.h1));
  EXPECT_THAT(hash.h2, Ne(other.h2));
}

TEST_F(HasherTest, GetHash32NativeAndFallback) {
  // Native 32-bit variant passes through...
  EXPECT_THAT(Hasher<FakeWith32>::GetHash32("native", kSeed), Eq(0xC0FFEE42U));
  // ...everything else XOR-folds the 64-bit hash.
  constexpr uint32_t kFolded = Hasher<rapidhash::Algorithm>::GetHash32("fold", kSeed);  // constexpr-safe
  EXPECT_THAT(kFolded, Eq(Hash64To32(rapidhash::GetHash64("fold", kSeed))));
  EXPECT_THAT(GetHash32("fold", kSeed), Eq(Hash64To32(GetHash64("fold", kSeed))));  // top-level, default algorithm
  EXPECT_THAT(GetHash32<xxh64::Algorithm>("fold", kSeed), Eq(Hash64To32(xxh64::GetHash64("fold", kSeed))));
}

TEST_F(HasherTest, TopLevelFunctionsUseDefaultAlgorithm) {
  // The bare entry points equal their default algorithm (and stay constexpr):
  // mumbo for 64-bit, jumbo for 128-bit.
  constexpr uint64_t kHash64 = GetHash64("top");
  EXPECT_THAT(kHash64, Eq((DefaultHasher::GetHash64("top"))));
  EXPECT_THAT(kHash64, Eq(mumbo::GetHash64("top", kDefaultSeed)));
  constexpr Hash128 kHash128 = GetHash128("top");
  EXPECT_THAT(kHash128, Eq((Hasher<Default128HashAlgorithm>::GetHash128("top"))));
  EXPECT_THAT(kHash128, Eq(jumbo::GetHash128("top", kDefaultSeed)));
}

TEST_F(HasherTest, TopLevelFunctionsArePluggable) {
  // Any algorithm struct can replace the default (the build-mangled `GetHash`
  // variant is covered in hash_mangle_test.cc).
  EXPECT_THAT(GetHash64<xxh64::Algorithm>("plug", kSeed), Eq(xxh64::GetHash64("plug", kSeed)));
  EXPECT_THAT(GetHash128<Fake64Only>("plug", kSeed), Eq((Hasher<Fake64Only>::GetHash128("plug", kSeed))));
}

// ---------------------------------------------------------------------------
// Hasher as a transparent container functor, and CombineHashes.
// ---------------------------------------------------------------------------
TEST_F(HasherTest, WorksAsTransparentContainerFunctor) {
  absl::flat_hash_map<std::string, int, DefaultHasher, std::equal_to<>> map;
  // map insert: operator[] creates the entry, so there is no range to exceed.
  // NOLINTBEGIN(*-avoid-unchecked-container-access)
  map["alpha"] = 1;
  map["beta"] = 2;
  // NOLINTEND(*-avoid-unchecked-container-access)
  const std::string_view lookup = "alpha";  // heterogeneous: no temporary std::string
  const auto it = map.find(lookup);
  ASSERT_THAT(it, Ne(map.end()));
  EXPECT_THAT(it->second, Eq(1));
  std::unordered_map<std::string, int, Hasher<xxh3::Algorithm>, std::equal_to<>> std_map;
  std_map["gamma"] = 3;
  EXPECT_THAT(std_map.find(std::string_view("gamma"))->second, Eq(3));
}

TEST_F(Hash64To32Test, FoldsBothHalves) {
  constexpr uint32_t kFolded = Hash64To32(0x123456780000FFFFULL);  // constexpr-safe
  EXPECT_THAT(kFolded, Eq(0x1234A987U));
  // Values differing only in the high half still shrink to different values.
  EXPECT_THAT(Hash64To32(0xAAAA000012345678ULL), Ne(Hash64To32(0xBBBB000012345678ULL)));
  EXPECT_THAT(Hash64To32(0), Eq(0U));
}

TEST_F(CombineHashesTest, MixesAndIsOrderDependent) {
  constexpr uint64_t kHash1 = GetHash64("first");
  constexpr uint64_t kHash2 = GetHash64("second");
  constexpr uint64_t kCombined = CombineHashes(kHash1, kHash2);  // constexpr-safe
  EXPECT_THAT(kCombined, Ne(kHash1));
  EXPECT_THAT(kCombined, Ne(kHash2));
  EXPECT_THAT(kCombined, Ne(CombineHashes(kHash2, kHash1)));  // order matters
  EXPECT_THAT(kCombined, Eq(CombineHashes(kHash1, kHash2)));  // deterministic
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::hash
