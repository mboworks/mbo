#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# shellcheck disable=SC2317 # Functions are called by bashtest

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

declare -a EXPECTED=(
  "dir"
  "sub"
  "sub/dir"
  "sub/dir:file1"
  "sub/dir:file2"
  "sub/two"
  "sub/two/dir"
  "sub/two/dir:file1"
  "sub/two/dir:file2"
  "sub/two/dir:file3"
)

FIXTURE_DIR="$(test_tmpdir glob)"
declare -r FIXTURE_DIR

function setup() {
  for entry in "${EXPECTED[@]}"; do
    IFS=":" read -r -a dir_file <<<"${entry}"
    if [[ ${#dir_file[@]} -ge 1 ]]; then
      mkdir -p "${FIXTURE_DIR}/${dir_file[0]}"
    fi
    if [[ ${#dir_file[@]} -ge 2 ]]; then
      touch "${FIXTURE_DIR}/${dir_file[0]}/${dir_file[1]}"
    fi
    if [[ ${#dir_file[@]} == 1 ]] && [[ ${#dir_file[@]} == 2 ]]; then
      die "Bad expected entry."
    fi
  done
}

setup

GLOB="${TEST_SRCDIR}/${TEST_WORKSPACE}/mbo/file/glob"
declare -r GLOB
TESTDATA="${TEST_SRCDIR}/${TEST_WORKSPACE}/mbo/file/testdata/glob_sh_test"
declare -r TESTDATA

[[ -x ${GLOB} ]] || die "Program glob not found."

function _test_glob_and_diff() {
  NAME="${1}"
  shift
  "${GLOB}" "${@}" >"${TEST_TMPDIR}/${NAME}.out" 2>&1
  expect_files_eq "${TESTDATA}/${NAME}.txt" "${TEST_TMPDIR}/${NAME}.out"
}

function test::default() {
  _test_glob_and_diff default "${FIXTURE_DIR}"
}

function test::flag_type() {
  _test_glob_and_diff flag_type "${FIXTURE_DIR}" -type
}

function test::simple() {
  _test_glob_and_diff simple "${FIXTURE_DIR}/*"
}

function test::select() {
  _test_glob_and_diff select "${FIXTURE_DIR}/**/file[23]"
}

function test::summary_and_extensions() {
  local output
  output="$("${GLOB}" "${FIXTURE_DIR}" --noentries --sum_extensions)"
  [[ ${output} == *"FileExt():"* ]] || die "Missing extension-less file summary: ${output}"
  [[ ${output} == *"Files:"* ]] || die "Missing file summary: ${output}"
  [[ ${output} == *"Total:"* ]] || die "Missing total summary: ${output}"
}

function test::fast_size_type_and_depth() {
  local output
  output="$("${GLOB}" "${FIXTURE_DIR}" "sub/dir/file1" --fast --size --type --depth --sum_every=1)"
  [[ ${output} == *"0 f 2 sub/dir/file1"* ]] || die "Missing decorated file entry: ${output}"
  [[ ${output} == *"Files:"* ]] || die "Missing periodic summary: ${output}"
}

function test::re2() {
  local output
  output="$("${GLOB}" "${FIXTURE_DIR}" '.*file[23]' --re2)"
  [[ ${output} == *"sub/dir/file2"* ]] || die "RE2 did not find file2: ${output}"
  [[ ${output} == *"sub/two/dir/file3"* ]] || die "RE2 did not find file3: ${output}"
  [[ ${output} != *"file1"* ]] || die "RE2 unexpectedly found file1: ${output}"
}

function test::hidden_entries_links_and_fifo() {
  local special_dir
  special_dir="$(mktemp -d)"
  mkdir -p "${special_dir}/.hidden_dir"
  touch "${special_dir}/.hidden_dir/visible.txt"
  touch "${special_dir}/.hidden_file"
  touch "${special_dir}/visible.txt"
  ln -s "visible.txt" "${special_dir}/visible.link"
  mkfifo "${special_dir}/visible.fifo"

  local default_output
  default_output="$("${GLOB}" "${special_dir}" --nodotdir --nodotfile --type)"
  [[ $'\n'${default_output}$'\n' != *$'\nd .hidden_dir\n'* ]] || die "Hidden directory was not filtered: ${default_output}"
  [[ $'\n'${default_output}$'\n' != *$'\nf .hidden_file\n'* ]] || die "Hidden file was not filtered: ${default_output}"
  [[ ${default_output} == *"l visible.link"* ]] || die "Symlink type was not reported: ${default_output}"
  [[ ${default_output} == *"p visible.fifo"* ]] || die "FIFO type was not reported: ${default_output}"

  local complete_output
  complete_output="$("${GLOB}" "${special_dir}" --dotdir --dotfile --sum_extensions --type)"
  [[ ${complete_output} == *"d .hidden_dir"* ]] || die "Hidden directory was not included: ${complete_output}"
  [[ ${complete_output} == *"f .hidden_file"* ]] || die "Hidden file was not included: ${complete_output}"
  [[ ${complete_output} == *"FileExt(.txt):"* ]] || die "Extension summary was not reported: ${complete_output}"
  [[ ${complete_output} == *"FileExt(.hidden_file):"* ]] || die "Dot-file summary was not reported: ${complete_output}"
}

function test::rejects_bad_argument_counts() {
  if "${GLOB}" >/dev/null 2>&1; then
    die "glob without a pattern unexpectedly succeeded"
  fi
  if "${GLOB}" one two three >/dev/null 2>&1; then
    die "glob with three positional arguments unexpectedly succeeded"
  fi
}

test_runner
