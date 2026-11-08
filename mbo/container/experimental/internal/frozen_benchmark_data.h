// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_BENCHMARK_DATA_H_
#define MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_BENCHMARK_DATA_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <utility>

namespace mbo::container::experimental::frozen_internal {

// Static backing bytes keep constexpr string-view fixtures valid across copies and all table layouts.
template<std::size_t Size>
struct BenchmarkData {
  static constexpr auto kBytes = [] {
    std::array<std::array<char, 10>, Size * 2> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      bytes.at(index) = {'k', 'e', 'y', '-', '0', '0', '0', '0', '0', '0'};
      auto number = index;
      for (std::size_t digit = 0; digit < 6; ++digit) {
        bytes.at(index).at(9 - digit) = static_cast<char>('0' + (number % 10));
        number /= 10;
      }
    }
    return bytes;
  }();

  template<typename Key>
  static constexpr auto Queries() {
    std::array<Key, Size * 2> keys{};
    for (std::size_t index = 0; index < keys.size(); ++index) {
      if constexpr (std::same_as<Key, std::string_view>) {
        keys.at(index) = std::string_view(kBytes.at(index).data(), kBytes.at(index).size());
      } else {
        keys.at(index) = static_cast<Key>(index);
      }
    }
    return keys;
  }

  template<typename Key>
  static constexpr auto Keys() {
    const auto queries = Queries<Key>();
    std::array<Key, Size> keys{};
    for (std::size_t index = 0; index < Size; ++index) {
      keys.at(index) = queries.at(index * 2);
    }
    return keys;
  }

  template<typename Key>
  static constexpr auto Pairs() {
    const auto keys = Keys<Key>();
    std::array<std::pair<Key, int>, Size> pairs{};
    for (std::size_t index = 0; index < Size; ++index) {
      pairs.at(index) = {keys.at(index), static_cast<int>(index)};
    }
    return pairs;
  }
};

}  // namespace mbo::container::experimental::frozen_internal

#endif  // MBO_CONTAINER_EXPERIMENTAL_INTERNAL_FROZEN_BENCHMARK_DATA_H_
