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

# Tests the `digest` binary's CLI surface: algorithm selection, the
# checksum-style output format, --reverse, stdin, multi-file processing,
# --check verification (roundtrip, coreutils interop format, corruption,
# quiet/status/strict/ignore_missing) and the error paths (directory,
# missing file, unknown algorithm).

# shellcheck disable=SC2317 # Functions are called by bashtest

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

DIGEST="${TEST_SRCDIR}/${TEST_WORKSPACE}/mbo/digest/digest"
declare -r DIGEST
TESTDATA="${TEST_SRCDIR}/${TEST_WORKSPACE}/mbo/digest/testdata/digest_cli_sh_test"
declare -r TESTDATA

[[ -x ${DIGEST} ]] || die "Program digest not found."

ABC="${BASHTEST_TMPDIR}/abc.txt"
declare -r ABC
printf 'abc' >"${ABC}"

# Known answers for the file containing exactly 'abc' (the values pinned in
# digest_test.cc / generated with independent references). No associative
# arrays: macOS ships bash 3.2.
SHA256_ABC="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
declare -r SHA256_ABC

function known_hash() {
  case "$1" in
    md5) echo "900150983cd24fb0d6963f7d28e17f72" ;;
    sha1) echo "a9993e364706816aba3e25717850c26c9cd0d89d" ;;
    sha256) echo "${SHA256_ABC}" ;;
    sha512-256) echo "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23" ;;
    sha3-256) echo "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532" ;;
    blake3) echo "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85" ;;
    *) die "No known hash for $1." ;;
  esac
}

function test::known_answers_per_algorithm() {
  local algorithm
  for algorithm in md5 sha1 sha256 sha512-256 sha3-256 blake3; do
    local out
    out="$("${DIGEST}" --algorithm="${algorithm}" "${ABC}")" || die "digest failed for ${algorithm}."
    [[ ${out} == "$(known_hash "${algorithm}")  ${ABC}" ]] \
      || die "Wrong ${algorithm} output: '${out}'"
  done
}

function test::short_algorithm_flag_alias() {
  local out
  out="$("${DIGEST}" -a sha3-256 "${ABC}")" || die "digest failed for -a."
  [[ ${out} == "$(known_hash sha3-256)  ${ABC}" ]] || die "Wrong -a output: '${out}'"
  out="$("${DIGEST}" -a md5 --algorithm=sha256 "${ABC}")" || die "digest failed for -a + --algorithm."
  [[ ${out} == "$(known_hash md5)  ${ABC}" ]] || die "-a must take precedence: '${out}'"
}

function test::default_algorithm_is_sha256() {
  local out
  out="$("${DIGEST}" "${ABC}")" || die "digest failed."
  [[ ${out} == "${SHA256_ABC}  ${ABC}" ]] || die "Default is not sha256: '${out}'"
}

# The output matrix: the same input files under every algorithm, each
# compared against an expected output generated with independent references
# (python hashlib / the official blake3 module) - never with this binary.
function test::algorithm_output_matrix() {
  local expected
  for expected in "${TESTDATA}"/*.out; do
    local algorithm="${expected##*/}"
    algorithm="${algorithm%.out}"
    [[ -r ${expected} ]] || die "Missing expected output testdata for ${algorithm}."
    (
      cd "${TESTDATA}" \
        && "${DIGEST}" --algorithm="${algorithm}" input1.txt input2.txt empty.txt \
          >"${TEST_TMPDIR}/${algorithm}.out"
    ) || die "digest failed for ${algorithm}."
    diff -u "${expected}" "${TEST_TMPDIR}/${algorithm}.out" || die "Output mismatch for ${algorithm}."
  done
}

function test::reverse_swaps_columns() {
  local out
  out="$("${DIGEST}" --reverse "${ABC}")" || die "digest failed."
  [[ ${out} == "${ABC}  ${SHA256_ABC}" ]] || die "Wrong --reverse output: '${out}'"
}

