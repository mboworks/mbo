#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# shellcheck disable=SC2317 # Functions are invoked by bashtest.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

function test::representative_workloads() {
  local benchmark="${TEST_SRCDIR}/${TEST_WORKSPACE}/mbo/strings/string_interner_benchmark"
  local filter='^(Build/Default/256/16|Unique/Default/16/1024/16|Hit/SetIndex/256/16|Get/List/256/16|Parent/RootHit/(0|1|2|4)/16/32/32|Error/.*|Counts/.*|Map/.*)$'
  local output status=0
  output="$("${benchmark}" "--benchmark_filter=${filter}" --benchmark_min_time=1x 2>&1)" || status=$?
  expect_eq 0 "${status}"
  expect_output_contains 'Build/Default/256/16' "${output}"
  expect_output_contains 'Unique/Default/16/1024/16' "${output}"
  expect_output_contains 'Parent/RootHit/4/16/32/32' "${output}"
  expect_output_contains 'Parent/RootHit/2/16/32/32' "${output}"
  expect_output_contains 'Map/IterateNonCopyable/4096' "${output}"
  expect_output_contains 'Counts/Unique/' "${output}"
  expect_output_contains 'Error/CharacterExpected' "${output}"
  expect_output_not_contains 'ERROR OCCURRED' "${output}"
}
