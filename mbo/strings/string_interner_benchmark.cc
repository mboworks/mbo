// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/hash_container_defaults.h"
#include "absl/container/node_hash_map.h"
#include "mbo/config/config.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/arena.h"
#include "mbo/strings/string_interner.h"
#include "mbo/strings/string_interner_policies.h"

namespace mbo::strings {
namespace {

// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): Google Benchmark drives iterations.

using Id = std::uint32_t;
using Hash = absl::DefaultHashContainerHash<std::string_view>;
using Index = absl::flat_hash_map<std::string_view, Id, Hash>;
using Default = StringInternerSet<>;
using Plain = StringInternerSet<Id>;
using SetIndex = StringInternerSet<Id, Hash, absl::flat_hash_set<std::string_view, Hash>>;
using VectorEntries = StringInternerSet<Id, Hash, Index, ArenaStringStorage<>, std::vector<std::string_view>>;
using DequeEntries = StringInternerSet<Id, Hash, Index, ArenaStringStorage<>, std::deque<std::string_view>>;
using ListEntries = StringInternerSet<Id, Hash, Index, ArenaStringStorage<>, std::list<std::string_view>>;
using OptionalPolicy = StringInternerPolicyHolder<StringInternerErrorOptionalPolicy>;
using StatusPolicy = StringInternerPolicyHolder<StringInternerErrorStatusPolicy>;
using StlLookupPolicy = StringInternerPolicyHolder<StringInternerErrorStlPolicy, StringInternerLookupStlPolicy>;
using Optional = StringInternerSet<
    Id,
    Hash,
    Index,
    ArenaStringStorage<>,
    strings_internal::StringInternerEntriesFor<const std::string_view>,
    OptionalPolicy>;
using Status = StringInternerSet<
    Id,
    Hash,
    Index,
    ArenaStringStorage<>,
    strings_internal::StringInternerEntriesFor<const std::string_view>,
    StatusPolicy>;
using StlLookup = StringInternerSet<
    Id,
    Hash,
    Index,
    ArenaStringStorage<>,
    strings_internal::StringInternerEntriesFor<const std::string_view>,
    StlLookupPolicy>;
constexpr mbo::memory::ArenaOptions kLargeArena{
    .initial_block_size = 65'536,
    .maximum_block_size = 1'048'576,
    .growth_numerator = 2,
    .growth_denominator = 1};
using LargeStorage = ArenaStringStorage<mbo::memory::Arena<mbo::memory::NewDeleteBlockSource, kLargeArena>>;
using LargeArena = StringInternerSet<Id, Hash, Index, LargeStorage>;
using Unordered = StringInternerSet<Id, Hash, std::unordered_map<std::string_view, Id, Hash>>;
using Owning = StringInternerSet<Id, Hash, absl::node_hash_map<std::string, Id>>;
using SafePolicy = StringInternerPolicyHolder<
    StringInternerErrorStlPolicy,
    StringInternerLookupMboPolicy,
    StringInternerIndexStlPolicy,
    StringInternerExceptionSafePolicy>;
using SafeMap = StringInternerMap<
    Id,
    std::uint64_t,
    Hash,
    Index,
    ArenaStringStorage<>,
    strings_internal::StringInternerEntriesFor<std::pair<const std::string_view, std::uint64_t>>,
    SafePolicy>;

struct ConstantHash final {
  using is_transparent = void;  // NOLINT(readability-identifier-naming): hash contract.

  static std::size_t operator()(std::string_view /*key*/) noexcept { return 7; }
};

using Colliding = StringInternerSet<Id, ConstantHash>;

struct NonCopyableValue final {
  explicit NonCopyableValue(int input) noexcept : value(input) {}

  NonCopyableValue(const NonCopyableValue&) = delete;
  NonCopyableValue& operator=(const NonCopyableValue&) = delete;
  NonCopyableValue(NonCopyableValue&&) = delete;
  NonCopyableValue& operator=(NonCopyableValue&&) = delete;
  ~NonCopyableValue() = default;
  int value;
};

enum class Operation { kHit, kMiss, kDuplicate, kGet, kGetString, kUnique };

template<typename Interner>
auto InsertOne(Interner& interner, std::string_view key) {
  if constexpr (requires { typename Interner::mapped_type; }) {
    return interner.try_emplace(key, 42);
  } else {
    return interner.insert(key);
  }
}

template<typename Interner>
bool CheckConfiguration() {
  Interner interner;
  const auto first = InsertOne(interner, "check");
  const auto duplicate = InsertOne(interner, "check");
  const auto identifier = typename Interner::id_type{0};
  const auto valid_result = [identifier](const auto& result, bool inserted) {
    if constexpr (requires { result.has_value(); }) {
      return result.has_value() && result->first == identifier && result->second == inserted;
    } else {
      return result.ok() && result->first == identifier && result->second == inserted;
    }
  };
  return valid_result(first, true) && valid_result(duplicate, false) && interner.size() == 1
         && interner.find("check") == identifier && !interner.find("missing")
         && interner.get_string(identifier) == "check";
}

struct Data {
  std::vector<std::string> keys;
  std::vector<std::string> misses;
  std::vector<std::size_t> order;