function test::stdin_via_dash() {
  local out
  out="$(printf 'abc' | "${DIGEST}" --algorithm=sha3-256 -)" || die "digest failed on stdin."
  [[ ${out} == "$(known_hash sha3-256)  -" ]] || die "Wrong stdin output: '${out}'"
}

function test::multiple_files_one_line_each() {
  local out
  out="$("${DIGEST}" "${ABC}" "${ABC}" "${ABC}")" || die "digest failed."
  [[ $(wc -l <<<"${out}") -eq 3 ]] || die "Expected three lines, got: '${out}'"
}

function test::directory_is_an_error_but_processing_continues() {
  local out
  if out="$("${DIGEST}" "${BASHTEST_TMPDIR}" "${ABC}" 2>"${TEST_TMPDIR}/dir.err")"; then
    die "Expected exit code 1 for a directory argument."
  fi
  grep -q "Is a directory" "${TEST_TMPDIR}/dir.err" || die "Expected a directory error message."
  [[ ${out} == "${SHA256_ABC}  ${ABC}" ]] || die "Expected the regular file still to be hashed."
}

function test::ignore_directories_skips_silently() {
  local out
  out="$("${DIGEST}" --ignore_directories "${BASHTEST_TMPDIR}" "${ABC}" 2>"${TEST_TMPDIR}/skip.err")" \
    || die "Expected exit code 0 with --ignore_directories."
  [[ -s "${TEST_TMPDIR}/skip.err" ]] && die "Expected no error output with --ignore_directories."
  [[ ${out} == "${SHA256_ABC}  ${ABC}" ]] || die "Expected only the regular file to be hashed."
  out="$("${DIGEST}" -d "${BASHTEST_TMPDIR}" "${ABC}" 2>"${TEST_TMPDIR}/skip_short.err")" \
    || die "Expected exit code 0 with -d."
  [[ -s "${TEST_TMPDIR}/skip_short.err" ]] && die "Expected no error output with -d."
  [[ ${out} == "${SHA256_ABC}  ${ABC}" ]] || die "Expected only the regular file to be hashed with -d."
}

function test::check_roundtrip_ok() {
  local dir="${TEST_TMPDIR}/roundtrip"
  mkdir -p "${dir}"
  cd "${dir}" || die "cd failed."
  printf 'abc' >f1.txt
  printf 'hello' >f2.txt
  "${DIGEST}" -a blake3 f1.txt f2.txt >sums.txt || die "digest failed."
  local out
  out="$("${DIGEST}" -a blake3 --check sums.txt)" || die "Expected exit 0 for a clean roundtrip."
  [[ ${out} == $'f1.txt: OK\nf2.txt: OK' ]] || die "Wrong check output: '${out}'"
  out="$("${DIGEST}" -a blake3 -c - <sums.txt)" || die "Expected exit 0 for stdin check."
  [[ ${out} == $'f1.txt: OK\nf2.txt: OK' ]] || die "Wrong stdin check output: '${out}'"
}

function test::check_accepts_coreutils_format() {
  local dir="${TEST_TMPDIR}/coreutils"
  mkdir -p "${dir}"
  cd "${dir}" || die "cd failed."
  printf 'abc' >abc.txt
  local upper
  upper="$(tr '[:lower:]' '[:upper:]' <<<"${SHA256_ABC}")"
  printf '%s *abc.txt\n' "${upper}" >sums.txt
  local out
  out="$("${DIGEST}" --check sums.txt)" || die "Expected exit 0 for '*' marker + uppercase hex."
  [[ ${out} == "abc.txt: OK" ]] || die "Wrong output: '${out}'"
}

