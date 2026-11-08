// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/container/experimental/hamt/hamt_node_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_interner_policy.h"
#include "mbo/hash/hash.h"
#include "mbo/strings/string_interner.h"

namespace mbo::container::experimental::hamt {
namespace {

using Id = mbo::strings::StringIdU32<>;
using Hash = mbo::hash::DefaultHasher;
using Policy = mbo::strings::StringInternerPolicyHolder<
    mbo::strings::StringInternerErrorStlPolicy,
    mbo::strings::StringInternerLookupMboPolicy,
    HamtStringInternerIndexPolicy>;

template<typename Index, typename IndexPolicy = Policy, typename Storage = mbo::strings::ArenaStringStorage<>>
using Interner = mbo::strings::
    StringInternerSet<Id, Hash, Index, Storage, mbo::container::SegmentedVector<std::string_view>, IndexPolicy>;

template<std::size_t Bits>
using Flat = Interner<HamtStringIndex<Id, Hash, std::equal_to<>, HamtOptions{.fragment_bits = Bits}>>;
using Node = Interner<HamtNodeStringIndex<Id, Hash>>;
using Abseil = Interner<absl::flat_hash_map<std::string_view, Id, Hash>, mbo::strings::StringInternerPolicyHolder<>>;
constexpr mbo::memory::ArenaOptions kLargeArena{
    .initial_block_size = 65'536,
    .maximum_block_size = 1'048'576,
    .growth_numerator = 2,
    .growth_denominator = 1,
};
using LargeArena = Interner<
    HamtStringIndex<Id, Hash>,
    Policy,
    mbo::strings::ArenaStringStorage<mbo::memory::Arena<mbo::memory::NewDeleteBlockSource, kLargeArena>>>;

enum class Operation { kDuplicate, kGet, kHit, kMiss };

std::vector<std::string> Keys(std::size_t count, std::string_view prefix) {
  std::vector<std::string> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.emplace_back(absl::StrCat(prefix, index));
  }
  return result;
}

template<typename Container>
bool Populate(Container& interner, const std::vector<std::string>& keys) {
  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto inserted = interner.insert(keys.at(index));
    if (!inserted || !inserted->second || inserted->first != Id(static_cast<std::uint32_t>(index))) {
      return false;
    }
  }
  return interner.size() == keys.size();
}

template<typename Container>
void Build(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto keys = Keys(count, "hamt-interner-key-");
  {
    Container check;
    if (!Populate(check, keys) || check.get_string(Id(0)) != keys.front()) {
      state.SkipWithError("generic interner population failed");
      return;
    }
  }
  for (auto unused : state) {
    Container interner;
    for (const auto& key : keys) {
      benchmark::DoNotOptimize(interner.insert(key));
    }
    benchmark::DoNotOptimize(interner.size());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Container, Operation Op>
void Read(benchmark::State& state) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto keys = Keys(count, "hamt-interner-key-");
  const auto missing = Keys(count, "hamt-interner-miss-");
  Container interner;
  if (!Populate(interner, keys)) {
    state.SkipWithError("generic interner population failed");
    return;
  }
  std::vector<std::size_t> order(count);
  std::iota(order.begin(), order.end(), 0);
  std::mt19937 random(42);
  std::shuffle(order.begin(), order.end(), random);
  for (const auto index : order) {
    if (interner.find(keys.at(index)) != Id(static_cast<std::uint32_t>(index)) || interner.find(missing.at(index))
        || interner.insert(keys.at(index))->second) {
      state.SkipWithError("generic interner lookup validation failed");
      return;
    }
  }
  for (auto unused : state) {
    for (const auto index : order) {
      if constexpr (Op == Operation::kDuplicate) {
        benchmark::DoNotOptimize(interner.insert(keys.at(index)));
      } else if constexpr (Op == Operation::kGet) {
        benchmark::DoNotOptimize(interner.get_string(Id(static_cast<std::uint32_t>(index))));
      } else if constexpr (Op == Operation::kHit) {
        benchmark::DoNotOptimize(interner.find(keys.at(index)));
      } else {
        benchmark::DoNotOptimize(interner.find(missing.at(index)));
      }
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Container>
void Register(std::string_view label) {
  const auto add = [label](std::string_view operation, auto function) {
    benchmark::RegisterBenchmark(absl::StrCat("GenericInterner/", label, "/", operation).c_str(), function)
        ->Arg(1'024)
        ->Arg(16'384);
  };
  add("Build", Build<Container>);
  add("Hit", Read<Container, Operation::kHit>);
  add("Miss", Read<Container, Operation::kMiss>);
  add("Duplicate", Read<Container, Operation::kDuplicate>);
  add("Get", Read<Container, Operation::kGet>);
}

const bool kRegistered = [] {
  Register<Flat<4>>("Flat4");
  Register<Flat<5>>("Flat5");
  Register<Flat<6>>("Flat6");
  Register<Flat<7>>("Flat7");
  Register<Node>("Node5");
  Register<LargeArena>("Flat5LargeArena");
  Register<Abseil>("AbseilFlat");
  return true;
}();

}  // namespace
}  // namespace mbo::container::experimental::hamt

BENCHMARK_MAIN();
