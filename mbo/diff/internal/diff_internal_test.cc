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

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/diff/diff_options.h"
#include "mbo/diff/internal/chunk.h"
#include "mbo/diff/internal/context.h"
#include "mbo/diff/internal/data.h"
#include "mbo/diff/internal/output.h"
#include "mbo/diff/internal/update_absl_log_flags.h"

// Unit tests for the diff plumbing: the pieces every algorithm shares. Each class
// gets its contract pinned directly rather than only through a whole-file diff.

namespace mbo::diff::diff_internal {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct DiffInternalTest : ::testing::Test {
  static DiffOptions Options(std::size_t context) {
    return DiffOptions{
        .context_size = context,
        .file_header_use = DiffOptions::FileHeaderUse::kNone,
    };
  }
};

// Context: a bounded ring of recent common lines. -------------------------------

TEST_F(DiffInternalTest, ContextStartsEmpty) {
  const DiffOptions options = Options(2);
  const Context context(options);
  EXPECT_THAT(context.Empty(), IsTrue());
  EXPECT_THAT(context.Full(), IsFalse());
}

TEST_F(DiffInternalTest, ContextFillsToTwiceTheContextSize) {
  const DiffOptions options = Options(2);
  Context context(options);
  // Full() means 2 * context_size entries: enough trailing context to close one
  // chunk plus leading context to open the next.
  EXPECT_THAT(context.Push("1"), IsFalse());
  EXPECT_THAT(context.Push("2"), IsFalse());
  EXPECT_THAT(context.Push("3"), IsFalse());
  EXPECT_THAT(context.Push("4"), IsTrue()) << "full at 2 * context_size";
  EXPECT_THAT(context.Full(), IsTrue());
  EXPECT_THAT(context.HalfFull(), IsTrue());
}

TEST_F(DiffInternalTest, ContextDropsTheOldestOnceFull) {
  const DiffOptions options = Options(1);
  Context context(options);
  context.Push("old");
  context.Push("mid");
  context.Push("new");  // Capacity 2: pushes "old" out.
  EXPECT_THAT(context.PopFront(), "mid");
  EXPECT_THAT(context.PopFront(), "new");
  EXPECT_THAT(context.Empty(), IsTrue());
}

TEST_F(DiffInternalTest, ContextCanBeCleared) {
  const DiffOptions options = Options(1);
  Context context(options);
  context.Push("line");
  context.Clear();
  EXPECT_THAT(context.Empty(), IsTrue());
}

TEST_F(DiffInternalTest, ContextSizeZeroKeepsNothing) {
  const DiffOptions options = Options(0);
  Context context(options);
  EXPECT_THAT(context.Push("a"), IsTrue()) << "a zero-context ring is always 'full'";
  EXPECT_THAT(context.Empty(), IsTrue()) << "and stores nothing";
}

// Data: the preprocessed line cache. --------------------------------------------

TEST_F(DiffInternalTest, DataSplitsLinesAndIterates) {
  const DiffOptions options = Options(0);
  Data data(options, std::nullopt, "a\nb\nc\n");
  EXPECT_THAT(data.Size(), 3);
  EXPECT_THAT(data.Done(), IsFalse());
  EXPECT_THAT(data.Line(), "a");
  EXPECT_THAT(data.Next(), "a");
  EXPECT_THAT(data.Idx(), 1);
  EXPECT_THAT(data.Next(), "b");
  EXPECT_THAT(data.Next(), "c");
  EXPECT_THAT(data.Done(), IsTrue());
  EXPECT_THAT(data.Next(), "") << "exhausted data yields empty lines";
}

TEST_F(DiffInternalTest, DataEmptyTextHasNoLines) {
  const DiffOptions options = Options(0);
  const Data data(options, std::nullopt, "");
  EXPECT_THAT(data.Size(), 0);
  EXPECT_THAT(data.Done(), IsTrue());
}

TEST_F(DiffInternalTest, DataHandlesAMissingFinalNewline) {
  const DiffOptions options = Options(0);
  Data data(options, std::nullopt, "a\nb");
  EXPECT_THAT(data.Size(), 2);
  data.Next();
  EXPECT_THAT(data.Line(), HasSubstr("b")) << "the unterminated last line is still a line";
}

TEST_F(DiffInternalTest, DataCachesTheProcessedComparisonText) {
  DiffOptions options = Options(0);
  options.ignore_trailing_space = true;
  const Data data(options, std::nullopt, "abc  \n");
  // `processed` is what comparisons use; whitespace options are materialised into
  // it while `line` stays as written. (ignore_case is NOT cached: it folds at
  // compare time in BaseDiff, so the cache stays byte-exact for output.)
  EXPECT_THAT(data.GetCache(0).line, "abc  ");
  EXPECT_THAT(data.GetCache(0).processed, "abc");
}

TEST_F(DiffInternalTest, DataAppliesRegexReplace) {
  const DiffOptions options = Options(0);
  const auto replace = DiffOptions::ParseRegexReplaceFlag("/[0-9]+/N/");
  ASSERT_THAT(replace.has_value(), IsTrue());
  const Data data(options, replace, "line 42\n");
  EXPECT_THAT(data.GetCache(0).line, "line 42") << "the original text is preserved";
  EXPECT_THAT(data.GetCache(0).processed, "line N") << "the comparison text is rewritten";
}

// Chunk + AppendChunk: assembling unified output. --------------------------------

TEST_F(DiffInternalTest, ChunkCollectsAndRendersAUnifiedHunk) {
  const DiffOptions options = Options(0);
  Chunk chunk("", options);
  chunk.PushLhs(0, 0, "old");
  chunk.PushRhs(1, 0, "new");
  chunk.MoveDiffs();
  EXPECT_THAT(chunk.MoveOutput(), "@@ -1 +1 @@\n-old\n+new\n");
}

TEST_F(DiffInternalTest, ChunkWithNoDiffsRendersNothing) {
  const DiffOptions options = Options(0);
  Chunk chunk("", options);
  chunk.PushBoth(0, 0, "same");
  chunk.MoveDiffs();
  EXPECT_THAT(chunk.MoveOutput(), IsEmpty());
}

TEST_F(DiffInternalTest, AppendChunkRendersUnifiedFormat) {
  const DiffOptions options = Options(0);
  std::string output;
  AppendChunk(
      output, options, ChunkRange{.lhs_idx = 0, .rhs_idx = 0, .lhs_size = 1, .rhs_size = 1},
      {{.kind = '-', .text = "old"}, {.kind = '+', .text = "new"}});
  EXPECT_THAT(output, "@@ -1 +1 @@\n-old\n+new\n");
}

TEST_F(DiffInternalTest, AppendChunkRendersContextLines) {
  const DiffOptions options = Options(1);
  std::string output;
  AppendChunk(
      output, options, ChunkRange{.lhs_idx = 0, .rhs_idx = 0, .lhs_size = 3, .rhs_size = 3},
      {{.kind = ' ', .text = "a"}, {.kind = '-', .text = "b"}, {.kind = '+', .text = "X"}, {.kind = ' ', .text = "c"}});
  EXPECT_THAT(output, "@@ -1,3 +1,3 @@\n a\n-b\n+X\n c\n");
}

// UpdateAbslLogFlags: must be callable without crashing; it only adjusts flags.

TEST_F(DiffInternalTest, UpdateAbslLogFlagsIsSafeToCall) {
  UpdateAbslLogFlags();
  SUCCEED();
}

}  // namespace
}  // namespace mbo::diff::diff_internal
