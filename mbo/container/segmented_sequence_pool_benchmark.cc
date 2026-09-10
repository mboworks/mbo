// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// The proof deliberately measures unchecked access to prevalidated bounded pool storage.

constexpr std::array<std::size_t, 8> kClasses = {64, 96, 128, 192, 256, 512, 1'024, 4'096};
constexpr std::array<std::size_t, 16> kRequests = {
    64, 128, 80, 256, 448, 1'024, 160, 4'096, 96, 224, 512, 896, 64, 192, 384, 2'048,
};
constexpr std::size_t kCloseWastePercent = 25;

struct Block final {
  std::uintptr_t identity = 0;
  std::size_t capacity = 0;
};

constexpr bool IsCloseFit(std::size_t capacity, std::size_t requested) noexcept {
  return capacity >= requested && capacity - requested <= requested * kCloseWastePercent / 100;
}

class LinearPool final {
 public:
  explicit LinearPool(std::size_t count) {
    blocks_.reserve(count);
    for (std::size_t pos = 0; pos < count; ++pos) {
      blocks_.push_back(Block{.identity = pos + 1, .capacity = kClasses[pos % kClasses.size()]});
    }
  }

  Block Take(std::size_t requested) {
    std::size_t exact = blocks_.size();
    std::size_t close = blocks_.size();
    std::size_t largest = blocks_.size();
    for (std::size_t pos = blocks_.size(); pos != 0; --pos) {
      const std::size_t index = pos - 1;
      const std::size_t capacity = blocks_[index].capacity;
      if (capacity == requested) {
        exact = index;
        break;
      }
      if (IsCloseFit(capacity, requested) && (close == blocks_.size() || capacity < blocks_[close].capacity)) {
        close = index;
      }
      if (capacity >= requested && (largest == blocks_.size() || capacity > blocks_[largest].capacity)) {
        largest = index;
      }
    }
    const std::size_t selected = exact != blocks_.size() ? exact : (close != blocks_.size() ? close : largest);
    if (selected == blocks_.size()) {
      return {};
    }
    Block result = blocks_[selected];
    blocks_[selected] = blocks_.back();
    blocks_.pop_back();
    return result;
  }

  void Put(Block block) { blocks_.push_back(block); }

  std::size_t MetadataBytes() const noexcept { return blocks_.capacity() * sizeof(Block); }

 private:
  std::vector<Block> blocks_;
};

class SortedPool final {
 public:
  explicit SortedPool(std::size_t count) {
    blocks_.reserve(count);
    for (std::size_t pos = 0; pos < count; ++pos) {
      blocks_.push_back(Block{.identity = pos + 1, .capacity = kClasses[pos % kClasses.size()]});
    }
    std::ranges::sort(blocks_, {}, &Block::capacity);
  }

  Block Take(std::size_t requested) {
    auto selected = std::ranges::lower_bound(blocks_, requested, {}, &Block::capacity);
    if (selected == blocks_.end()) {
      return {};
    }
    if (!IsCloseFit(selected->capacity, requested) && selected->capacity != requested) {
      selected = std::prev(blocks_.end());
    } else {
      const std::size_t capacity = selected->capacity;
      selected = std::prev(std::ranges::upper_bound(selected, blocks_.end(), capacity, {}, &Block::capacity));
    }
    Block result = *selected;
    blocks_.erase(selected);
    return result;
  }

  void Put(Block block) {
    blocks_.insert(std::ranges::upper_bound(blocks_, block.capacity, {}, &Block::capacity), block);
  }

  std::size_t MetadataBytes() const noexcept { return blocks_.capacity() * sizeof(Block); }

 private:
  std::vector<Block> blocks_;
};

class ClassPool final {
 public:
  explicit ClassPool(std::size_t count) {
    const std::size_t per_class = (count + kClasses.size() - 1) / kClasses.size();
    for (auto& blocks : classes_) {
      blocks.reserve(per_class);
    }
    for (std::size_t pos = 0; pos < count; ++pos) {
      classes_[pos % kClasses.size()].push_back(
          Block{.identity = pos + 1, .capacity = kClasses[pos % kClasses.size()]});
    }
  }

  Block Take(std::size_t requested) {
    std::size_t first_fit = kClasses.size();
    for (std::size_t pos = 0; pos < kClasses.size(); ++pos) {
      if (!classes_[pos].empty() && kClasses[pos] >= requested) {
        first_fit = pos;
        break;
      }
    }
    if (first_fit == kClasses.size()) {
      return {};
    }
    std::size_t selected = first_fit;
    if (!IsCloseFit(kClasses[first_fit], requested) && kClasses[first_fit] != requested) {
      for (std::size_t pos = kClasses.size(); pos != first_fit; --pos) {
        if (!classes_[pos - 1].empty()) {
          selected = pos - 1;
          break;
        }
      }
    }
    Block result = classes_[selected].back();
    classes_[selected].pop_back();
    return result;
  }

  void Put(Block block) {
    const auto* const pos = std::ranges::lower_bound(kClasses, block.capacity);
    classes_[static_cast<std::size_t>(pos - kClasses.begin())].push_back(block);
  }

  std::size_t MetadataBytes() const noexcept {
    std::size_t result = 0;
    for (const auto& blocks : classes_) {
      result += blocks.capacity() * sizeof(Block);
    }
    return result;
  }

 private:
  std::array<std::vector<Block>, kClasses.size()> classes_;
};

template<typename Pool>
void BmPoolCycle(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  Pool pool(count);
  std::size_t request_pos = 0;
  std::uintptr_t checksum = 0;
  for (auto _ : state) {
    const std::size_t requested = kRequests[request_pos++ % kRequests.size()];
    Block block = pool.Take(requested);
    benchmark::DoNotOptimize(block);
    if (block.identity == 0 || block.capacity < requested) {
      state.SkipWithError("pool failed to return a compatible block");
      return;
    }
    checksum += block.identity;
    pool.Put(block);
  }
  benchmark::DoNotOptimize(checksum);
  state.counters["metadata"] = static_cast<double>(pool.MetadataBytes());
  state.counters["pool_size"] = static_cast<double>(count);
  state.SetItemsProcessed(state.iterations());
}

#define REGISTER_POOL(Type) BENCHMARK_TEMPLATE(BmPoolCycle, Type)->Name("Pool/" #Type)->Arg(8)->Arg(32)->Arg(128)

REGISTER_POOL(LinearPool);
REGISTER_POOL(SortedPool);
REGISTER_POOL(ClassPool);

#undef REGISTER_POOL

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

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
  benchmark::AddCustomContext("experiment", "segmented-sequence-pool-v1");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
