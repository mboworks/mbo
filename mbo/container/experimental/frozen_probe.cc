// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <string_view>

#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/internal/frozen_benchmark_data.h"
#include "mbo/container/limited_map.h"

#ifndef MBO_FROZEN_PROBE_LAYOUT
# define MBO_FROZEN_PROBE_LAYOUT 3
#endif
#ifndef MBO_FROZEN_PROBE_SIZE
# define MBO_FROZEN_PROBE_SIZE 64
#endif

namespace mbo::container::experimental {
namespace {
constexpr auto kEntries = frozen_internal::BenchmarkData<MBO_FROZEN_PROBE_SIZE>::Pairs<std::string_view>();
#if MBO_FROZEN_PROBE_LAYOUT == 0
constexpr auto kTable = kEntries;
#elif MBO_FROZEN_PROBE_LAYOUT == 1
constexpr LimitedMap<std::string_view, int, kEntries.size()> kTable(kEntries.begin(), kEntries.end());
#else
constexpr FrozenOptions kOptions{
    .capacity = kEntries.size(),
    .slots = MBO_FROZEN_PROBE_LAYOUT == 2 ? kEntries.size() : 2 * kEntries.size(),
};
constexpr FrozenMap<std::string_view, int, kOptions> kTable(kEntries);
#endif

bool Contains(std::string_view key) {
#if MBO_FROZEN_PROBE_LAYOUT == 0
  return std::ranges::any_of(kTable, [key](const auto& entry) { return entry.first == key; });
#else
  return kTable.contains(key);
#endif
}
}  // namespace
}  // namespace mbo::container::experimental

int main(int argc, char** argv) {
  return mbo::container::experimental::Contains(argc > 1 ? argv[1] : "key-000000") ? 0 : 1;
}

#undef MBO_FROZEN_PROBE_LAYOUT
#undef MBO_FROZEN_PROBE_SIZE
