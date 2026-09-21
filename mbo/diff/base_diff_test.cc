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

#include "mbo/diff/base_diff.h"

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/diff/chunked_diff.h"
#include "mbo/diff/diff_options.h"
#include "mbo/file/artefact.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

// Tests the shared diff plumbing the algorithms are built on: BaseDiff (headers,
// preprocessed data, option-aware comparison) and ChunkedDiff (the push/finalize
// protocol every algorithm replays its edit script through).

namespace mbo::diff {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

struct BaseDiffTest : ::testing::Test {
  static DiffOptions BareOptions() {
    return DiffOptions{
        .context_size = 0,
        .file_header_use = DiffOptions::FileHeaderUse::kNone,
    };
  }

  static file::Artefact Text(std::string_view text, std::string_view name = "") {
    return {.data = std::string(text), .name = std::string(name)};
  }
};

// BaseDiff: file headers. --------------------------------------------------------

TEST_F(BaseDiffTest, NoHeadersWhenDisabled) {
  EXPECT_THAT(BaseDiff::FileHeaders(Text("x", "lhs"), Text("y", "rhs"), BareOptions()), IsEmpty());
}

TEST_F(BaseDiffTest, BothHeadersNameBothFiles) {
  DiffOptions options = BareOptions();
  options.file_header_use = DiffOptions::FileHeaderUse::kBoth;
  const std::string headers = BaseDiff::FileHeaders(Text("x", "left.txt"), Text("y", "right.txt"), options);
  EXPECT_THAT(headers, HasSubstr("--- left.txt"));
  EXPECT_THAT(headers, HasSubstr("+++ right.txt"));
}

TEST_F(BaseDiffTest, LeftHeadersUseTheLeftNameOnBothLines) {
  DiffOptions options = BareOptions();
  options.file_header_use = DiffOptions::FileHeaderUse::kLeft;
  const std::string headers = BaseDiff::FileHeaders(Text("x", "left.txt"), Text("y", "right.txt"), options);
  EXPECT_THAT(headers, HasSubstr("--- left.txt"));
  EXPECT_THAT(headers, HasSubstr("+++ left.txt"));
  EXPECT_THAT(headers, Not(HasSubstr("right.txt")));
}

TEST_F(BaseDiffTest, RightHeadersUseTheRightNameOnBothLines) {
  DiffOptions options = BareOptions();
  options.file_header_use = DiffOptions::FileHeaderUse::kRight;
  const std::string headers = BaseDiff::FileHeaders(Text("x", "left.txt"), Text("y", "right.txt"), options);
  EXPECT_THAT(headers, HasSubstr("--- right.txt"));
  EXPECT_THAT(headers, HasSubstr("+++ right.txt"));
  EXPECT_THAT(headers, Not(HasSubstr("left.txt")));
}

// BaseDiff: option-aware comparison over the preprocessed caches. -----------------

TEST_F(BaseDiffTest, CompareEqIsExactByDefault) {
  const DiffOptions options = BareOptions();
  // The artefacts must outlive the diff: BaseDiff's line caches VIEW their data.
  const file::Artefact lhs = Text("AbC\n");
  const file::Artefact rhs = Text("abc\n");
  const BaseDiff diff(lhs, rhs, options);
  EXPECT_THAT(diff.CompareEq(0, 0), IsFalse());
}

TEST_F(BaseDiffTest, CompareEqHonoursIgnoreCase) {
  DiffOptions options = BareOptions();
  options.ignore_case = true;
  const file::Artefact lhs = Text("AbC\n");
  const file::Artefact rhs = Text("abc\n");
  const BaseDiff diff(lhs, rhs, options);
  EXPECT_THAT(diff.CompareEq(0, 0), IsTrue());
}

TEST_F(BaseDiffTest, HeaderIsTheRenderedFileHeaders) {
  DiffOptions options = BareOptions();
  options.file_header_use = DiffOptions::FileHeaderUse::kBoth;
  const file::Artefact lhs = Text("x\n", "l");
  const file::Artefact rhs = Text("y\n", "r");
  const BaseDiff diff(lhs, rhs, options);
  EXPECT_THAT(diff.Header(), BaseDiff::FileHeaders(Text("x\n", "l"), Text("y\n", "r"), options));
}

// ChunkedDiff: the push/finalize protocol. ----------------------------------------

TEST_F(BaseDiffTest, ChunkedDiffReplaysAnEditScript) {
  const DiffOptions options = BareOptions();
  const file::Artefact lhs = Text("a\nb\nc\n");
  const file::Artefact rhs = Text("a\nX\nc\n");
  ChunkedDiff diff(lhs, rhs, options);
  EXPECT_THAT(diff.More(), IsTrue());
  diff.PushEqual();  // a == a
  diff.PushDiff();   // b -> X
  diff.PushEqual();  // c == c
  EXPECT_THAT(diff.More(), IsFalse());
  EXPECT_THAT(diff.Finalize(), IsOkAndHolds(EqualsText("@@ -2 +2 @@\n-b\n+X\n")));
}

TEST_F(BaseDiffTest, ChunkedDiffAllEqualFinalizesEmpty) {
  const DiffOptions options = BareOptions();
  const file::Artefact lhs = Text("a\nb\n");
  const file::Artefact rhs = Text("a\nb\n");
  ChunkedDiff diff(lhs, rhs, options);
  diff.PushEqual();
  diff.PushEqual();
  EXPECT_THAT(diff.Finalize(), IsOkAndHolds(IsEmpty()));
}

// Exercises the protected Chunk() accessor the way the algorithms do: through a
// derived type. PushRhs alone leaves the pending one-sided lines unflushed;
// MoveDiffs is what commits them, and it is protected on purpose.
struct OneSidedDiff : ChunkedDiff {
  using ChunkedDiff::Chunk;
  using ChunkedDiff::ChunkedDiff;
};

TEST_F(BaseDiffTest, ChunkedDiffOneSidedPushes) {
  // Pure insertion: rhs has an extra line.
  const DiffOptions options = BareOptions();
  const file::Artefact lhs = Text("a\nc\n");
  const file::Artefact rhs = Text("a\nb\nc\n");
  OneSidedDiff diff(lhs, rhs, options);
  diff.PushEqual();  // a
  diff.PushRhs();    // +b
  diff.Chunk().MoveDiffs();
  diff.PushEqual();  // c
  EXPECT_THAT(diff.Finalize(), IsOkAndHolds(EqualsText("@@ -1,0 +2 @@\n+b\n")));
}

}  // namespace
}  // namespace mbo::diff
