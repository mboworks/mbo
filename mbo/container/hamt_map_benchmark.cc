// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
// Deliberate standard-container comparison, not a production recommendation.
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "benchmark/benchmark.h"
#include "mbo/container/hamt_flat_map.h"
#include "mbo/container/hamt_node_map.h"

namespace mbo::container {
namespace {

struct IntegerHash final {
  constexpr std::uint64_t operator()(std::uint64_t key) const noexcept {
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    return key ^ (key >> 31);
  }
};

struct ConstantHash final {
  constexpr std::uint64_t operator()(std::uint64_t /*key*/) const noexcept { return 0; }
};

template<std::size_t Bits>
using Flat =
    HamtFlatMap<std::uint64_t, std::uint64_t, IntegerHash, std::equal_to<>, HamtOptions{.fragment_bits = Bits}>;
template<std::size_t Bits>
using Node =
    HamtNodeMap<std::uint64_t, std::uint64_t, IntegerHash, std::equal_to<>, HamtOptions{.fragment_bits = Bits}>;
using Standard = std::unordered_map<std::uint64_t, std::uint64_t, IntegerHash>;
using AbseilFlat = absl::flat_hash_map<std::uint64_t, std::uint64_t, IntegerHash>;
using AbseilNode = absl::node_hash_map<std::uint64_t, std::uint64_t, IntegerHash>;
using CollisionFlat = HamtFlatMap<std::uint64_t, std::uint64_t, ConstantHash>;
using CollisionNode = HamtNodeMap<std::uint64_t, std::uint64_t, ConstantHash>;
using CollisionStandard = std::unordered_map<std::uint64_t, std::uint64_t, ConstantHash>;
using CollisionAbseilFlat = absl::flat_hash_map<std::uint64_t, std::uint64_t, ConstantHash>;
using CollisionAbseilNode = absl::node_hash_map<std::uint64_t, std::uint64_t, ConstantHash>;

template<typename Map>
Map Populate(std::size_t count) {
  if constexpr (requires(Map map) { map.transient(); }) {
    Map map;
    for (std::size_t pos = 0; pos < count; ++pos) {
      map = std::move(map.insert({static_cast<std::uint64_t>(pos), static_cast<std::uint64_t>(pos + 1)}).first);
    }
    return map;
  } else {
    Map map;
    for (std::size_t pos = 0; pos < count; ++pos) {
      map.insert({static_cast<std::uint64_t>(pos), static_cast<std::uint64_t>(pos + 1)});
    }
    return map;
  }
}

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark's range variable drives iterations.
template<typename Map, bool Missing>
void BmLookup(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const Map map = Populate<Map>(count);
  if (map.size() != count) {
    state.SkipWithError("population cardinality mismatch");
    return;
  }
  for (std::size_t pos = 0; pos < count; ++pos) {
    const auto key = static_cast<std::uint64_t>(Missing ? count + pos : pos);
    const auto found = map.find(key);
    if constexpr (Missing) {
      if (found != map.end()) {
        state.SkipWithError("missing key unexpectedly found");
        return;
      }
    } else if (found == map.end() || found->second != pos + 1) {
      state.SkipWithError("stored key or mapped value mismatch");
      return;
    }
  }
  const auto* read = &map;
  benchmark::DoNotOptimize(read);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
      const std::size_t pos = (ordinal * 40'503) & (count - 1);
      const auto key = static_cast<std::uint64_t>(Missing ? count + pos : pos);
      const auto found = read->find(key);
      if (found != read->end()) {
        sum += found->second;
      }
    }
    benchmark::DoNotOptimize(sum);
  }
  state.counters["entries"] = static_cast<double>(count);
  state.counters["missing"] = Missing ? 1 : 0;
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Map>
void BmTraversal(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const Map map = Populate<Map>(count);
  std::vector<bool> seen(count, false);
  std::size_t visited = 0;
  for (const auto& entry : map) {
    if (entry.first >= count || entry.second != entry.first + 1) {
      state.SkipWithError("traversal key or value mismatch");
      return;
    }
    const auto pos = static_cast<std::size_t>(entry.first);
    if (seen.at(pos)) {
      state.SkipWithError("traversal repeated a key");
      return;
    }
    seen.at(pos) = true;
    ++visited;
  }
  if (visited != count) {
    state.SkipWithError("traversal cardinality mismatch");
    return;
  }
  const auto* read = &map;
  benchmark::DoNotOptimize(read);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    std::uint64_t sum = 0;
    for (const auto& entry : *read) {
      sum += entry.second;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.counters["entries"] = static_cast<double>(count);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Map>
auto NewEditable() {
  if constexpr (requires(Map map) { map.transient(); }) {
    return Map{}.transient();
  } else {
    return Map{};
  }
}

template<bool Validate, typename Map>
bool FillEraseCycle(Map& map, std::size_t count, benchmark::State& state) {
  for (std::size_t pos = 0; pos < count; ++pos) {
    auto inserted = map.insert({static_cast<std::uint64_t>(pos), static_cast<std::uint64_t>(pos + 1)});
    if constexpr (Validate) {
      if (!inserted.second || inserted.first == map.end() || inserted.first->first != pos
          || inserted.first->second != pos + 1) {
        state.SkipWithError("fresh insertion result mismatch");
        return false;
      }
    } else {
      benchmark::DoNotOptimize(inserted);
    }
  }
  for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
    const auto key = static_cast<std::uint64_t>((ordinal * 40'503) & (count - 1));
    auto erased = map.erase(key);
    if constexpr (Validate) {
      if (erased != 1) {
        state.SkipWithError("fresh erasure result mismatch");
        return false;
      }
    } else {
      benchmark::DoNotOptimize(erased);
    }
  }
  if constexpr (Validate) {
    if (!map.empty()) {
      state.SkipWithError("fill/erase cycle did not empty the map");
      return false;
    }
  }
  return true;
}

template<typename Map>
void BmFillEraseFresh(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  {
    auto preflight = NewEditable<Map>();
    if (!FillEraseCycle<true>(preflight, count, state)) {
      return;
    }
  }
  for (auto _ : state) {
    auto map = NewEditable<Map>();
    FillEraseCycle<false>(map, count, state);
    benchmark::DoNotOptimize(map);
  }
  state.counters["entries"] = static_cast<double>(count);
  state.counters["operations_per_iteration"] = static_cast<double>(2 * count);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(2 * count));
}

template<typename Map>
auto EditableFrom(const Map& parent) {
  if constexpr (requires { parent.transient(); }) {
    return parent.transient();
  } else {
    return Map(parent);
  }
}

template<bool AllEntries, typename Map>
void BmBranchUpdate(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const Map parent = Populate<Map>(count);
  const std::size_t edits = AllEntries ? count : 1;
  {
    auto child = EditableFrom(parent);
    for (std::size_t pos = 0; pos < edits; ++pos) {
      child.at(static_cast<std::uint64_t>(pos)) += 1;
    }
    for (std::size_t pos = 0; pos < count; ++pos) {
      const auto key = static_cast<std::uint64_t>(pos);
      if (parent.at(key) != pos + 1 || std::as_const(child).at(key) != pos + 1 + (pos < edits ? 1 : 0)) {
        state.SkipWithError("branch update changed parent or produced an incorrect child");
        return;
      }
    }
    if (child.size() != count || parent.size() != count) {
      state.SkipWithError("branch update changed cardinality");
      return;
    }
  }
  for (auto _ : state) {
    auto child = EditableFrom(parent);
    for (std::size_t pos = 0; pos < edits; ++pos) {
      auto& mapped = child.at(static_cast<std::uint64_t>(pos));
      mapped += 1;
      benchmark::DoNotOptimize(mapped);
    }
    benchmark::DoNotOptimize(child);
  }
  state.counters["entries"] = static_cast<double>(count);
  state.counters["edits_per_iteration"] = static_cast<double>(edits);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(edits));
}

template<typename Map>
void Register(const char* name) {
  const std::string prefix = std::string("HamtMap/") + name;
  benchmark::RegisterBenchmark(prefix + "/FindHit", &BmLookup<Map, false>)->Arg(64)->Arg(1'024)->Arg(16'384);
  benchmark::RegisterBenchmark(prefix + "/FindMiss", &BmLookup<Map, true>)->Arg(64)->Arg(1'024)->Arg(16'384);
  benchmark::RegisterBenchmark(prefix + "/Traverse", &BmTraversal<Map>)->Arg(64)->Arg(1'024)->Arg(16'384);
  benchmark::RegisterBenchmark(prefix + "/FillEraseFresh", &BmFillEraseFresh<Map>)->Arg(64)->Arg(1'024)->Arg(16'384);
  benchmark::RegisterBenchmark(prefix + "/BranchUpdateOne", &BmBranchUpdate<false, Map>)
      ->Arg(64)
      ->Arg(1'024)
      ->Arg(16'384);
  benchmark::RegisterBenchmark(prefix + "/BranchUpdateAll", &BmBranchUpdate<true, Map>)
      ->Arg(64)
      ->Arg(1'024)
      ->Arg(16'384);
}

template<typename Map>
void RegisterCollision(const char* name) {
  const std::string prefix = std::string("HamtMap/FullHashCollision/") + name;
  benchmark::RegisterBenchmark(prefix + "/FindHit", &BmLookup<Map, false>)->Arg(16)->Arg(64)->Arg(256);
  benchmark::RegisterBenchmark(prefix + "/FindMiss", &BmLookup<Map, true>)->Arg(16)->Arg(64)->Arg(256);
  benchmark::RegisterBenchmark(prefix + "/FillEraseFresh", &BmFillEraseFresh<Map>)->Arg(16)->Arg(64)->Arg(256);
}

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

void RegisterAll() {
  Register<Flat<4>>("Flat4");
  Register<Flat<5>>("Flat5");
  Register<Flat<6>>("Flat6");
  Register<Flat<7>>("Flat7");
  Register<Node<4>>("Node4");
  Register<Node<5>>("Node5");
  Register<Node<6>>("Node6");
  Register<Node<7>>("Node7");
  Register<Standard>("Standard");
  Register<AbseilFlat>("AbseilFlat");
  Register<AbseilNode>("AbseilNode");
  RegisterCollision<CollisionFlat>("Flat5");
  RegisterCollision<CollisionNode>("Node5");
  RegisterCollision<CollisionStandard>("Standard");
  RegisterCollision<CollisionAbseilFlat>("AbseilFlat");
  RegisterCollision<CollisionAbseilNode>("AbseilNode");
}

}  // namespace
}  // namespace mbo::container

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  mbo::container::RegisterAll();
#if defined(__clang__)
  benchmark::AddCustomContext("compiler_version", __clang_version__);
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler_version", __VERSION__);
#endif
#if __cplusplus >= 202'302L
  benchmark::AddCustomContext("cxx_standard", "c++23");
#else
  benchmark::AddCustomContext("cxx_standard", "c++20");
#endif
  benchmark::AddCustomContext("hash_profiles", "common-64-bit-integer-mix; constant-zero full-hash collision");
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