  Data(std::size_t count, std::size_t length, std::uint64_t seed = 1'729) : keys(count), misses(count), order(count) {
    std::mt19937_64 random(seed);
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
      auto& key = keys.at(ordinal);
      key.resize(length);
      for (char& value : key) {
        value = static_cast<char>('a' + (random() % 26));
      }
      for (std::size_t byte = 0; byte < std::min<std::size_t>(8, length); ++byte) {
        key.at(byte) = static_cast<char>(static_cast<std::uint64_t>(ordinal) >> (byte * 8));
      }
      misses.at(ordinal) = key;
      misses.at(ordinal).back() ^= 0x40;
    }
    std::ranges::iota(order, std::size_t{0});
    std::shuffle(order.begin(), order.end(), random);
  }

  // Timed accesses use indices from the validated permutation, or a population-bounded loop.
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  const std::string& Key(std::size_t ordinal) const noexcept { return keys[ordinal]; }

  const std::string& Miss(std::size_t ordinal) const noexcept { return misses[ordinal]; }

  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
};

template<typename Interner>
void Build(benchmark::State& state, bool reserve) {
  const std::size_t count = state.range(0);
  const Data data(count, state.range(1));
  const auto populate = [&data](Interner& interner) {
    for (const auto ordinal : data.order) {
      benchmark::DoNotOptimize(InsertOne(interner, data.Key(ordinal)));
    }
    benchmark::ClobberMemory();
  };
  for (const auto _ : state) {
    if (reserve) {
      typename Interner::container_type index;
      index.reserve(count);
      Interner interner(typename Interner::options_type{.container = std::move(index)});
      populate(interner);
    } else {
      Interner interner;
      populate(interner);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Interner, Operation Op>
void Lookup(benchmark::State& state) {
  const auto existing_count = static_cast<std::size_t>(state.range(0));
  const Data data(existing_count == 0 ? 256 : existing_count, state.range(1));
  Interner interner;
  for (std::size_t ordinal = 0; ordinal < existing_count; ++ordinal) {
    benchmark::DoNotOptimize(InsertOne(interner, data.Key(ordinal)));
  }
  for (const auto _ : state) {
    for (const auto ordinal : data.order) {
      if constexpr (Op == Operation::kHit) {
        benchmark::DoNotOptimize(interner.find(data.Key(ordinal)));
      }
      if constexpr (Op == Operation::kMiss) {
        benchmark::DoNotOptimize(interner.find(data.Miss(ordinal)));
      }
      if constexpr (Op == Operation::kDuplicate) {
        benchmark::DoNotOptimize(InsertOne(interner, data.Key(ordinal)));
      }
      if constexpr (Op == Operation::kGet) {
        benchmark::DoNotOptimize(interner.get(typename Interner::id_type{static_cast<Id>(ordinal)}));
      }
      if constexpr (Op == Operation::kGetString) {
        benchmark::DoNotOptimize(interner.get_string(typename Interner::id_type{static_cast<Id>(ordinal)}));
      }
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

void InsertPopulated(benchmark::State& state) {
  const auto existing_count = static_cast<std::size_t>(state.range(0));
  const auto added_count = static_cast<std::size_t>(state.range(1));
  const Data existing(existing_count, state.range(2));
  const Data added(added_count, state.range(2), 2'729);
  for (const auto _ : state) {
    state.PauseTiming();
    auto interner = std::make_unique<Default>();
    for (const auto& key : existing.keys) {
      benchmark::DoNotOptimize(interner->insert(key));
    }
    state.ResumeTiming();
    for (const auto ordinal : added.order) {
      benchmark::DoNotOptimize(interner->insert(added.Key(ordinal)));
    }
    state.PauseTiming();
    interner.reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(added_count));
  state.counters["existing_strings"] = static_cast<double>(existing_count);
}

template<typename Interner>
void Iterate(benchmark::State& state) {
  const Data data(state.range(0), 16);
  Interner interner;
  for (const auto& key : data.keys) {
    benchmark::DoNotOptimize(InsertOne(interner, key));
  }
  for (const auto _ : state) {
    for (const auto& key : interner) {
      const auto* address = std::addressof(key);
      benchmark::DoNotOptimize(address);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

void DirectFind(benchmark::State& state, bool miss) {
  const Data data(state.range(0), state.range(1));
  Index index;
  for (std::size_t ordinal = 0; ordinal < data.keys.size(); ++ordinal) {
    index.emplace(data.Key(ordinal), ordinal);
  }
  for (const auto _ : state) {
    for (const auto ordinal : data.order) {
      benchmark::DoNotOptimize(index.find(miss ? data.Miss(ordinal) : data.Key(ordinal)));
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

template<typename Storage>
void Store(benchmark::State& state) {
  const Data data(state.range(0), state.range(1));
  for (const auto _ : state) {
    Storage storage;
    for (const auto ordinal : data.order) {
      benchmark::DoNotOptimize(storage.try_store(data.Key(ordinal)));
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

void HashOnly(benchmark::State& state) {
  const Data data(state.range(0), state.range(1));
  const Hash hash;
  for (const auto _ : state) {
    for (const auto ordinal : data.order) {
      benchmark::DoNotOptimize(hash(data.Key(ordinal)));
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

void SpecialKey(benchmark::State& state, bool embedded_nul) {
  const std::string_view key = embedded_nul ? std::string_view("a\0b", 3) : std::string_view{};
  Plain interner;
  benchmark::DoNotOptimize(interner.insert(key));
  for (const auto _ : state) {
    benchmark::DoNotOptimize(interner.insert(key));
  }
}

void ParentLookup(benchmark::State& state, int location) {
  const auto depth = static_cast<std::size_t>(state.range(0));
  const auto parent_count = static_cast<std::size_t>(state.range(2));
  const auto local_count = static_cast<std::size_t>(state.range(3));
  const auto root_count = depth == 0 ? local_count : parent_count;
  const auto query_count = location == 0 ? root_count : (location == 1 ? local_count : 256);
  const Data data(query_count, state.range(1));
  std::vector<std::unique_ptr<Plain>> chain;
  for (std::size_t layer = 0; layer <= depth; ++layer) {
    chain.push_back(std::make_unique<Plain>(layer != 0 ? chain.back().get() : nullptr));
    const bool holds_queries = (layer == 0 && location == 0) || (layer == depth && location == 1);
    if (holds_queries) {
      for (const auto& key : data.keys) {
        benchmark::DoNotOptimize(chain.back()->insert(key));
      }
    } else {
      const Data unrelated(layer == depth ? local_count : parent_count, state.range(1), 3'729 + layer);
      for (const auto& key : unrelated.keys) {
        benchmark::DoNotOptimize(chain.back()->insert(key));
      }
    }
  }
  for (const auto _ : state) {
    for (const auto ordinal : data.order) {
      benchmark::DoNotOptimize(chain.back()->find(data.Key(ordinal)));
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
  state.counters["parent_links"] = static_cast<double>(depth);
  state.counters["strings_per_parent"] = static_cast<double>(parent_count);
  state.counters["local_strings"] = static_cast<double>(local_count);
}

void RegisterParentCases() {
  constexpr auto kLengths = std::to_array<int>({16, 64, 1'024});
  constexpr auto kDepths = std::to_array<int>({0, 1, 2, 4});
  constexpr auto kPopulations = std::to_array<int>({0, 1, 32, 256});
  for (const auto length : kLengths) {
    const auto add = [length](int depth, int parent_count, int local_count) {
      if ((depth == 0 ? local_count : parent_count) > 0) {
        benchmark::RegisterBenchmark("Parent/RootHit", &ParentLookup, 0)
            ->Args({depth, length, parent_count, local_count});
      }
      if (local_count > 0) {
        benchmark::RegisterBenchmark("Parent/LocalHit", &ParentLookup, 1)
            ->Args({depth, length, parent_count, local_count});
      }
      benchmark::RegisterBenchmark("Parent/Miss", &ParentLookup, 2)->Args({depth, length, parent_count, local_count});
    };
    for (const auto depth : kDepths) {
      for (const auto population : kPopulations) {
        add(depth, population, population);
      }
      if (depth > 0) {
        add(depth, 256, 0);
        add(depth, 0, 256);
        add(depth, 4'096, 32);
      }
    }
  }
}

void RegisterPopulationCases() {
  constexpr auto kCounts = std::to_array<int>({0, 1, 16, 256, 65'536});
  constexpr auto kLengths = std::to_array<int>({16, 64, 1'024});
  for (const auto count : kCounts) {
    for (const auto length : kLengths) {
      benchmark::RegisterBenchmark("Miss/Default", &Lookup<Default, Operation::kMiss>)->Args({count, length});
      benchmark::RegisterBenchmark("Unique/Default", &InsertPopulated)->Args({count, 1'024, length});
      if (count > 0) {
        if (count != 256 || length != 16) {
          benchmark::RegisterBenchmark("Build/Default", &Build<Default>, false)->Args({count, length});
        }
        benchmark::RegisterBenchmark("BuildReserved/Default", &Build<Default>, true)->Args({count, length});
        benchmark::RegisterBenchmark("Hit/Default", &Lookup<Default, Operation::kHit>)->Args({count, length});
        benchmark::RegisterBenchmark("Duplicate/Default", &Lookup<Default, Operation::kDuplicate>)
            ->Args({count, length});
        benchmark::RegisterBenchmark("Get/Default", &Lookup<Default, Operation::kGet>)->Args({count, length});
      }
    }
  }
}

struct CountingHash {
  using is_transparent = void;
  std::size_t* count = nullptr;

  std::size_t operator()(std::string_view key) const noexcept {
    ++*count;
    return Hash{}(key);
  }
};

struct CountingEqual {
  using is_transparent = void;
  std::size_t* count = nullptr;

  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    ++*count;
    return lhs == rhs;
  }
};

// NOLINTBEGIN(readability-identifier-naming): adapter implements the Storage contract.
struct ExhaustedStorage final {
  static std::optional<std::string_view> try_store(std::string_view /*key*/) noexcept { return std::nullopt; }
};

// NOLINTEND(readability-identifier-naming)

template<typename ErrorPolicy>
void CharacterFailure(benchmark::State& state) {
  using Policy = StringInternerPolicyHolder<ErrorPolicy>;
  using Interner = StringInternerSet<
      Id, Hash, Index, ExhaustedStorage, strings_internal::StringInternerEntriesFor<const std::string_view>, Policy>;
  Interner interner;
  for (const auto _ : state) {
    benchmark::DoNotOptimize(interner.insert("full"));
  }
}

void IdFailure(benchmark::State& state) {
  StringInternerSet<std::uint8_t> interner;
  for (unsigned ordinal = 0; ordinal < 256; ++ordinal) {
    benchmark::DoNotOptimize(interner.insert(std::to_string(ordinal)));
  }
  for (const auto _ : state) {
    benchmark::DoNotOptimize(interner.insert("full"));
  }
}

void Counts(benchmark::State& state) {
  using CountIndex = absl::flat_hash_map<std::string_view, Id, CountingHash, CountingEqual>;
  using CountInterner = StringInternerSet<Id, CountingHash, CountIndex>;
  std::size_t hashes = 0;
  std::size_t equals = 0;
  CountIndex index(0, CountingHash{&hashes}, CountingEqual{&equals});
  index.reserve(4'096);
  CountInterner interner(CountInterner::options_type{.container = std::move(index)});
  const Data data(2'048, 64);
  for (const auto& key : data.keys) {
    benchmark::DoNotOptimize(interner.insert(key));
  }
  hashes = equals = 0;
  for (const auto _ : state) {
    for (const auto ordinal : data.order) {
      switch (state.range(0)) {
        case 0: benchmark::DoNotOptimize(interner.find(data.Key(ordinal))); break;
        case 1: benchmark::DoNotOptimize(interner.find(data.Miss(ordinal))); break;
        case 2: benchmark::DoNotOptimize(interner.insert(data.Key(ordinal))); break;
        case 3: benchmark::DoNotOptimize(interner.insert(data.Miss(ordinal))); break;
        case 4: benchmark::DoNotOptimize(interner.get(static_cast<Id>(ordinal))); break;
        default: std::abort();
      }
    }
  }
  const auto operations = static_cast<double>(state.iterations()) * static_cast<double>(data.keys.size());
  state.counters["hashes_per_operation"] = static_cast<double>(hashes) / operations;
  state.counters["equalities_per_operation"] = static_cast<double>(equals) / operations;
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(data.keys.size()));
}

void Layout(benchmark::State& state) {
  using Map = StringInternerMap<Id, std::uint64_t>;
  using OwningMap = StringInternerMap<Id, std::uint64_t, void, absl::node_hash_map<std::string, Id>>;
  for (const auto _ : state) {
    benchmark::ClobberMemory();
  }
  state.counters["interner_bytes"] = sizeof(Default);
  state.counters["expected_bytes"] = sizeof(Plain::insertion_result);
  state.counters["optional_bytes"] = sizeof(Optional::insertion_result);
  state.counters["status_bytes"] = sizeof(Status::insertion_result);
  state.counters["mbo_lookup_bytes"] = sizeof(Map::lookup_result);
  state.counters["stl_lookup_bytes"] = sizeof(std::optional<std::reference_wrapper<std::uint64_t>>);
  state.counters["index_slot_bytes"] = sizeof(Index::value_type);
  state.counters["set_dense_slot_bytes"] = sizeof(std::string_view);
  state.counters["map_dense_slot_bytes"] =
      sizeof(strings_internal::StringInternerElementTraits<Map::element_type>::Entry);
  state.counters["owning_map_dense_slot_bytes"] =
      sizeof(strings_internal::StringInternerElementTraits<OwningMap::element_type>::Entry);
}

template<typename Interner>
void Register(const std::string& name, bool extras = false) {
  if (!CheckConfiguration<Interner>()) {
    std::abort();
  }
  const auto sizes = std::to_array<int>({16, 64, 1'024});
  for (const int length : sizes) {
    benchmark::RegisterBenchmark("Build/" + name, &Build<Interner>, false)->Args({4'096, length});
    benchmark::RegisterBenchmark("BuildReserved/" + name, &Build<Interner>, true)->Args({4'096, length});
    benchmark::RegisterBenchmark("Hit/" + name, &Lookup<Interner, Operation::kHit>)->Args({4'096, length});
    benchmark::RegisterBenchmark("Miss/" + name, &Lookup<Interner, Operation::kMiss>)->Args({4'096, length});
    benchmark::RegisterBenchmark("Duplicate/" + name, &Lookup<Interner, Operation::kDuplicate>)->Args({4'096, length});
  }
  benchmark::RegisterBenchmark("Get/" + name, &Lookup<Interner, Operation::kGet>)->Args({4'096, 16});
  benchmark::RegisterBenchmark("GetString/" + name, &Lookup<Interner, Operation::kGetString>)->Args({4'096, 16});
  if (extras) {
    benchmark::RegisterBenchmark("Iterate/" + name, &Iterate<Interner>)->Arg(4'096);
  }
}

void RegisterBenchmarks() {
  Register<Default>("Default", true);
  Register<Plain>("Plain", true);
  Register<Optional>("Optional");
  Register<Status>("Status");
  Register<StlLookup>("StlLookup");
  Register<LargeArena>("LargeArena");
  Register<Unordered>("Unordered");
  Register<Owning>("Owning");
  Register<VectorEntries>("Vector", true);
  Register<DequeEntries>("Deque", true);
  benchmark::RegisterBenchmark("Build/Default", &Build<Default>, false)->Args({256, 16});
  RegisterPopulationCases();
  RegisterParentCases();
  benchmark::RegisterBenchmark("Map/Build", &Build<StringInternerMap<Id, std::uint64_t>>, false)->Args({4'096, 16});
  benchmark::RegisterBenchmark("Map/Hit", &Lookup<StringInternerMap<Id, std::uint64_t>, Operation::kHit>)
      ->Args({4'096, 16});
  benchmark::RegisterBenchmark("Map/Get", &Lookup<StringInternerMap<Id, std::uint64_t>, Operation::kGet>)
      ->Args({4'096, 16});
  benchmark::RegisterBenchmark("Map/Duplicate", &Lookup<StringInternerMap<Id, std::uint64_t>, Operation::kDuplicate>)
      ->Args({4'096, 16});
  benchmark::RegisterBenchmark("Map/IterateNonCopyable", &Iterate<StringInternerMap<Id, NonCopyableValue>>)->Arg(4'096);
  benchmark::RegisterBenchmark("Map/SafeBuild", &Build<SafeMap>, false)->Args({4'096, 16});
  benchmark::RegisterBenchmark("Map/SafeDuplicate", &Lookup<SafeMap, Operation::kDuplicate>)->Args({4'096, 16});
  benchmark::RegisterBenchmark("Key/EmptyDuplicate", &SpecialKey, false);
  benchmark::RegisterBenchmark("Key/EmbeddedNulDuplicate", &SpecialKey, true);
  benchmark::RegisterBenchmark("Hit/Colliding", &Lookup<Colliding, Operation::kHit>)->Args({256, 16});
  benchmark::RegisterBenchmark("Miss/Colliding", &Lookup<Colliding, Operation::kMiss>)->Args({256, 16});
  benchmark::RegisterBenchmark("Error/CharacterExpected", &CharacterFailure<StringInternerErrorStlPolicy>);
  benchmark::RegisterBenchmark("Error/CharacterOptional", &CharacterFailure<StringInternerErrorOptionalPolicy>);
  benchmark::RegisterBenchmark("Error/CharacterStatus", &CharacterFailure<StringInternerErrorStatusPolicy>);
  benchmark::RegisterBenchmark("Error/Id", &IdFailure);
  benchmark::RegisterBenchmark("Layout", &Layout)->Iterations(1);
  benchmark::RegisterBenchmark("Counts/Hit", &Counts)->Arg(0)->Iterations(1);
  benchmark::RegisterBenchmark("Counts/Miss", &Counts)->Arg(1)->Iterations(1);
  benchmark::RegisterBenchmark("Counts/Duplicate", &Counts)->Arg(2)->Iterations(1);
  benchmark::RegisterBenchmark("Counts/Unique", &Counts)->Arg(3)->Iterations(1);
  benchmark::RegisterBenchmark("Counts/Get", &Counts)->Arg(4)->Iterations(1);
  const auto counts = std::to_array<int>({256, 4'096});
  for (const int count : counts) {
    benchmark::RegisterBenchmark("Hit/SetIndex", &Lookup<SetIndex, Operation::kHit>)->Args({count, 16});
    benchmark::RegisterBenchmark("Get/List", &Lookup<ListEntries, Operation::kGet>)->Args({count, 16});
    benchmark::RegisterBenchmark("Iterate/List", &Iterate<ListEntries>)->Arg(count);
  }
  const auto lengths = std::to_array<int>({16, 64, 1'024});
  for (const int length : lengths) {
    benchmark::RegisterBenchmark("Storage/Arena", &Store<ArenaStringStorage<>>)->Args({4'096, length});
    benchmark::RegisterBenchmark("Storage/LargeArena", &Store<LargeStorage>)->Args({4'096, length});
    benchmark::RegisterBenchmark("Hash/Default", &HashOnly)->Args({4'096, length});
    benchmark::RegisterBenchmark("Hit/DirectIndex", &DirectFind, false)->Args({4'096, length});
    benchmark::RegisterBenchmark("Miss/DirectIndex", &DirectFind, true)->Args({4'096, length});
  }
}

void AddContext() {
#if defined(__clang__)
  benchmark::AddCustomContext("compiler", "clang-" + std::to_string(__clang_major__));
  benchmark::AddCustomContext("compiler_name", "Clang");
  benchmark::AddCustomContext(
      "compiler_version", std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "."
                              + std::to_string(__clang_patchlevel__));
  benchmark::AddCustomContext("compiler_version_extra", __clang_version__);
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler", "gcc-" + std::to_string(__GNUC__));
  benchmark::AddCustomContext("compiler_name", "GCC");
  benchmark::AddCustomContext(
      "compiler_version",
      std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__));
  benchmark::AddCustomContext("compiler_version_extra", __VERSION__);
#endif
#if defined(_LIBCPP_VERSION)
  benchmark::AddCustomContext("standard_library", "libc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(_LIBCPP_VERSION));
#elif defined(__GLIBCXX__)
  benchmark::AddCustomContext("standard_library", "libstdc++");
  benchmark::AddCustomContext("standard_library_version", std::to_string(__GLIBCXX__));
#endif
  benchmark::AddCustomContext("cxx_standard_requested", "c++23");
  benchmark::AddCustomContext("cplusplus", std::to_string(__cplusplus));
  benchmark::AddCustomContext("exceptions_enabled", config::kExceptionsEnabled ? "true" : "false");
  benchmark::AddCustomContext("experiment", "StringInterner");
  benchmark::AddCustomContext("key_order", "mt19937_64 seed 1729, shuffled, exact-length owned queries");
}

// NOLINTEND(clang-analyzer-deadcode.DeadStores)

}  // namespace
}  // namespace mbo::strings

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  mbo::strings::RegisterBenchmarks();
  mbo::strings::AddContext();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
