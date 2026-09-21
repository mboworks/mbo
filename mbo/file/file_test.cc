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

#include "mbo/file/file.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef _WIN32
# include <sys/stat.h>
#endif

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

namespace mbo::file {
namespace {

namespace fs = ::std::filesystem;

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::HasSubstr;

struct FileTest : public ::testing::Test {
  static std::string TestDir() {
    const auto* const test = ::testing::UnitTest::GetInstance()->current_test_info();
    return JoinPaths(::testing::TempDir(), test->name());
  }

  FileTest() : tmp_dir(TestDir()) {}

 protected:
  // `::testing::Test` declares these protected; widening them to public would let
  // anything call the fixture's setup/teardown out of band.
  void SetUp() override { fs::create_directory(tmp_dir); }

  void TearDown() override { fs::remove_all(tmp_dir); }

 public:
  const fs::path tmp_dir;
};

TEST_F(FileTest, SetContents) {
  const fs::path tmp_file = JoinPaths(tmp_dir, "foo.txt");
  EXPECT_OK(SetContents(tmp_file.string(), "foo"));
}

TEST_F(FileTest, FileOperationsReportMissingAndInvalidPaths) {
  const fs::path missing = JoinPaths(tmp_dir, "missing.txt");
  EXPECT_THAT(SetContents(tmp_dir, "content"), StatusIs(absl::StatusCode::kUnknown, HasSubstr("Unable to open file")));
  EXPECT_THAT(Readable(missing), StatusIs(absl::StatusCode::kNotFound, HasSubstr("File does not exist")));
  EXPECT_THAT(GetContents(missing), StatusIs(absl::StatusCode::kNotFound, HasSubstr("Unable to read file")));
  EXPECT_THAT(GetMaxLines(missing, 1), StatusIs(absl::StatusCode::kNotFound, HasSubstr("Unable to read file")));
  EXPECT_THAT(GetMTime(missing), StatusIs(absl::StatusCode::kNotFound, HasSubstr("File error")));
}

TEST_F(FileTest, SetGetContentsWithZero) {
  const fs::path tmp_file = JoinPaths(tmp_dir, "foo.txt");
  EXPECT_THAT(SetContents(tmp_file.string(), "foo\0bar"), IsOk());
  EXPECT_THAT(GetContents(tmp_file.string()), IsOkAndHolds("foo\0bar"));
}

TEST_F(FileTest, GetContentsPreservesBinaryBytes) {
  const fs::path tmp_file = JoinPaths(tmp_dir, "binary.txt");
  constexpr std::string_view kContents = "line one\r\nline two\0tail";
  ASSERT_OK(SetContents(tmp_file, kContents));
  EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds(kContents));
}

#ifndef _WIN32
TEST_F(FileTest, GetContentsRejectsNonSeekableFile) {
  const fs::path fifo = JoinPaths(tmp_dir, "contents.fifo");
  ASSERT_THAT(::mkfifo(fifo.c_str(), 0600), 0);
  std::atomic_bool reader_done = false;
  const std::jthread writer([&fifo, &reader_done] {
    const std::ofstream output(fifo, std::ios_base::binary);
    while (!reader_done.load()) {
      std::this_thread::yield();
    }
  });
  EXPECT_THAT(GetContents(fifo), StatusIs(absl::StatusCode::kUnknown, HasSubstr("Unable to determine file size")));
  reader_done = true;
}
#endif

TEST_F(FileTest, Readable) {
  EXPECT_THAT(
      Readable(tmp_dir),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("Cannot open directory for reading")));
  const fs::path tmp_file = JoinPaths(tmp_dir, "foo.txt");
  ASSERT_OK(SetContents(tmp_file, "foo"));
  EXPECT_OK(Readable(tmp_file.string()));
}

TEST_F(FileTest, SetAndGetContents) {
  const fs::path tmp_file = JoinPaths(tmp_dir, "foo.txt");
  EXPECT_OK(SetContents(tmp_file, "foo"));
  EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds("foo"));
}

TEST_F(FileTest, GetMaxLines) {
  const fs::path tmp_file = JoinPaths(tmp_dir, "foo.txt");
  {
    static constexpr std::string_view kContents;
    EXPECT_OK(SetContents(tmp_file, kContents));
    EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds(kContents));
    EXPECT_THAT(GetMaxLines(tmp_file, 0), IsOkAndHolds(""));
    EXPECT_THAT(GetMaxLines(tmp_file, 1), IsOkAndHolds(""));
    EXPECT_THAT(GetMaxLines(tmp_file, 9), IsOkAndHolds(""));
  }
  {
    static constexpr std::string_view kContents = "\n";
    EXPECT_OK(SetContents(tmp_file, kContents));
    EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 0), IsOkAndHolds(""));
    EXPECT_THAT(GetMaxLines(tmp_file, 1), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 9), IsOkAndHolds(EqualsText(kContents)));
  }
  {
    static constexpr std::string_view kContents = "foo\nbar\nbaz";
    EXPECT_OK(SetContents(tmp_file, kContents));
    EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 0), IsOkAndHolds(""));
    EXPECT_THAT(GetMaxLines(tmp_file, 1), IsOkAndHolds("foo\n"));
    EXPECT_THAT(GetMaxLines(tmp_file, 2), IsOkAndHolds(EqualsText("foo\nbar\n")));
    EXPECT_THAT(GetMaxLines(tmp_file, 3), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 9), IsOkAndHolds(EqualsText(kContents)));
  }
  {
    static constexpr std::string_view kContents = "foo\nbar\nbaz\n";
    EXPECT_OK(SetContents(tmp_file, kContents));
    EXPECT_THAT(GetContents(tmp_file), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 0), IsOkAndHolds(""));
    EXPECT_THAT(GetMaxLines(tmp_file, 1), IsOkAndHolds("foo\n"));
    EXPECT_THAT(GetMaxLines(tmp_file, 2), IsOkAndHolds(EqualsText("foo\nbar\n")));
    EXPECT_THAT(GetMaxLines(tmp_file, 3), IsOkAndHolds(EqualsText(kContents)));
    EXPECT_THAT(GetMaxLines(tmp_file, 9), IsOkAndHolds(EqualsText(kContents)));
  }
}

