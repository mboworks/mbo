// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index):
// the benchmark deliberately measures unchecked mapping implementations over validated positions.
// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.

constexpr std::size_t kElementCount = 16'384;
constexpr std::size_t kPageSize = 64;
constexpr auto kListedCapacities = std::to_array<std::size_t>({64, 256, 1'024, 4'096});

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

using AlignedBlob64 = Blob<64, 64>;

static_assert(sizeof(Blob<16>) == 16);
static_assert(sizeof(Blob<64>) == 64);
static_assert(sizeof(Blob<256>) == 256);
static_assert(sizeof(Blob<64, 64>) == 64);
static_assert(alignof(Blob<64, 64>) == 64);
static_assert(sizeof(StringRecord) == sizeof(const char*) + sizeof(std::size_t));

template<typename T>
T MakeValue(std::size_t pos) noexcept {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<T>(pos);
  } else if constexpr (std::same_as<T, StringRecord>) {
    return StringRecord{.data = nullptr, .size = pos};
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
  } else {
    return value.value;
  }
}

template<typename T>
struct Segment final {
  std::vector<T> owned;
  T* data = nullptr;
  std::size_t begin = 0;
  std::size_t capacity = 0;
};

template<typename T>
class ShapeStorage final {
 public:
  ShapeStorage() {
    std::size_t begin = 0;
    std::size_t segment_index = 0;
    while (begin < kElementCount) {
      const std::size_t capacity =
          segment_index < kListedCapacities.size() ? kListedCapacities[segment_index] : kListedCapacities.back();
      std::vector<T> owned;
      owned.reserve(capacity);
      for (std::size_t offset = 0; offset < capacity; ++offset) {
        owned.push_back(MakeValue<T>(begin + offset));
      }
      T* const data = owned.data();
      segments_.push_back(Segment<T>{
          .owned = std::move(owned),
          .data = data,
          .begin = begin,
          .capacity = capacity,
      });
      begin += capacity;
      ++segment_index;
    }
  }

  const std::vector<Segment<T>>& Segments() const noexcept { return segments_; }

  const T& Expected(std::size_t pos) const noexcept {
    constexpr std::size_t kListedCapacity = 64 + 256 + 1'024 + 4'096;
    std::size_t segment_index = 0;
    std::size_t offset = pos;
    if (pos >= kListedCapacity) {
      const std::size_t tail = pos - kListedCapacity;
      segment_index = kListedCapacities.size() + (tail / kListedCapacities.back());
      offset = tail % kListedCapacities.back();
    } else {
      while (offset >= kListedCapacities[segment_index]) {
        offset -= kListedCapacities[segment_index];
        ++segment_index;
      }
    }
    return segments_[segment_index].data[offset];
  }

 private:
  std::vector<Segment<T>> segments_;
};

template<typename T>
class TailShapeLookup final {
 public:
  explicit TailShapeLookup(const ShapeStorage<T>& storage) noexcept : storage_(&storage) {}

  const T& operator[](std::size_t pos) const noexcept { return storage_->Expected(pos); }

  std::size_t Bytes() const noexcept { return 0; }

 private:
  const ShapeStorage<T>* storage_;
};

template<typename T>
class PointerShapeLookup final {
 public:
  explicit PointerShapeLookup(const ShapeStorage<T>& storage) {
    for (const Segment<T>& segment : storage.Segments()) {
      for (std::size_t offset = 0; offset < segment.capacity; offset += kPageSize) {
        pages_.push_back(segment.data + offset);
      }
    }
  }

  const T& operator[](std::size_t pos) const noexcept { return pages_[pos / kPageSize][pos % kPageSize]; }

  std::size_t Bytes() const noexcept { return pages_.capacity() * sizeof(typename decltype(pages_)::value_type); }

 private:
  std::vector<T*> pages_;
};

template<typename T>
class CompactShapeLookup final {
 public:
  explicit CompactShapeLookup(const ShapeStorage<T>& storage) : segments_(&storage.Segments()) {
    std::size_t segment_index = 0;
    for (const Segment<T>& segment : storage.Segments()) {
      for (std::size_t offset = 0; offset < segment.capacity; offset += kPageSize) {
        pages_.push_back(static_cast<std::uint16_t>(segment_index));
      }
      ++segment_index;
    }
  }

  const T& operator[](std::size_t pos) const noexcept {
    const Segment<T>& segment = (*segments_)[pages_[pos / kPageSize]];
    return segment.data[pos - segment.begin];
  }

