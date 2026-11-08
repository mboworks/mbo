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

#ifndef MBO_CONTAINER_LIMITED_SET_BENCHMARK_H_
#define MBO_CONTAINER_LIMITED_SET_BENCHMARK_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "benchmark/benchmark.h"
#include "mbo/container/limited_options.h"
#include "mbo/container/limited_set.h"
#include "mbo/container/limited_vector.h"

namespace mbo::container {

// NOLINTBEGIN(*-avoid-unchecked-container-access): benchmark harness - the
// timed loops measure the container's access itself, and every index wraps
// within the generated test data by construction.

class Random {
 public:
  static constexpr int kUniformValueMin = std::numeric_limits<int>::min();
  static constexpr int kUniformValueMax = std::numeric_limits<int>::max();

  Random()
      : mt_(kRandomInit),  // NOLINT(cert-msc32-c,cert-msc51-cpp)
        uniform_(kUniformValueMin, kUniformValueMax) {}

  int Uniform() { return uniform_(mt_); }

 private:
  static constexpr int kRandomInit = 42;  // Benhchmarks must be repeatable.

  std::mt19937 mt_;
  std::uniform_int_distribution<int> uniform_;
};

template<std::size_t Size, bool HaveOrMiss, typename Compare, LimitedOptionsFlag... Flags>
class Benchmarks {
 private:
  enum class Function { kContains, kFind, kIndexOf };

  // The comparator arrives as a concrete type rather than a template to instantiate.
  // The caller then picks the form it wants - `std::less<>` transparent, or
  // `CompareLess<int>` - instead of this header hard-coding `Compare<int>` and forcing
  // the non-transparent spelling on everything.
  using BenchmarkedContainer = LimitedSet<int, LimitedOptions<Size, Flags...>{}, Compare>;

  static constexpr std::size_t kNumTestsValues = 100'000;

  static BenchmarkedContainer GetData(Random& random) {
    BenchmarkedContainer data;
    while (data.size() < Size) {
      data.insert(random.Uniform());
    }
    return data;
  }

  static std::vector<int> GetInput(Random& random, const BenchmarkedContainer& data) {
    if (HaveOrMiss) {
      return std::vector<int>{data.begin(), data.end()};
    }
    std::vector<int> input;
    while (input.size() < kNumTestsValues) {
      const int value = random.Uniform();
      if (!data.contains(value)) {
        input.push_back(value);
      }
    }
    return input;
  }

  template<Function func>
  static void Benchmark(benchmark::State& state) {
    Random random;  // One per benchmark function instantiation means all functions use the same data.

    struct TestData {
      explicit TestData(Random& random) : data(GetData(random)), input(GetInput(random, data)) {}

      const BenchmarkedContainer data;
      const std::vector<int> input;
    };

    static constexpr std::size_t kNumTestDataSets = 113;  // Number of test data sets.
    const std::vector<TestData> test_data = [&] {
      std::vector<TestData> test_data;
      test_data.reserve(kNumTestDataSets);
      while (test_data.size() < kNumTestDataSets) {
        test_data.emplace_back(random);
      }
      return test_data;
    }();
    std::size_t test = 0;
    std::size_t item = 0;
    int64_t item_count = 0;
    const auto* data = &test_data[0].data;
    const auto* input = &test_data[0].input;
    // NOLINTNEXTLINE(readability-identifier-length)
    for (const auto _ : state) {
      if constexpr (func == Function::kContains) {
        if (HaveOrMiss != data->contains((*input)[item])) {
          state.SkipWithError("BAD_RESULT");
          break;
        }
      } else if constexpr (func == Function::kFind) {
        if (HaveOrMiss != (data->find((*input)[item]) != data->end())) {
          state.SkipWithError("BAD_RESULT");
          break;
        }
      } else if constexpr (func == Function::kIndexOf) {
        if (HaveOrMiss != (data->index_of((*input)[item]) != BenchmarkedContainer::npos)) {
          state.SkipWithError("BAD_RESULT");
          break;
        }
      } else {
        state.SkipWithError("NOT IMPLEMENTED");
        break;
      }
      // state.PauseTiming();
      ++item_count;
      if (++item >= input->size()) {
        item = 0;
        ++test;
        test %= test_data.size();
        data = &test_data[test].data;
        input = &test_data[test].input;
      }
      // state.ResumeTiming();
    }
    state.SetItemsProcessed(item_count);
    state.counters["Size"] = Size;
  }

 public:
  static void BmContains(benchmark::State& state) { Benchmark<Function::kContains>(state); }

  static void BmFind(benchmark::State& state) { Benchmark<Function::kFind>(state); }

  static void BmIndexOf(benchmark::State& state) { Benchmark<Function::kIndexOf>(state); }
};

template<bool HaveOrMiss>
std::string MakeName(std::string_view compare, std::string_view flags, std::string_view func) {
  return absl::StrJoin(
      std::initializer_list<std::string_view>{
          absl::StripPrefix(func, "Bm"),
          compare,
          absl::StripPrefix(flags, "LimitedOptionsFlag::k"),
          HaveOrMiss ? "Good" : "Miss",
      },
      ", ");
}

// NOLINTEND(*-avoid-unchecked-container-access)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_LIMITED_SET_BENCHMARK_H_
