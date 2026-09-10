// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// The proof deliberately measures unchecked access to prevalidated bounded pool storage.

constexpr std::size_t kBlockCount = 128;
constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kCloseWastePercent = 25;
constexpr std::array<std::size_t, 8> kClassBytes = {512, 768, 1'024, 1'536, 2'048, 4'096, 8'192, 32'768};
constexpr std::array<std::size_t, 8> kChangedBytes = {512, 640, 1'024, 1'408, 2'048, 3'584, 8'192, 16'384};

struct RetainedBlock final {
  mbo::memory::MemoryBlock memory;
  std::size_t age = 0;
};

class CountingSource final {
 public:
  std::optional<mbo::memory::MemoryBlock> Acquire(std::size_t size) noexcept {
    ++acquires_;
    acquired_bytes_ += size;
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignof(std::max_align_t));
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++releases_;
    released_bytes_ += block.size;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  void ResetCounts() noexcept {
    acquires_ = 0;
    releases_ = 0;
    acquired_bytes_ = 0;
    released_bytes_ = 0;
  }

  std::size_t Acquires() const noexcept { return acquires_; }

  std::size_t Releases() const noexcept { return releases_; }

  std::size_t AcquiredBytes() const noexcept { return acquired_bytes_; }

  std::size_t ReleasedBytes() const noexcept { return released_bytes_; }

 private:
  std::size_t acquires_ = 0;
  std::size_t releases_ = 0;
  std::size_t acquired_bytes_ = 0;
  std::size_t released_bytes_ = 0;
};

class RetainedPool final {
 public:
  RetainedPool(CountingSource& source, std::size_t count_limit, std::size_t byte_limit)
      : source_(source), count_limit_(count_limit), byte_limit_(byte_limit) {
    for (auto& blocks : classes_) {
      blocks.reserve(kBlockCount);
    }
  }

  RetainedPool(const RetainedPool&) = delete;
  RetainedPool& operator=(const RetainedPool&) = delete;
  RetainedPool(RetainedPool&&) = delete;
  RetainedPool& operator=(RetainedPool&&) = delete;

  ~RetainedPool() {
    for (auto& blocks : classes_) {
      for (const RetainedBlock& block : blocks) {
        source_.Release(block.memory);
      }
    }
  }

  std::optional<mbo::memory::MemoryBlock> Take(std::size_t requested) noexcept {
    std::size_t first_fit = classes_.size();
    for (std::size_t pos = 0; pos < classes_.size(); ++pos) {
      if (!classes_[pos].empty() && kClassBytes[pos] >= requested) {
        first_fit = pos;
        break;
      }
    }
    if (first_fit == classes_.size()) {
      return std::nullopt;
    }
    std::size_t selected = first_fit;
    const std::size_t first_capacity = kClassBytes[first_fit];
    if (first_capacity != requested && first_capacity - requested > requested * kCloseWastePercent / 100) {
      for (std::size_t pos = classes_.size(); pos != first_fit; --pos) {
        if (!classes_[pos - 1].empty()) {
          selected = pos - 1;
          break;
        }
      }
    }
    RetainedBlock result = classes_[selected].back();
    classes_[selected].pop_back();
    --count_;
    bytes_ -= result.memory.size;
    return result.memory;
  }

  void Put(mbo::memory::MemoryBlock block) noexcept {
    const auto* const pos = std::ranges::lower_bound(kClassBytes, block.size);
    if (pos == kClassBytes.end() || *pos != block.size || count_limit_ == 0 || byte_limit_ < block.size) {
      source_.Release(block);
      return;
    }
    classes_[static_cast<std::size_t>(pos - kClassBytes.begin())].push_back(
        RetainedBlock{.memory = block, .age = next_age_++});
    ++count_;
    bytes_ += block.size;
    while (count_ > count_limit_ || bytes_ > byte_limit_) {
      EvictOldest();
    }
  }

  std::size_t Count() const noexcept { return count_; }

  std::size_t Bytes() const noexcept { return bytes_; }

 private:
  void EvictOldest() noexcept {
    std::size_t selected = classes_.size();
    std::size_t oldest = kUnlimited;
    for (std::size_t pos = 0; pos < classes_.size(); ++pos) {
      if (!classes_[pos].empty() && classes_[pos].front().age < oldest) {
        selected = pos;
        oldest = classes_[pos].front().age;
      }
    }
    const RetainedBlock block = classes_[selected].front();
    classes_[selected].erase(classes_[selected].begin());
    --count_;
    bytes_ -= block.memory.size;
    source_.Release(block.memory);
  }

