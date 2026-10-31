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

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

#include "benchmark/benchmark.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"
#include "mbo/types/compare.h"

namespace mbo::container {
namespace {

enum class ContainerKind { kMap, kSet };
enum class Function { kIndexOf, kLowerBound, kUpperBound };
enum class Scenario { kBefore, kFirst, kMiddleMiss, kMiddleHit, kLast, kAfter };

template<ContainerKind Kind, std::size_t Size, typename Compare>
struct ContainerTraits;

template<std::size_t Size, typename Compare>
struct ContainerTraits<ContainerKind::kSet, Size, Compare> {
  using Type = LimitedSet<int, Size, Compare>;

  static void Insert(Type& data, int key) { data.insert(key); }

  static int KeyAt(const Type& data, std::size_t pos) { return data.at_index(pos); }
};

template<std::size_t Size, typename Compare>
struct ContainerTraits<ContainerKind::kMap, Size, Compare> {
  using Type = LimitedMap<int, int, Size, Compare>;

  static void Insert(Type& data, int key) { data.insert(std::make_pair(key, key)); }

  static int KeyAt(const Type& data, std::size_t pos) { return data.at_index(pos).first; }
};

template<ContainerKind Kind, std::size_t Size, typename Compare, Function Func, Scenario Case>
void OrderedLookup(benchmark::State& state) {
  using Traits = ContainerTraits<Kind, Size, Compare>;
  using Container = Traits::Type;
  Container data;
  for (std::size_t pos = 0; pos < Size; ++pos) {
    Traits::Insert(data, static_cast<int>(pos * 2));
  }
  constexpr bool kAscending = !std::is_same_v<Compare, std::greater<>>;
  const std::size_t middle = Size / 2;
  int key = [&] {
    if constexpr (Case == Scenario::kBefore) {
      return kAscending ? -1 : static_cast<int>((Size * 2) + 1);
    }
    if constexpr (Case == Scenario::kFirst) {
      return Traits::KeyAt(data, 0);
    }
    if constexpr (Case == Scenario::kMiddleMiss) {
      return (Traits::KeyAt(data, middle - (middle == Size - 1)) + Traits::KeyAt(data, middle)) / 2;
    }
    if constexpr (Case == Scenario::kMiddleHit) {
      return Traits::KeyAt(data, middle);
    }
    if constexpr (Case == Scenario::kLast) {
      return Traits::KeyAt(data, Size - 1);
    }
    return kAscending ? static_cast<int>((Size * 2) + 1) : -1;
  }();
  benchmark::DoNotOptimize(data);
  benchmark::DoNotOptimize(key);
  // NOLINTNEXTLINE(readability-identifier-length)
  for (const auto _ : state) {
    if constexpr (Func == Function::kIndexOf) {
      benchmark::DoNotOptimize(data.index_of(key));
    } else if constexpr (Func == Function::kLowerBound) {
      benchmark::DoNotOptimize(data.lower_bound(key));
    } else {
      benchmark::DoNotOptimize(data.upper_bound(key));
    }
  }
  state.SetItemsProcessed(state.iterations());
}

// NOLINTBEGIN(*-macro-usage,*-avoid-non-const-global-variables,*-owning-memory)
#define MBO_BENCH_ONE(Kind, Size, Compare, Func, Case)                                                  \
  BENCHMARK(OrderedLookup<ContainerKind::k##Kind, Size, Compare, Function::k##Func, Scenario::k##Case>) \
      ->Name(#Kind "/" #Size "/" #Compare "/" #Func "/" #Case)
#define MBO_BENCH_CASES(Kind, Size, Compare, Func)      \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, Before);     \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, First);      \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, MiddleMiss); \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, MiddleHit);  \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, Last);       \
  MBO_BENCH_ONE(Kind, Size, Compare, Func, After)
#define MBO_BENCH_FUNCS(Kind, Size, Compare)        \
  MBO_BENCH_CASES(Kind, Size, Compare, IndexOf);    \
  MBO_BENCH_CASES(Kind, Size, Compare, LowerBound); \
  MBO_BENCH_CASES(Kind, Size, Compare, UpperBound)
#define MBO_BENCH_COMPARE(Kind, Size)          \
  MBO_BENCH_FUNCS(Kind, Size, std::less<>);    \
  MBO_BENCH_FUNCS(Kind, Size, std::greater<>); \
  MBO_BENCH_FUNCS(Kind, Size, mbo::types::CompareLess<int>)
#define MBO_BENCH_SIZE(Size)    \
  MBO_BENCH_COMPARE(Set, Size); \
  MBO_BENCH_COMPARE(Map, Size)

MBO_BENCH_SIZE(2);
MBO_BENCH_SIZE(4);
MBO_BENCH_SIZE(8);
MBO_BENCH_SIZE(16);
MBO_BENCH_SIZE(17);
MBO_BENCH_SIZE(32);

#undef MBO_BENCH_SIZE
#undef MBO_BENCH_COMPARE
#undef MBO_BENCH_FUNCS
#undef MBO_BENCH_CASES
#undef MBO_BENCH_ONE
// NOLINTEND(*-macro-usage,*-avoid-non-const-global-variables,*-owning-memory)

}  // namespace
}  // namespace mbo::container

BENCHMARK_MAIN();  // NOLINT
