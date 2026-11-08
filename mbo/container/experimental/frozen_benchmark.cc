// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <version>

#if defined(__APPLE__)
# include <Availability.h>
#endif

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "benchmark/benchmark.h"
#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/frozen_set.h"
#include "mbo/container/experimental/internal/frozen_benchmark_data.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"
#include "mbo/hash/hash.h"

namespace mbo::container::experimental {
namespace {

void AddFrozenBenchmarkContext(std::string_view experiment) {
#if defined(__clang__)
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_name", "Apple Clang");
# else
  benchmark::AddCustomContext("compiler_name", "Clang");
# endif
  benchmark::AddCustomContext("compiler", std::string("clang-") + std::to_string(__clang_major__));
  benchmark::AddCustomContext(
      "compiler_version", std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "."
                              + std::to_string(__clang_patchlevel__));
  benchmark::AddCustomContext("compiler_version_extra", __clang_version__);
# if defined(__apple_build_version__)
  benchmark::AddCustomContext("compiler_build_version", std::to_string(__apple_build_version__));
# endif
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler_name", "GCC");
  benchmark::AddCustomContext("compiler", std::string("gcc-") + std::to_string(__GNUC__));
  benchmark::AddCustomContext(
      "compiler_version",
      std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__));
  benchmark::AddCustomContext("compiler_version_extra", __VERSION__);
#endif