  CountingSource& source_;
  std::array<std::vector<RetainedBlock>, kClassBytes.size()> classes_;
  std::size_t count_limit_;
  std::size_t byte_limit_;
  std::size_t count_ = 0;
  std::size_t bytes_ = 0;
  std::size_t next_age_ = 0;
};

class BlockSequence final {
 public:
  BlockSequence(std::size_t count_limit, std::size_t byte_limit) : pool_(source_, count_limit, byte_limit) {
    active_.reserve(kBlockCount);
  }

  BlockSequence(const BlockSequence&) = delete;
  BlockSequence& operator=(const BlockSequence&) = delete;
  BlockSequence(BlockSequence&&) = delete;
  BlockSequence& operator=(BlockSequence&&) = delete;

  ~BlockSequence() {
    for (const mbo::memory::MemoryBlock block : active_) {
      source_.Release(block);
    }
  }

  template<typename Sizes>
  bool Grow(const Sizes& sizes) noexcept {
    for (std::size_t pos = 0; pos < kBlockCount; ++pos) {
      const std::size_t requested = sizes[pos % sizes.size()];
      auto block = pool_.Take(requested);
      if (!block) {
        block = source_.Acquire(requested);
      }
      if (!block || block->size < requested) {
        return false;
      }
      active_.push_back(*block);
    }
    return true;
  }

  void Rollback() noexcept {
    while (!active_.empty()) {
      pool_.Put(active_.back());
      active_.pop_back();
    }
  }

  CountingSource& Source() noexcept { return source_; }

  std::size_t RetainedCount() const noexcept { return pool_.Count(); }

  std::size_t RetainedBytes() const noexcept { return pool_.Bytes(); }

 private:
  CountingSource source_;
  RetainedPool pool_;
  std::vector<mbo::memory::MemoryBlock> active_;
};

struct RetentionLimits final {
  const char* name;
  std::size_t count;
  std::size_t bytes;
};

constexpr std::array<RetentionLimits, 6> kLimits = {{
    {.name = "Eager", .count = 0, .bytes = 0},
    {.name = "Count8", .count = 8, .bytes = kUnlimited},
    {.name = "Count32", .count = 32, .bytes = kUnlimited},
    {.name = "Bytes32K", .count = kUnlimited, .bytes = std::size_t{32} * 1'024},
    {.name = "Bytes128K", .count = kUnlimited, .bytes = std::size_t{128} * 1'024},
    {.name = "Unbounded", .count = kUnlimited, .bytes = kUnlimited},
}};

template<bool Changed>
void BmRetention(benchmark::State& state) {
  const RetentionLimits limits = kLimits[static_cast<std::size_t>(state.range(0))];
  BlockSequence sequence(limits.count, limits.bytes);
  if (!sequence.Grow(kClassBytes)) {
    state.SkipWithError("initial allocation failed");
    return;
  }
  sequence.Source().ResetCounts();
  bool alternate = false;
  std::size_t low_count = 0;
  std::size_t low_bytes = 0;
  for (auto _ : state) {
    sequence.Rollback();
    low_count = sequence.RetainedCount();
    low_bytes = sequence.RetainedBytes();
    const bool grown = Changed && alternate ? sequence.Grow(kChangedBytes) : sequence.Grow(kClassBytes);
    alternate = !alternate;
    if (!grown) {
      state.SkipWithError("regrowth allocation failed");
      return;
    }
  }
  const auto iterations = static_cast<double>(state.iterations());
  state.counters["acquires_per_cycle"] = static_cast<double>(sequence.Source().Acquires()) / iterations;
  state.counters["acquired_bytes_per_cycle"] = static_cast<double>(sequence.Source().AcquiredBytes()) / iterations;
  state.counters["low_bytes"] = static_cast<double>(low_bytes);
  state.counters["low_count"] = static_cast<double>(low_count);
  state.counters["releases_per_cycle"] = static_cast<double>(sequence.Source().Releases()) / iterations;
  state.counters["released_bytes_per_cycle"] = static_cast<double>(sequence.Source().ReleasedBytes()) / iterations;
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBlockCount));
}

void RegisterBenchmarks() {
  for (std::size_t pos = 0; pos < kLimits.size(); ++pos) {
    benchmark::RegisterBenchmark(std::string("Retention/Exact/") + kLimits[pos].name, &BmRetention<false>)
        ->Arg(static_cast<std::int64_t>(pos));
    benchmark::RegisterBenchmark(std::string("Retention/Changed/") + kLimits[pos].name, &BmRetention<true>)
        ->Arg(static_cast<std::int64_t>(pos));
  }
}

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
  benchmark::AddCustomContext("experiment", "segmented-sequence-retention-v1");
  mbo::container::RegisterBenchmarks();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
