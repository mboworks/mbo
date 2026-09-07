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

#include "mbo/status/status_builder.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace mbo::status {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusHasPayload;
using ::mbo::testing::StatusIs;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct StatusBuilderTest : ::testing::Test {};

TEST_F(StatusBuilderTest, Status) {
  EXPECT_THAT(StatusBuilder(), IsOk());
  EXPECT_THAT(StatusBuilder().ok(), IsTrue());
  EXPECT_THAT(StatusBuilder(absl::OkStatus()), IsOk());
  EXPECT_THAT(StatusBuilder(absl::OkStatus()).ok(), IsTrue());
  EXPECT_THAT(StatusBuilder(absl::CancelledError()), StatusIs(absl::StatusCode::kCancelled));
  EXPECT_THAT(StatusBuilder(absl::CancelledError()).ok(), IsFalse());
}

TEST_F(StatusBuilderTest, StatusOr) {
  using StatusOr = absl::StatusOr<int>;
  EXPECT_THAT(StatusBuilder(StatusOr(1)), IsOk());
  EXPECT_THAT(StatusBuilder(StatusOr(1)).ok(), IsTrue());
  EXPECT_THAT(StatusBuilder(StatusOr(absl::CancelledError())), StatusIs(absl::StatusCode::kCancelled));
  EXPECT_THAT(StatusBuilder(StatusOr(absl::CancelledError())).ok(), IsFalse());
}

TEST_F(StatusBuilderTest, Message) {
  const auto error = StatusBuilder(absl::CancelledError("<Error>")) << "<Message>";
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kCancelled, "<Error><Message>"));
}

TEST_F(StatusBuilderTest, SetAppend) {
  const auto error = StatusBuilder(absl::CancelledError("<Error>")).SetAppend().SetAppend() << "<Message>";
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kCancelled, "<Error><Message>"));
}

TEST_F(StatusBuilderTest, AppendTagSupportsRvalueBuilder) {
  const auto error = StatusBuilder(absl::CancelledError("<Error>")) << StatusBuilder::Append << "<Message>";
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kCancelled, "<Error><Message>"));
}

TEST_F(StatusBuilderTest, OperationsOnOkStatusRemainNoOps) {
  StatusBuilder builder(absl::OkStatus());
  builder.SetAppend().SetPayload("url", "content");
  builder << "ignored";
  EXPECT_THAT(absl::Status(builder), IsOk());
}

TEST_F(StatusBuilderTest, SetPrepend) {
  const auto error = StatusBuilder(absl::CancelledError("<Error>")).SetPrepend()
                     << "<Prefix>" << StatusBuilder::Append << "<Suffix>";
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kCancelled, "<Prefix><Error><Suffix>"));
}

TEST_F(StatusBuilderTest, SetPayload) {
  const auto error = StatusBuilder(absl::CancelledError()).SetPayload("url", "content");
  const absl::Status status(error);
  EXPECT_THAT(status, StatusHasPayload("url", "content"));
}

}  // namespace
}  // namespace mbo::status