  benchmark::AddCustomContext("cxx_standard_requested", "c++23");
  benchmark::AddCustomContext("cplusplus", std::to_string(__cplusplus));
#if defined(_LIBCPP_VERSION)
  benchmark::AddCustomContext("standard_library", "libc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(_LIBCPP_VERSION));
#elif defined(__GLIBCXX__)
  benchmark::AddCustomContext("standard_library", "libstdc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(__GLIBCXX__));
# if defined(_GLIBCXX_RELEASE)
  benchmark::AddCustomContext("standard_library_release", std::to_string(_GLIBCXX_RELEASE));
# endif
#endif

#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
  benchmark::AddCustomContext("macos_deployment_target", std::to_string(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__));
#endif
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED)
  benchmark::AddCustomContext("macos_sdk_maximum", std::to_string(__MAC_OS_X_VERSION_MAX_ALLOWED));
#endif
  benchmark::AddCustomContext("experiment", std::string(experiment));
}

enum class Layout {
  kLinear,
  kLimited,
  kMinimal,
  kSparse,
  kStdUnordered,
  kAbslFlat,
  kAbslNode,
  kStdCustomHash,
  kAbslFlatCustomHash,
  kAbslNodeCustomHash
};
enum class Workload { kHit, kMiss, kMixed };
enum class Operation { kFind, kContains, kCount, kEqualRange, kAt };

template<typename Key, bool Map, Layout Kind, typename Hash>
auto EmptyDynamicTable() {
  if constexpr (Kind == Layout::kStdUnordered) {
    return std::conditional_t<Map, std::unordered_map<Key, int>, std::unordered_set<Key>>{};
  } else if constexpr (Kind == Layout::kAbslFlat) {
    return std::conditional_t<Map, absl::flat_hash_map<Key, int>, absl::flat_hash_set<Key>>{};
  } else if constexpr (Kind == Layout::kAbslNode) {
    return std::conditional_t<Map, absl::node_hash_map<Key, int>, absl::node_hash_set<Key>>{};
  } else if constexpr (Kind == Layout::kStdCustomHash) {
    return std::conditional_t<Map, std::unordered_map<Key, int, Hash>, std::unordered_set<Key, Hash>>{};
  } else if constexpr (Kind == Layout::kAbslFlatCustomHash) {
    return std::conditional_t<Map, absl::flat_hash_map<Key, int, Hash>, absl::flat_hash_set<Key, Hash>>{};
  } else {
    static_assert(Kind == Layout::kAbslNodeCustomHash);
    return std::conditional_t<Map, absl::node_hash_map<Key, int, Hash>, absl::node_hash_set<Key, Hash>>{};
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, typename Hash = FrozenHash<Key>>
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
  } else if constexpr (Kind == Layout::kMinimal || Kind == Layout::kSparse) {
    constexpr FrozenOptions kOptions{.capacity = Size, .slots = Kind == Layout::kMinimal ? Size : Size * 2};
    if constexpr (Map) {
      return FrozenMap<Key, int, kOptions, Hash>(Fixture::template Pairs<Key>());
    } else {
      return FrozenSet<Key, kOptions, Hash>(Fixture::template Keys<Key>());
    }
  } else {
    auto table = EmptyDynamicTable<Key, Map, Kind, Hash>();
    table.reserve(Size);
    if constexpr (Map) {
      const auto entries = Fixture::template Pairs<Key>();
      table.insert(entries.begin(), entries.end());
    } else {
      const auto entries = Fixture::template Keys<Key>();
      table.insert(entries.begin(), entries.end());
    }
    return table;
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, typename Hash = FrozenHash<Key>>
const auto& GetTable() {
  if constexpr (
      Kind != Layout::kLinear && Kind != Layout::kLimited && Kind != Layout::kMinimal && Kind != Layout::kSparse) {
    // Static runtime setup is shared by all read operations and happens before the timed loop.
    static const auto kTable = MakeTable<Key, Size, Map, Kind, Hash>();
    return kTable;
  } else {
    static constexpr auto kTable = MakeTable<Key, Size, Map, Kind, Hash>();
    return kTable;
  }
}

template<bool Map, typename Table, typename Key>
auto Find(const Table& table, const Key& key) {
  if constexpr (requires { table.find(key); }) {
    return table.find(key);
  } else {
    return std::ranges::find_if(table, [&key](const auto& value) {
      if constexpr (Map) {
        return value.first == key;
      } else {
        return value == key;
      }
    });
  }
}

template<bool Map, typename Table, typename Key>
bool Contains(const Table& table, const Key& key) {
  if constexpr (requires { table.contains(key); }) {
    return table.contains(key);
  } else {
    return Find<Map>(table, key) != table.end();
  }
}

template<bool Map, typename Table, typename Key>
std::size_t Count(const Table& table, const Key& key) {
  if constexpr (requires { table.count(key); }) {
    return table.count(key);
  } else {
    return Contains<Map>(table, key) ? 1 : 0;
  }
}

template<bool Map, typename Table, typename Key>
auto EqualRange(const Table& table, const Key& key) {
  if constexpr (requires { table.equal_range(key); }) {
    return table.equal_range(key);
  } else {
    const auto found = Find<Map>(table, key);
    return std::pair{found, found == table.end() ? found : std::next(found)};
  }
}

template<typename Table, typename Key>
int At(const Table& table, const Key& key) {
  if constexpr (requires { table.find(key); }) {
    return table.at(key);
  } else {
    // Linear baseline for successful checked map access; callers supply registered keys only.
    const auto found = Find<true>(table, key);
    if (found == table.end()) {
      std::abort();
    }
    return found->second;
  }
}

template<bool Map, typename Table, typename Iterator>
void ConsumeFound(const Table& table, Iterator found) {
  auto present = found != table.end();
  benchmark::DoNotOptimize(present);
  if (present) {
    if constexpr (Map) {
      auto value = found->second;
      benchmark::DoNotOptimize(value);
    } else {
      auto key = *found;
      benchmark::DoNotOptimize(key);
    }
  }
}

// Reject invalid fixtures before timing and warm the same complete query corpus for every layout.
template<typename Key, std::size_t Size, bool Map, typename Table>
bool ValidateTable(const Table& table) {
  if (table.size() != Size || static_cast<std::size_t>(std::distance(table.begin(), table.end())) != Size) {
    return false;
  }
  const auto queries = frozen_internal::BenchmarkData<Size>::template Queries<Key>();
  for (std::size_t index = 0; index < queries.size(); ++index) {
    const auto& key = queries.at(index);
    const bool expected = index % 2 == 0;
    const auto found = Find<Map>(table, key);
    const auto range = EqualRange<Map>(table, key);
    if ((found != table.end()) != expected || Contains<Map>(table, key) != expected
        || Count<Map>(table, key) != static_cast<std::size_t>(expected)
        || (expected ? range.first != found || range.second != std::next(found) : range.first != range.second)) {
      return false;
    }
    if (expected) {
      if constexpr (Map) {
        if (found->first != key || found->second != static_cast<int>(index / 2)
            || At(table, key) != static_cast<int>(index / 2)) {
          return false;
        }
      } else if (*found != key) {
        return false;
      }
    }
  }
  return true;
}

template<typename Table>
void Counters(benchmark::State& state, const Table& table) {
  // Dynamic containers' allocations are excluded: this is sizeof, not total storage consumption.
  state.counters["object_bytes"] = sizeof(table);
  state.counters["elements"] = static_cast<double>(table.size());
  if constexpr (requires {
                  table.bucket_count();
                  table.load_factor();
                }) {
    state.counters["buckets"] = static_cast<double>(table.bucket_count());
    state.counters["load_factor"] = table.load_factor();
  }
  if constexpr (requires { table.construction_work(); }) {
    state.counters["construction_work"] = static_cast<double>(table.construction_work());
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, Operation Op, typename Hash = FrozenHash<Key>>
void Read(benchmark::State& state, Workload workload) {
  const auto* table = &GetTable<Key, Size, Map, Kind, Hash>();
  if (!ValidateTable<Key, Size, Map>(*table)) {
    state.SkipWithError("Invalid read benchmark fixture");
    return;
  }
  const auto queries = frozen_internal::BenchmarkData<Size>::template Queries<Key>();
  std::size_t cursor = 0;
  benchmark::DoNotOptimize(table);
  for (auto step : state) {
    benchmark::DoNotOptimize(step);
    const auto position = workload == Workload::kMixed ? cursor % queries.size()
                                                       : (2 * (cursor % Size)) + (workload == Workload::kMiss ? 1 : 0);
    auto key = queries.at(position);
    benchmark::DoNotOptimize(key);
    if constexpr (Op == Operation::kFind) {
      ConsumeFound<Map>(*table, Find<Map>(*table, key));
    } else if constexpr (Op == Operation::kContains) {
      auto result = Contains<Map>(*table, key);
      benchmark::DoNotOptimize(result);
    } else if constexpr (Op == Operation::kCount) {
      auto result = Count<Map>(*table, key);
      benchmark::DoNotOptimize(result);
    } else if constexpr (Op == Operation::kEqualRange) {
      const auto range = EqualRange<Map>(*table, key);
      ConsumeFound<Map>(*table, range.first == range.second ? table->end() : range.first);
      auto reaches_end = range.second == table->end();
      benchmark::DoNotOptimize(reaches_end);
    } else {
      static_assert(Map && Op == Operation::kAt);
      auto value = At(*table, key);
      benchmark::DoNotOptimize(value);
    }
    ++cursor;
  }
  state.SetItemsProcessed(state.iterations());
  Counters(state, *table);
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, typename Hash = FrozenHash<Key>>
void Iterate(benchmark::State& state) {
  const auto* table = &GetTable<Key, Size, Map, Kind, Hash>();
  if (!ValidateTable<Key, Size, Map>(*table)) {
    state.SkipWithError("Invalid iteration benchmark fixture");
    return;
  }
  for (auto step : state) {
    benchmark::DoNotOptimize(step);
    // Hide the pointer each traversal so a constexpr table cannot become a precomputed result.
    benchmark::DoNotOptimize(table);
    for (const auto& entry : *table) {
      if constexpr (Map) {
        auto key = entry.first;
        auto value = entry.second;
        benchmark::DoNotOptimize(key);
        benchmark::DoNotOptimize(value);
      } else {
        auto key = entry;
        benchmark::DoNotOptimize(key);
      }
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(Size));
  Counters(state, *table);
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, Operation Op, typename Hash = FrozenHash<Key>>
void RegisterRead(const std::string& prefix, std::string_view operation) {
  const auto name = prefix + "/" + std::string(operation);
  benchmark::RegisterBenchmark(name + "/hit", Read<Key, Size, Map, Kind, Op, Hash>, Workload::kHit);
  if constexpr (Op != Operation::kAt) {
    benchmark::RegisterBenchmark(name + "/miss", Read<Key, Size, Map, Kind, Op, Hash>, Workload::kMiss);
    benchmark::RegisterBenchmark(name + "/mixed", Read<Key, Size, Map, Kind, Op, Hash>, Workload::kMixed);
  }
}

template<typename Key, std::size_t Size, bool Map, Layout Kind, typename Hash = FrozenHash<Key>>
void RegisterLayout(std::string_view key_name, std::string_view layout_name) {
  const std::string prefix = std::string(Map ? "Map/" : "Set/") + std::string(key_name) + "/" + std::to_string(Size)
                             + "/" + std::string(layout_name);
  RegisterRead<Key, Size, Map, Kind, Operation::kFind, Hash>(prefix, "find");
  RegisterRead<Key, Size, Map, Kind, Operation::kContains, Hash>(prefix, "contains");
  RegisterRead<Key, Size, Map, Kind, Operation::kCount, Hash>(prefix, "count");
  RegisterRead<Key, Size, Map, Kind, Operation::kEqualRange, Hash>(prefix, "equal_range");
  if constexpr (Map) {
    RegisterRead<Key, Size, Map, Kind, Operation::kAt, Hash>(prefix, "at");
  }
  benchmark::RegisterBenchmark(prefix + "/iterate", Iterate<Key, Size, Map, Kind, Hash>);
}

template<typename Key, std::size_t Size, bool Map>
void RegisterShape(std::string_view key_name) {
  RegisterLayout<Key, Size, Map, Layout::kLinear>(key_name, "linear");
  RegisterLayout<Key, Size, Map, Layout::kLimited>(key_name, "limited");
  RegisterLayout<Key, Size, Map, Layout::kMinimal>(key_name, "minimal");
  RegisterLayout<Key, Size, Map, Layout::kSparse>(key_name, "sparse");
  RegisterLayout<Key, Size, Map, Layout::kStdUnordered>(key_name, "std_unordered");
  RegisterLayout<Key, Size, Map, Layout::kAbslFlat>(key_name, "absl_flat");
  RegisterLayout<Key, Size, Map, Layout::kAbslNode>(key_name, "absl_node");
}

template<std::size_t Size>
void RegisterSize() {
  RegisterShape<int, Size, false>("int");
  RegisterShape<int, Size, true>("int");
  RegisterShape<std::string_view, Size, false>("string");
  RegisterShape<std::string_view, Size, true>("string");
}

template<typename Key, typename Hash>
void HashOnly(benchmark::State& state) {
  const auto queries = frozen_internal::BenchmarkData<64>::template Queries<Key>();
  const Hash hash;
  std::size_t cursor = 0;
  for (auto step : state) {
    benchmark::DoNotOptimize(step);
    auto key = queries.at(cursor++ % queries.size());
    benchmark::DoNotOptimize(key);
    auto code = hash(key);
    benchmark::DoNotOptimize(code);
  }
  state.SetItemsProcessed(state.iterations());
}

template<typename Key, bool Map, typename Hash = FrozenHash<Key>>
void RegisterSameHash(std::string_view key_name, std::string_view hash_name = "frozen_hash") {
  const auto prefix = std::string("Diagnostic/") + (Map ? "Map/" : "Set/") + std::string(key_name) + "/64/";
  benchmark::RegisterBenchmark(
      prefix + "std_" + std::string(hash_name) + "/find/mixed",
      Read<Key, 64, Map, Layout::kStdCustomHash, Operation::kFind, Hash>, Workload::kMixed);
  benchmark::RegisterBenchmark(
      prefix + "absl_flat_" + std::string(hash_name) + "/find/mixed",
      Read<Key, 64, Map, Layout::kAbslFlatCustomHash, Operation::kFind, Hash>, Workload::kMixed);
  benchmark::RegisterBenchmark(
      prefix + "absl_node_" + std::string(hash_name) + "/find/mixed",
      Read<Key, 64, Map, Layout::kAbslNodeCustomHash, Operation::kFind, Hash>, Workload::kMixed);
}

template<typename Key>
void RegisterHashDiagnostics(std::string_view key_name) {
  const auto prefix = std::string("Hash/") + std::string(key_name) + "/";
  benchmark::RegisterBenchmark(prefix + "frozen", HashOnly<Key, FrozenHash<Key>>);
  benchmark::RegisterBenchmark(prefix + "std", HashOnly<Key, std::hash<Key>>);
  benchmark::RegisterBenchmark(prefix + "absl", HashOnly<Key, typename absl::flat_hash_set<Key>::hasher>);
  RegisterSameHash<Key, false>(key_name);
  RegisterSameHash<Key, true>(key_name);
}

template<typename Hash, std::size_t Size, bool Map>
void RegisterMboHashShape(std::string_view name) {
  RegisterLayout<std::string_view, Size, Map, Layout::kMinimal, Hash>("string", "minimal_" + std::string(name));
  RegisterLayout<std::string_view, Size, Map, Layout::kSparse, Hash>("string", "sparse_" + std::string(name));
}

template<typename Hash, std::size_t Size>
void RegisterMboHashSize(std::string_view name) {
  RegisterMboHashShape<Hash, Size, false>(name);
  RegisterMboHashShape<Hash, Size, true>(name);
}

template<typename Algorithm>
void RegisterMboHash(std::string_view name) {
  using Hash = mbo::hash::Hasher<Algorithm>;
  RegisterMboHashSize<Hash, 8>(name);
  RegisterMboHashSize<Hash, 64>(name);
  RegisterMboHashSize<Hash, 256>(name);
  benchmark::RegisterBenchmark("Hash/string/" + std::string(name), HashOnly<std::string_view, Hash>);
  RegisterSameHash<std::string_view, false, Hash>("string", name);
  RegisterSameHash<std::string_view, true, Hash>("string", name);
}

}  // namespace
}  // namespace mbo::container::experimental

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  mbo::container::experimental::AddFrozenBenchmarkContext("frozen-fambo-default-v1");
  mbo::container::experimental::RegisterSize<8>();
  mbo::container::experimental::RegisterSize<64>();
  mbo::container::experimental::RegisterSize<256>();
  mbo::container::experimental::RegisterHashDiagnostics<int>("int");
  mbo::container::experimental::RegisterHashDiagnostics<std::string_view>("string");
  mbo::container::experimental::RegisterMboHash<mbo::hash::mumbo::Algorithm>("mumbo");
  mbo::container::experimental::RegisterMboHash<mbo::hash::fambo::Algorithm>("fambo");
  mbo::container::experimental::RegisterMboHash<mbo::hash::dumbo::Algorithm>("dumbo");
  benchmark::AddCustomContext(
      "setup", "constexpr inline tables; reserve(size) and insert for STL/Abseil, outside timing");
  benchmark::AddCustomContext(
      "hashes", "native defaults unless named: FrozenHash, mumbo, fambo, dumbo; native load-factor policy");
  benchmark::AddCustomContext("frozen_default", "fambo with seed 5381 for string views; integer hashing unchanged");
  benchmark::AddCustomContext(
      "diagnostics", "Hash isolates hash calls; Diagnostic supplies the named hash to STL/Abseil");
  benchmark::AddCustomContext("queries", "cyclic warm corpus; alternating hit/miss for mixed; 10-byte string views");
  benchmark::AddCustomContext("object_bytes", "sizeof(container), excludes dynamic allocations and borrowed key bytes");
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
