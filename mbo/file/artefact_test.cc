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

#include "mbo/file/artefact.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

namespace mbo::file {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::EqualsText;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct ArtefactTest : ::testing::Test {
 protected:
  // `::testing::Test` declares these protected; widening them to public would let
  // anything drive the fixture's setup/teardown out of band.
  void SetUp() override {
    tmp_dir = fs::temp_directory_path() / "mbo_artefact_test";
    fs::create_directories(tmp_dir);
  }

  void TearDown() override { fs::remove_all(tmp_dir); }

  std::string Write(std::string_view name, std::string_view content) const {
    const fs::path path = tmp_dir / name;
    std::ofstream out(path);
    out << content;
    out.close();
    return path.string();
  }

 public:
  // Public, and without the trailing underscore: the check exempts a class whose
  // member variables are ALL public, and TEST_F bodies read this directly.
  fs::path tmp_dir;
};

TEST_F(ArtefactTest, DefaultsAreTheDocumentedOnes) {
  const Artefact artefact;
  EXPECT_THAT(artefact.data, IsEmpty());
  EXPECT_THAT(artefact.name, "-") << "an unnamed artefact reads as stdin";
  EXPECT_THAT(artefact.time, absl::FromUnixSeconds(0));
  EXPECT_THAT(artefact.tz, absl::UTCTimeZone());
}

TEST_F(ArtefactTest, OptionsDefaultToKeepingTheTime) {
  const Artefact::Options options = Artefact::Options::Default();
  EXPECT_THAT(options.skip_time, IsFalse());
  EXPECT_THAT(options.tz, absl::UTCTimeZone());
}

TEST_F(ArtefactTest, ReadsAFilesContentAndName) {
  const std::string path = Write("simple.txt", "hello\nworld\n");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::Read(path));
  EXPECT_THAT(artefact.data, EqualsText("hello\nworld\n"));
  EXPECT_THAT(artefact.name, path) << "the name is the path it was read from";
}

TEST_F(ArtefactTest, ReadsAnEmptyFile) {
  const std::string path = Write("empty.txt", "");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::Read(path));
  EXPECT_THAT(artefact.data, IsEmpty());
}

TEST_F(ArtefactTest, ReadingAMissingFileFails) {
  const auto artefact = Artefact::Read((tmp_dir / "does_not_exist.txt").string());
  EXPECT_THAT(artefact.ok(), IsFalse());
}

TEST_F(ArtefactTest, SkipTimeLeavesTheTimeAtItsDefault) {
  const std::string path = Write("timed.txt", "x");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::Read(path, {.skip_time = true}));
  EXPECT_THAT(artefact.time, absl::FromUnixSeconds(0)) << "skip_time means the file's mtime is not read";
}

TEST_F(ArtefactTest, WithoutSkipTimeReadsTheRealMTime) {
  const absl::Time before = absl::Now() - absl::Minutes(1);
  const std::string path = Write("timed2.txt", "x");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::Read(path));
  // A WINDOW around now, not merely "after the epoch". std::filesystem::file_clock has
  // an implementation-defined epoch - libc++ uses the Unix epoch, libstdc++ uses
  // 2174-01-01 - so a conversion that assumes the two clocks share an origin lands
  // roughly 148 years off. "> epoch" only catches that in the negative direction, and
  // so passed on macOS while the value was wrong on Linux.
  EXPECT_THAT(artefact.time > before, IsTrue()) << "mtime is not implausibly old: " << artefact.time;
  EXPECT_THAT(artefact.time < absl::Now() + absl::Minutes(1), IsTrue())
      << "mtime is not in the future: " << artefact.time;
}

TEST_F(ArtefactTest, ReadMaxLinesTruncatesToTheLimit) {
  const std::string path = Write("many.txt", "a\nb\nc\nd\n");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::ReadMaxLines(path, 2));
  EXPECT_THAT(artefact.data, EqualsText("a\nb\n"));
}

TEST_F(ArtefactTest, ReadMaxLinesReturnsEverythingWhenUnderTheLimit) {
  const std::string path = Write("few.txt", "a\nb\n");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::ReadMaxLines(path, 10));
  EXPECT_THAT(artefact.data, EqualsText("a\nb\n"));
}

TEST_F(ArtefactTest, ReadMaxLinesWithZeroReturnsNothing) {
  const std::string path = Write("zero.txt", "a\nb\n");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::ReadMaxLines(path, 0));
  EXPECT_THAT(artefact.data, IsEmpty());
}

TEST_F(ArtefactTest, ReadMaxLinesReportsAMissingFile) {
  const auto artefact = Artefact::ReadMaxLines((tmp_dir / "does_not_exist.txt").string(), 1);
  EXPECT_THAT(artefact.ok(), IsFalse());
}

TEST_F(ArtefactTest, ReadMaxLinesCanSkipTheModificationTime) {
  const std::string path = Write("untimed.txt", "a\nb\n");
  MBO_ASSERT_OK_AND_ASSIGN(const Artefact artefact, Artefact::ReadMaxLines(path, 1, {.skip_time = true}));
  EXPECT_THAT(artefact.data, "a\n");
  EXPECT_THAT(artefact.time, absl::UnixEpoch());
}

}  // namespace
}  // namespace mbo::file
