// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/any_scan.h"
#include "mbo/container/convert_container.h"
#include "mbo/container/experimental/circular_buffer.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"
#include "mbo/container/limited_vector.h"
#include "mbo/container/segmented_deque.h"
#include "mbo/container/segmented_options.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::SizeIs;

struct ContainerReadmeTest : ::testing::Test {};

TEST_F(ContainerReadmeTest, LimitedContainers) {
  LimitedVector<std::string, 4> tasks;
  tasks.emplace_back("compile");
  tasks.emplace_back("test");
  const auto task = tasks.pop_back_value();
  EXPECT_THAT(task, Eq("test"));
  EXPECT_THAT(tasks, ElementsAre("compile"));

  const LimitedSet<int, 4> priorities{3, 1, 3};
  EXPECT_THAT(priorities, ElementsAre(1, 3));
  LimitedMap<int, std::string, 4> names{{2, "two"}, {1, "one"}};
  names.at(2) = "second";
  EXPECT_THAT(names.at(1), Eq("one"));
  EXPECT_THAT(names.at(2), Eq("second"));
}

TEST_F(ContainerReadmeTest, SegmentedVectorPreservesIteratorAcrossGrowth) {
  constexpr SegmentedOptions kOptions{.segment_size = 64, .segment_capacity = 8, .segment_reservation = 8};
  SegmentedVector<int, kOptions> values;
  values.emplace_back(7);
  const auto first = values.begin();
  values.resize(130, 9);
  const int original = *first;
  const int last = values.pop_back_value();
  EXPECT_THAT(original, Eq(7));
  EXPECT_THAT(last, Eq(9));
  EXPECT_THAT(values, SizeIs(129));
}

TEST_F(ContainerReadmeTest, SegmentedDequeUsesPmrSegmentStorage) {
  std::pmr::monotonic_buffer_resource resource;
  using Queue = SegmentedDeque<int, SegmentedOptions{.segment_size = 64}, memory::PmrBlockSource>;
  Queue queue{memory::PmrBlockSource(&resource)};
  queue.reserve_back(128);
  queue.push_back(1);
  queue.push_front(0);
  const int next = queue.pop_front_value();
  EXPECT_THAT(next, Eq(0));
  EXPECT_THAT(queue, ElementsAre(1));
}

TEST_F(ContainerReadmeTest, CircularBufferTransfersOwnershipAtFront) {
  experimental::CircularBuffer<std::unique_ptr<int>> pending;
  pending.reserve(3);
  pending.push_back(std::make_unique<int>(2));
  pending.push_front(std::make_unique<int>(1));
  const auto item = pending.pop_front_value();
  EXPECT_THAT(item, Pointee(1));
  EXPECT_THAT(pending, ElementsAre(Pointee(2)));
  EXPECT_THAT(pending.capacity(), Eq(4));
}

TEST_F(ContainerReadmeTest, ScanningAndConvertingBorrowedInput) {
  const std::vector<std::string> words{"one", "two"};
  const ConstScan<std::string> scan = MakeConstScan(words);
  std::size_t characters = 0;
  for (const auto& word : scan) {
    characters += word.size();
  }
  const std::vector<std::string_view> views{"three", "four"};
  const std::vector<std::string> owned = ConvertContainer(views);
  EXPECT_THAT(characters, Eq(6));
  EXPECT_THAT(owned, ElementsAre("three", "four"));
}

}  // namespace
}  // namespace mbo::container