function test::check_detects_corruption() {
  local dir="${TEST_TMPDIR}/corrupt"
  mkdir -p "${dir}"
  cd "${dir}" || die "cd failed."
  printf 'abc' >f.txt
  "${DIGEST}" f.txt >sums.txt || die "digest failed."
  printf 'CORRUPT' >f.txt
  local out
  if out="$("${DIGEST}" --check sums.txt 2>"${TEST_TMPDIR}/warn.err")"; then
    die "Expected exit 1 for a corrupted file."
  fi
  [[ ${out} == "f.txt: FAILED" ]] || die "Wrong output: '${out}'"
  grep -q "did NOT match" "${TEST_TMPDIR}/warn.err" || die "Expected a NOT-match warning."
  out="$("${DIGEST}" --check --quiet sums.txt 2>/dev/null)" && die "Expected exit 1 with --quiet."
  [[ ${out} == "f.txt: FAILED" ]] || die "--quiet must still print FAILED: '${out}'"
  out="$("${DIGEST}" --check --status sums.txt 2>&1)" && die "Expected exit 1 with --status."
  [[ -z ${out} ]] || die "--status must print nothing: '${out}'"
}

function test::check_missing_and_ignore_missing() {
  local dir="${TEST_TMPDIR}/missing"
  mkdir -p "${dir}"
  cd "${dir}" || die "cd failed."
  printf 'abc' >present.txt
  "${DIGEST}" present.txt >sums.txt || die "digest failed."
  printf '%s  gone.txt\n' "${SHA256_ABC}" >>sums.txt
  local out
  if out="$("${DIGEST}" --check --quiet sums.txt 2>/dev/null)"; then
    die "Expected exit 1 for an unreadable listed file."
  fi
  [[ ${out} == "gone.txt: FAILED open or read" ]] || die "Wrong output: '${out}'"
  "${DIGEST}" --check --quiet --ignore_missing sums.txt >/dev/null || die "Expected exit 0 with --ignore_missing."
  printf '%s  gone.txt\n' "${SHA256_ABC}" >only_missing.txt
  if "${DIGEST}" --check --quiet --ignore_missing only_missing.txt >/dev/null 2>"${TEST_TMPDIR}/none.err"; then
    die "Expected exit 1 when no file was verified."
  fi
  grep -q "no file was verified" "${TEST_TMPDIR}/none.err" || die "Expected a no-file-verified message."
}

function test::check_strict_rejects_malformed() {
  local dir="${TEST_TMPDIR}/strict"
  mkdir -p "${dir}"
  cd "${dir}" || die "cd failed."
  printf 'abc' >f.txt
  "${DIGEST}" f.txt >sums.txt || die "digest failed."
  printf 'this is not a checksum line\n' >>sums.txt
  "${DIGEST}" --check --quiet sums.txt 2>"${TEST_TMPDIR}/fmt.err" || die "Malformed lines alone must not fail without --strict."
  grep -q "improperly formatted" "${TEST_TMPDIR}/fmt.err" || die "Expected a format warning."
  if "${DIGEST}" --check --quiet --strict sums.txt >/dev/null 2>&1; then
    die "Expected exit 1 with --strict."
  fi
}

function test::missing_file_is_an_error() {
  if "${DIGEST}" "${BASHTEST_TMPDIR}/does-not-exist" 2>"${TEST_TMPDIR}/missing.err"; then
    die "Expected exit code 1 for a missing file."
  fi
  grep -q "Cannot read file" "${TEST_TMPDIR}/missing.err" || die "Expected a read error message."
}

function test::unknown_algorithm_is_an_error() {
  if "${DIGEST}" --algorithm=nope "${ABC}" 2>"${TEST_TMPDIR}/algo.err"; then
    die "Expected exit code 1 for an unknown algorithm."
  fi
  grep -q "Unknown --algorithm 'nope'" "${TEST_TMPDIR}/algo.err" || die "Expected an unknown-algorithm message."
}

function test::no_files_is_an_error() {
  if "${DIGEST}" 2>"${TEST_TMPDIR}/nofiles.err"; then
    die "Expected exit code 1 without file arguments."
  fi
  grep -q "At least one file" "${TEST_TMPDIR}/nofiles.err" || die "Expected a usage error message."
}

test_runner
