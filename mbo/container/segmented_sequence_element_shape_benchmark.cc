// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "mbo/container/internal/segmented_sequence_benchmark_context.h"
#include "mbo/container/segmented_sequence.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;
constexpr SegmentedSequenceOptions kSegment64{.segment_size = 64};
constexpr SegmentedSequenceOptions kSegment256{.segment_size = 256};

template<std::size_t Bytes, std::size_t Alignment = alignof(std::uint64_t)>
struct alignas(Alignment) Blob final {
  static_assert(Bytes >= sizeof(std::uint64_t));

  std::uint64_t value = 0;
  std::array<std::byte, Bytes - sizeof(std::uint64_t)> padding{};
};

struct StringRecord final {
  const char* data = nullptr;
  std::size_t size = 0;
};

using Blob16 = Blob<16>;
using Blob64 = Blob<64>;
using Blob256 = Blob<256>;
using AlignedBlob64 = Blob<64, 64>;

static_assert(sizeof(Blob16) == 16);
static_assert(sizeof(Blob64) == 64);
static_assert(sizeof(Blob256) == 256);
static_assert(sizeof(AlignedBlob64) == 64);
static_assert(alignof(AlignedBlob64) == 64);
static_assert(sizeof(StringRecord) == sizeof(const char*) + sizeof(std::size_t));

template<typename T>
T MakeValue(std::size_t pos) {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<T>(pos);
  } else if constexpr (std::same_as<T, StringRecord>) {
    return StringRecord{.data = nullptr, .size = pos};
  } else if constexpr (std::same_as<T, std::string>) {
    return std::to_string(pos);
  } else {
    return T{.value = pos};
  }
}

template<typename T>
std::uint64_t ReadValue(const T& value) noexcept {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<std::uint64_t>(value);
  } else if constexpr (std::same_as<T, StringRecord>) {
    return value.size;
  } else if constexpr (std::same_as<T, std::string>) {
    return value.size();
  } else {
    return value.value;
  }
}

template<typename T, SegmentedSequenceOptions Options, bool Permuted>
void BmIndexed(benchmark::State& state) {
  SegmentedSequence<T, Options> sequence;
  sequence.reserve(kElementCount);
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    sequence.unchecked_emplace_back(MakeValue<T>(pos));
  }

  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < kElementCount; ++ordinal) {
      const std::size_t pos = Permuted ? (ordinal * 40'503) & (kElementCount - 1) : ordinal;
      sum += ReadValue(sequence[pos]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    benchmark::DoNotOptimize(sum);
  }

  state.counters["capacity"] = static_cast<double>(sequence.capacity());
  state.counters["element_alignment"] = static_cast<double>(alignof(T));
  state.counters["element_bytes"] = static_cast<double>(sizeof(T));
  state.counters["reserved"] = static_cast<double>(sequence.bytes_reserved());
  state.counters["segments"] = static_cast<double>(sequence.segment_count());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

#define REGISTER_ELEMENT_SHAPE(Label, Type)                                                             \
  BENCHMARK_TEMPLATE(BmIndexed, Type, kSegment64, false)->Name("ElementShape/Sequential/S64/" Label);   \
  BENCHMARK_TEMPLATE(BmIndexed, Type, kSegment64, true)->Name("ElementShape/Permuted/S64/" Label);      \
  BENCHMARK_TEMPLATE(BmIndexed, Type, kSegment256, false)->Name("ElementShape/Sequential/S256/" Label); \
  BENCHMARK_TEMPLATE(BmIndexed, Type, kSegment256, true)->Name("ElementShape/Permuted/S256/" Label)

REGISTER_ELEMENT_SHAPE("U8", std::uint8_t);
REGISTER_ELEMENT_SHAPE("U16", std::uint16_t);
REGISTER_ELEMENT_SHAPE("U32", std::uint32_t);
REGISTER_ELEMENT_SHAPE("U64", std::uint64_t);
REGISTER_ELEMENT_SHAPE("Blob16", Blob16);
REGISTER_ELEMENT_SHAPE("StringRecord", StringRecord);
REGISTER_ELEMENT_SHAPE("Blob64", Blob64);
REGISTER_ELEMENT_SHAPE("Blob256", Blob256);
REGISTER_ELEMENT_SHAPE("Aligned64", AlignedBlob64);
REGISTER_ELEMENT_SHAPE("String", std::string);

#undef REGISTER_ELEMENT_SHAPE

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::container::container_internal::AddSegmentedSequenceBenchmarkContext("segmented-sequence-element-shape-v2");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
