// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Explicitly include <version> to detect case-insensitive VERSION collisions.
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <version>

#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/hamt/hamt_flat_map.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/segmented_deque.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/strings/string_interner.h"
#include "mbo/types/string_or_view.h"

int main() {
  mbo::container::SegmentedVector<int> sequence;
  sequence.emplace_back(7);
  mbo::container::SegmentedDeque<int> deque;
  deque.emplace_front(8);
  mbo::container::LimitedMap<int, int, 4> limited;
  limited.try_emplace(1, 9);
  static constexpr auto kEntries = std::to_array<std::pair<int, int>>({{1, 10}, {2, 11}});
  static constexpr auto kFrozen = mbo::container::experimental::MakeFrozenMap(kEntries);
  using Map = mbo::container::experimental::hamt::HamtFlatMap<int, int>;
  const auto inserted = Map{}.insert({1, 12});
  const Map snapshot = inserted.first;
  auto editing = snapshot.transient();
  editing.insert({2, 13});
  const auto updated = std::move(editing).persistent();
  mbo::strings::StringInternerSet<std::int32_t> strings;
  const auto interned = strings.insert("value");
  if (!interned) {
    return 1;
  }
  const auto value = strings.get(interned->first);
  mbo::StringOrView text(std::string("owned"));
  const auto owned = std::move(text).str();
  return sequence.front() == 7 && deque.front() == 8 && limited.at(1) == 9 && kFrozen.at(1) == 10
                 && !snapshot.contains(2) && updated.at(2) == 13 && value && *value == "value" && owned == "owned"
             ? 0
             : 1;
}