#ifdef __linux__
TEST_F(FileTest, GetMaxLinesReportsReadError) {
  EXPECT_THAT(GetMaxLines("/proc/self/mem", 1), StatusIs(absl::StatusCode::kUnknown, HasSubstr("Unable to read file")));
}
#endif

TEST_F(FileTest, IsAbsolutePath) {
  EXPECT_TRUE(IsAbsolutePath(tmp_dir));
}

TEST_F(FileTest, NormalizePath) {
  EXPECT_THAT(NormalizePath(""), "");
  EXPECT_THAT(NormalizePath("/"), "/");
  EXPECT_THAT(NormalizePath("//"), "/");
  EXPECT_THAT(NormalizePath("///"), "/");
  EXPECT_THAT(NormalizePath("////"), "/");
  EXPECT_THAT(NormalizePath("//a/"), "/a");
  EXPECT_THAT(NormalizePath("//a//b"), "/a/b");
  EXPECT_THAT(NormalizePath("//a//b/"), "/a/b");
  EXPECT_THAT(NormalizePath("//a//b//"), "/a/b");
  EXPECT_THAT(NormalizePath("//a////b"), "/a/b");
  EXPECT_THAT(NormalizePath("a/"), "a");
}

TEST_F(FileTest, NormalizePathAcceptsNativeString) {
  using PathChar = fs::path::value_type;
  using PathString = fs::path::string_type;
  const PathString input = {static_cast<PathChar>('a'), static_cast<PathChar>('/'), static_cast<PathChar>('\\')};
  const PathString expected = {static_cast<PathChar>('a')};
  EXPECT_THAT(NormalizePath(fs::path(input)).native(), expected);
}

TEST_F(FileTest, JoinPaths) {
  EXPECT_THAT(JoinPaths(""), "");
  EXPECT_THAT(JoinPaths("a"), "a");
  EXPECT_THAT(JoinPaths("", ""), "");
  EXPECT_THAT(JoinPaths("a", ""), "a");
  EXPECT_THAT(JoinPaths("", "b"), "b");
  EXPECT_THAT(JoinPaths("a", "b"), "a/b");
  EXPECT_THAT(JoinPaths("", "", ""), "");
  EXPECT_THAT(JoinPaths("a", "", ""), "a");
  EXPECT_THAT(JoinPaths("", "b", ""), "b");
  EXPECT_THAT(JoinPaths("a", "b", ""), "a/b");
  EXPECT_THAT(JoinPaths("", "", "c"), "c");
  EXPECT_THAT(JoinPaths("a", "", "c"), "a/c");
  EXPECT_THAT(JoinPaths("a", "b", ""), "a/b");
  EXPECT_THAT(JoinPaths("a", "b", "c"), "a/b/c");
  EXPECT_THAT(JoinPaths("", "/", ""), "/");
  EXPECT_THAT(JoinPaths("", "/", "/"), "/");
  EXPECT_THAT(JoinPaths("", "/", "/", "a"), "/a");
  EXPECT_THAT(JoinPaths("a/"), "a");
  EXPECT_THAT(JoinPaths("a", "/b", "/c", "d"), "a/b/c/d");
  EXPECT_THAT(JoinPaths("/a", "/b", "/c", "d"), "/a/b/c/d");
#ifdef _WIN32
  EXPECT_THAT(JoinPaths("a", "/b", "x:/c", "d"), "a/b/c/d");
#endif
}

TEST_F(FileTest, JoinPathsRespectAbsolute) {
  EXPECT_THAT(JoinPathsRespectAbsolute(""), "");
  EXPECT_THAT(JoinPathsRespectAbsolute("a"), "a");
  EXPECT_THAT(JoinPathsRespectAbsolute("", ""), "");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", ""), "a");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "b"), "b");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "b"), "a/b");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "", ""), "");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "", ""), "a");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "b", ""), "b");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "b", ""), "a/b");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "", "c"), "c");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "", "c"), "a/c");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "b", ""), "a/b");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "b", "c"), "a/b/c");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "/", ""), "/");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "/", "/"), "/");
  EXPECT_THAT(JoinPathsRespectAbsolute("", "/", "/", "a"), "/a");
  EXPECT_THAT(JoinPathsRespectAbsolute("a/"), "a");
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "/b", "/c", "d"), "/c/d");
  EXPECT_THAT(JoinPathsRespectAbsolute("/a", "/b", "/c", "d"), "/c/d");
#ifdef _WIN32
  EXPECT_THAT(JoinPathsRespectAbsolute("a", "/b", "x:/c", "d"), "x:/c/d");
#endif
}

}  // namespace
}  // namespace mbo::file