  std::size_t Bytes() const noexcept { return pages_.capacity() * sizeof(pages_.front()); }

 private:
  const std::vector<Segment<T>>* segments_;
  std::vector<std::uint16_t> pages_;
};

template<bool Permuted, typename T, typename Lookup>
void MeasureLookup(benchmark::State& state, const ShapeStorage<T>& storage, const Lookup& lookup) {
  for (std::size_t pos = 0; pos < kElementCount; ++pos) {
    if (std::addressof(lookup[pos]) != std::addressof(storage.Expected(pos))) {
      state.SkipWithError("element-shape lookup returned the wrong address");
      return;
    }
  }
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < kElementCount; ++ordinal) {
      const std::size_t pos = Permuted ? (ordinal * 40'503) & (kElementCount - 1) : ordinal;
      sum += ReadValue(lookup[pos]);
    }
    benchmark::DoNotOptimize(sum);
  }
  state.counters["directory_bytes"] = static_cast<double>(lookup.Bytes());
  state.counters["element_bytes"] = static_cast<double>(sizeof(T));
  state.counters["element_alignment"] = static_cast<double>(alignof(T));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kElementCount));
}

template<typename T, bool Permuted>
void BmTailShape(benchmark::State& state) {
  const ShapeStorage<T> storage;
  const TailShapeLookup<T> lookup(storage);
  MeasureLookup<Permuted>(state, storage, lookup);
}

template<typename T, bool Permuted>
void BmPointerShape(benchmark::State& state) {
  const ShapeStorage<T> storage;
  const PointerShapeLookup<T> lookup(storage);
  MeasureLookup<Permuted>(state, storage, lookup);
}

template<typename T, bool Permuted>
void BmCompactShape(benchmark::State& state) {
  const ShapeStorage<T> storage;
  const CompactShapeLookup<T> lookup(storage);
  MeasureLookup<Permuted>(state, storage, lookup);
}

#define REGISTER_ELEMENT_SHAPE(Label, Type)                                                              \
  BENCHMARK_TEMPLATE(BmTailShape, Type, false)->Name("ElementShape/Sequential/Tail/" Label);             \
  BENCHMARK_TEMPLATE(BmTailShape, Type, true)->Name("ElementShape/Permuted/Tail/" Label);                \
  BENCHMARK_TEMPLATE(BmPointerShape, Type, false)->Name("ElementShape/Sequential/PointerPage64/" Label); \
  BENCHMARK_TEMPLATE(BmPointerShape, Type, true)->Name("ElementShape/Permuted/PointerPage64/" Label);    \
  BENCHMARK_TEMPLATE(BmCompactShape, Type, false)->Name("ElementShape/Sequential/CompactPage64/" Label); \
  BENCHMARK_TEMPLATE(BmCompactShape, Type, true)->Name("ElementShape/Permuted/CompactPage64/" Label)

REGISTER_ELEMENT_SHAPE("U8", std::uint8_t);
REGISTER_ELEMENT_SHAPE("U16", std::uint16_t);
REGISTER_ELEMENT_SHAPE("U32", std::uint32_t);
REGISTER_ELEMENT_SHAPE("U64", std::uint64_t);
REGISTER_ELEMENT_SHAPE("Blob16", Blob<16>);
REGISTER_ELEMENT_SHAPE("StringRecord", StringRecord);
REGISTER_ELEMENT_SHAPE("Blob64", Blob<64>);
REGISTER_ELEMENT_SHAPE("Blob256", Blob<256>);
REGISTER_ELEMENT_SHAPE("Aligned64", AlignedBlob64);

#undef REGISTER_ELEMENT_SHAPE

// NOLINTEND(clang-analyzer-deadcode.DeadStores)
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
#if defined(__clang__)
  benchmark::AddCustomContext("compiler", std::string("clang-") + std::to_string(__clang_major__));
  benchmark::AddCustomContext("compiler_version", __clang_version__);
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler", std::string("gcc-") + std::to_string(__GNUC__));
  benchmark::AddCustomContext("compiler_version", __VERSION__);
#endif
#if __cplusplus >= 202'302L
  benchmark::AddCustomContext("cxx_standard", "c++23");
#else
  benchmark::AddCustomContext("cxx_standard", "c++20");
#endif
  benchmark::AddCustomContext("experiment", "segmented-sequence-element-shape-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
