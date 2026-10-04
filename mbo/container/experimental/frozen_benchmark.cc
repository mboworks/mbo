// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "benchmark/benchmark.h"
#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/frozen_set.h"
#include "mbo/container/experimental/internal/frozen_benchmark_data.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"

namespace mbo::container::experimental {
namespace {

enum class Layout { kLinear, kLimited, kMinimal, kSparse };
enum class Workload { kHit, kMiss, kMixed };

template<typename Key, std::size_t Size, bool Map, Layout Kind>
constexpr auto MakeTable() {
  using Fixture = frozen_internal::BenchmarkData<Size>;
  if constexpr (Kind == Layout::kLinear) {
    if constexpr (Map) {
      return Fixture::template Pairs<Key>();
    } else {
      return Fixture::template Keys<Key>();
    }
  } else if constexpr (Kind == Layout::kLimited) {
    if constexpr (Map) {
      constexpr auto kEntries = Fixture::template Pairs<Key>();
      return LimitedMap<Key, int, Size>(kEntries.begin(), kEntries.end());
    } else {
      constexpr auto kEntries = Fixture::template Keys<Key>();
      return LimitedSet<Key, Size>(kEntries.begin(), kEntries.end());
    }
  } else {
    constexpr FrozenOptions kOptions{.capacity = Size, .slots = Kind == Layout::kMinimal ? Size : Size * 2};
    if constexpr (Map) {
      return FrozenMap<Key, int, kOptions>(Fixture::template Pairs<Key>());
    } else {
      return FrozenSet<Key, kOptions>(Fixture::template Keys<Key>());
    }
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind>
void Lookup(benchmark::State& state, Workload workload) {
  static constexpr auto kTable = MakeTable<Key, Size, Map, Kind>();
  const auto queries = frozen_internal::BenchmarkData<Size>::template Queries<Key>();
  std::size_t cursor = 0;
  const auto* table = &kTable;
  benchmark::DoNotOptimize(table);
  for (auto step : state) {
    benchmark::DoNotOptimize(step);
    const auto position = workload == Workload::kMixed ? cursor % queries.size()
                                                       : (2 * (cursor % Size)) + (workload == Workload::kMiss ? 1 : 0);
    auto key = queries.at(position);
    benchmark::DoNotOptimize(key);
    if constexpr (Kind == Layout::kLinear) {
      const auto found = std::ranges::find_if(*table, [&key](const auto& value) {
        if constexpr (Map) {
          return value.first == key;
        } else {
          return value == key;
        }
      });
      if constexpr (Map) {
        benchmark::DoNotOptimize(found == table->end() ? -1 : found->second);
      } else {
        benchmark::DoNotOptimize(found != table->end());
      }
    } else if constexpr (Map) {
      const auto found = table->find(key);
      benchmark::DoNotOptimize(found == table->end() ? -1 : found->second);
    } else {
      benchmark::DoNotOptimize(table->contains(key));
    }
    ++cursor;
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["object_bytes"] = sizeof(kTable);
  if constexpr (Kind == Layout::kMinimal || Kind == Layout::kSparse) {
    state.counters["construction_work"] = kTable.construction_work();
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind>
void RegisterLayout(std::string_view key_name, std::string_view layout_name) {
  const std::string prefix = std::string(Map ? "Map/" : "Set/") + std::string(key_name) + "/" + std::to_string(Size)
                             + "/" + std::string(layout_name);
  benchmark::RegisterBenchmark(prefix + "/hit", Lookup<Key, Size, Map, Kind>, Workload::kHit);
  benchmark::RegisterBenchmark(prefix + "/miss", Lookup<Key, Size, Map, Kind>, Workload::kMiss);
  benchmark::RegisterBenchmark(prefix + "/mixed", Lookup<Key, Size, Map, Kind>, Workload::kMixed);
}

template<typename Key, std::size_t Size, bool Map>
void RegisterShape(std::string_view key_name) {
  RegisterLayout<Key, Size, Map, Layout::kLinear>(key_name, "linear");
  RegisterLayout<Key, Size, Map, Layout::kLimited>(key_name, "limited");
  RegisterLayout<Key, Size, Map, Layout::kMinimal>(key_name, "minimal");
  RegisterLayout<Key, Size, Map, Layout::kSparse>(key_name, "sparse");
}

template<std::size_t Size>
void RegisterSize() {
  RegisterShape<int, Size, false>("int");
  RegisterShape<int, Size, true>("int");
  RegisterShape<std::string_view, Size, false>("string");
  RegisterShape<std::string_view, Size, true>("string");
}

}  // namespace
}  // namespace mbo::container::experimental

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  mbo::container::experimental::RegisterSize<8>();
  mbo::container::experimental::RegisterSize<64>();
  mbo::container::experimental::RegisterSize<256>();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
