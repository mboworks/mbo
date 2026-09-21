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

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/diff/diff_options.h"
#include "mbo/diff/impl/diff_direct.h"
#include "mbo/diff/impl/diff_myers.h"
#include "mbo/diff/impl/diff_naive.h"
#include "mbo/file/artefact.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

// Tests the three diff algorithm implementations directly, not through the
// `Diff::FileDiff` dispatcher: each is its own library, and each deserves its own
// contract checks. All three replay their edit scripts through `ChunkedDiff`, so
// where their algorithms must agree (a single-line change has exactly one minimal
// script) their unified output must be byte-identical; where they legitimately
// differ (naive is greedy, direct is positional side-by-side) the tests say so
// rather than over-constraining.

namespace mbo::diff {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

struct DiffImplTest : ::testing::Test {
  // Bare hunks: no file headers, no context - the smallest comparable output.
  static DiffOptions BareOptions() {
    return DiffOptions{
        .context_size = 0,
        .file_header_use = DiffOptions::FileHeaderUse::kNone,
    };
  }

  static file::Artefact Text(std::string_view text) { return {.data = std::string(text)}; }
};

// Identical inputs -------------------------------------------------------------

TEST_F(DiffImplTest, MyersIdenticalInputsProduceNoDiff) {
  EXPECT_THAT(DiffMyers::FileDiff(Text("a\nb\n"), Text("a\nb\n"), BareOptions()), IsOkAndHolds(IsEmpty()));
}

TEST_F(DiffImplTest, NaiveIdenticalInputsProduceNoDiff) {
  EXPECT_THAT(DiffNaive::FileDiff(Text("a\nb\n"), Text("a\nb\n"), BareOptions()), IsOkAndHolds(IsEmpty()));
}

TEST_F(DiffImplTest, DirectIdenticalInputsProduceNoDiff) {
  EXPECT_THAT(DiffDirect::FileDiff(Text("a\nb\n"), Text("a\nb\n"), BareOptions()), IsOkAndHolds(IsEmpty()));
}

// A single-line change has exactly one minimal edit script, so Myers and Naive
// must agree byte for byte. -----------------------------------------------------

TEST_F(DiffImplTest, MyersSingleLineChange) {
  EXPECT_THAT(
      DiffMyers::FileDiff(Text("a\nb\nc\n"), Text("a\nX\nc\n"), BareOptions()),
      IsOkAndHolds(EqualsText("@@ -2 +2 @@\n-b\n+X\n")));
}

TEST_F(DiffImplTest, NaiveAgreesWithMyersOnASingleLineChange) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const std::string myers, DiffMyers::FileDiff(Text("a\nb\nc\n"), Text("a\nX\nc\n"), BareOptions()));
  EXPECT_THAT(
      DiffNaive::FileDiff(Text("a\nb\nc\n"), Text("a\nX\nc\n"), BareOptions()), IsOkAndHolds(EqualsText(myers)));
}

TEST_F(DiffImplTest, MyersPureInsertion) {
  EXPECT_THAT(
      DiffMyers::FileDiff(Text("a\nc\n"), Text("a\nb\nc\n"), BareOptions()),
      IsOkAndHolds(EqualsText("@@ -1,0 +2 @@\n+b\n")));
}

TEST_F(DiffImplTest, MyersPureDeletion) {
  EXPECT_THAT(
      DiffMyers::FileDiff(Text("a\nb\nc\n"), Text("a\nc\n"), BareOptions()),
      IsOkAndHolds(EqualsText("@@ -2 +1,0 @@\n-b\n")));
}

TEST_F(DiffImplTest, MyersEmptyVersusContent) {
  EXPECT_THAT(DiffMyers::FileDiff(Text(""), Text("a\n"), BareOptions()), IsOkAndHolds(HasSubstr("+a")));
}

// Context lines pass through ChunkedDiff identically for both unified algorithms.

TEST_F(DiffImplTest, ContextLinesAreEmitted) {
  DiffOptions options = BareOptions();
  options.context_size = 1;
  EXPECT_THAT(
      DiffMyers::FileDiff(Text("a\nb\nc\n"), Text("a\nX\nc\n"), options),
      IsOkAndHolds(EqualsText("@@ -1,3 +1,3 @@\n a\n-b\n+X\n c\n")));
}

// Naive is documented as greedy and non-minimal: it must still produce a diff
// that names every changed line, even where Myers would emit fewer edits.

TEST_F(DiffImplTest, NaiveStillCoversAllChanges) {
  EXPECT_THAT(
      DiffNaive::FileDiff(Text("a\nb\na\nb\n"), Text("b\na\nb\na\n"), BareOptions()), IsOkAndHolds(Not(IsEmpty())));
}

TEST_F(DiffImplTest, MyersMinimalOptionYieldsAMinimalScriptOnTheSameInput) {
  DiffOptions options = BareOptions();
  options.minimal = true;
  MBO_ASSERT_OK_AND_ASSIGN(
      const std::string capped, DiffMyers::FileDiff(Text("a\nb\na\nb\n"), Text("b\na\nb\na\n"), BareOptions()));
  MBO_ASSERT_OK_AND_ASSIGN(
      const std::string minimal, DiffMyers::FileDiff(Text("a\nb\na\nb\n"), Text("b\na\nb\na\n"), options));
  // For an input this small the cost cap never triggers, so both must be the
  // one minimal script: delete the leading "a", append a trailing "a".
  EXPECT_THAT(minimal, capped);
}

// Direct is a positional side-by-side view, not a unified diff: a changed line
// shows both versions on one row.

TEST_F(DiffImplTest, DirectShowsChangedLinesSideBySide) {
  EXPECT_THAT(
      DiffDirect::FileDiff(Text("a\nb\nc\n"), Text("a\nX\nc\n"), BareOptions()),
      IsOkAndHolds(AllOf(HasSubstr("b"), HasSubstr("X"))));
}

}  // namespace
}  // namespace mbo::diff
