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

# Run clang-tidy as an enforcing, report-only pass over the compilation scope
# affected by the given files. Source-only changes stay focused. A project
# header, generated-header template, or .bzl change promotes the scope to every
# first-party translation unit in compile_commands.json because it can affect
# sources that did not themselves change.
#
# clang-tidy resolution prefers the hermetic toolchains_llvm binary (so it matches
# the compile DB's clang flags and understands the codebase), then a versioned system
# clang-tidy on PATH. A resolved clang-tidy older than the minimum below is treated
# as "not installed" and skipped - clang-tidy 16/17 mis-parse this codebase
# and emit false positives whose fixes break the build. That is not hypothetical:
# trunk pinned clang-tidy 16 and its `--export-fixes` runs rewrote the working tree
# with build-breaking "fixes", which is why clang-tidy moved here out of trunk.
# Note that a plain `clang-tidy` on PATH may well BE trunk's 16 (.trunk/tools), so
# the version gate below is what keeps it from being picked up. Mirrors the binary
# resolution ladder in tools/clang_format.sh.

set -euo pipefail

# The minimum clang-tidy major version. Below this, parsing the codebase is unreliable.
readonly MIN_MAJOR=18

# clang-tidy is an automatic commit gate, so a missing prerequisite is a setup
# ERROR rather than a reason to wave the commit through. Silently skipping would
# mean a developer without a compile DB never runs the gate at all and only finds
# out in CI - which is exactly the failure mode the gate exists to prevent.
function die() {
  echo "ERROR: clang-tidy: ${*}" 1>&2
  echo "       (to commit without this hook: SKIP=\"clang-tidy\" git commit ...)" 1>&2
  exit 1
}

# A compile DB is mandatory; without it clang-tidy cannot resolve include paths.
[ -f "compile_commands.json" ] \
  || die "no compile_commands.json; generate it with './compile_commands-update.sh'"

# The `bazel-<repo>` convenience link points at the execroot; fall back to cwd.
BAZEL_OUTPUT="bazel-$(basename "${PWD}")"
[ -d "${BAZEL_OUTPUT}" ] || BAZEL_OUTPUT="."

# The output base holds every fetched repo, including the `--config=clang`
# dev-dependency LLVM toolchain that the execroot symlink forest may omit.
OUTPUT_BASE="$(bazel info output_base 2>/dev/null || true)"

declare -a CLANG_TIDY_LOCS=(
  # 1) Hermetic toolchain via the `bazel-<repo>` execroot link.
  # 1.1) Bazel workspaces
  "${BAZEL_OUTPUT}/external/llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/llvm_toolchain/bin/clang-tidy"
  # 1.2) Bazel modules < 8
  "${BAZEL_OUTPUT}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/bin/clang-tidy"
  # 1.3) Bazel modules >= 8
  "${BAZEL_OUTPUT}/external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang-tidy"

  # 2) Same hermetic toolchain via the output base (covers the dev-dependency
  #    case where it is absent from the execroot symlink forest). Still hermetic,
  #    so this is tried BEFORE any system clang-tidy below.
  "${OUTPUT_BASE:+${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-tidy}"
  "${OUTPUT_BASE:+${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang-tidy}"

  # 3) System clang-tidy by versioned name (its version may differ from the
  #    hermetic toolchain, so it is a last resort). Nothing below MIN_MAJOR is
  #    listed - such a binary would be rejected by the version gate anyway.
  "$(which "clang-tidy-23" 2>/dev/null || true)"
  "$(which "clang-tidy-22" 2>/dev/null || true)"
  "$(which "clang-tidy-21" 2>/dev/null || true)"
  "$(which "clang-tidy-20" 2>/dev/null || true)"
  "$(which "clang-tidy-19" 2>/dev/null || true)"
  "$(which "clang-tidy-18" 2>/dev/null || true)"

  # 4) LLVM_PATH or a plain clang-tidy on PATH.
  "${LLVM_PATH:-/usr}/bin/clang-tidy"
  "$(which clang-tidy 2>/dev/null || true)"
)

CLANG_TIDY=""
for LOC in "${CLANG_TIDY_LOCS[@]}"; do
  if [ -n "${LOC}" ] && [ -x "${LOC}" ]; then
    # Gate on the major version: "LLVM version 22.1.8" -> 22. Skip anything older
    # than MIN_MAJOR (an unparseable version is treated as too old / unusable).
    major="$("${LOC}" --version 2>/dev/null | grep -oE 'version [0-9]+' | grep -oE '[0-9]+' | head -1 || true)"
    if [ -n "${major}" ] && [ "${major}" -ge "${MIN_MAJOR}" ]; then
      CLANG_TIDY="${LOC}"
      break
    fi
  fi
done

[ -n "${CLANG_TIDY}" ] \
  || die "no clang-tidy >= ${MIN_MAJOR} found; build once with --config=clang to fetch the hermetic toolchain, or install a recent clang-tidy"

# Nothing to check (pre-commit may invoke with no matching files).
[ "${#}" -gt 0 ] || exit 0

# Checks that carry no meaning in test code, disabled for `*_test.cc` only. Doing
# it here rather than with a NOLINT per test keeps one statement of the rule
# instead of a comment repeated on every test, and new tests are covered without
# anyone remembering to annotate them.
#   * readability-function-cognitive-complexity: a gtest TestBody's score comes
#     from ASSERT_*/EXPECT_* macros expanding to branches, not from logic that
#     could be refactored. Test bodies reached 122 against a threshold of 30.
#   * clang-analyzer-cplusplus.NewDeleteLeaks: a false positive raised inside
#     gmock-matchers.h, on the hand-rolled union buffer gmock uses for matcher
#     storage. It is third-party code we cannot annotate, and neither
#     --header-filter nor ExcludeHeaderFilterRegex suppresses it - the
#     clang-analyzer checks report regardless of header filtering. It reaches us
#     only through gmock, so it can go with the test partition rather than being
#     disabled for first-party code, where the check is worth having.
# `--checks` APPENDS to the `Checks` in .clang-tidy (it does not replace it), so
# every other check still applies to tests.
readonly TEST_DISABLED_CHECKS='-readability-function-cognitive-complexity,-clang-analyzer-cplusplus.NewDeleteLeaks'

PARALLELISM="${CLANG_TIDY_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
readonly PARALLELISM

declare -a SOURCES=()
declare -a TESTS=()
while IFS= read -r FILE; do
  case "${FILE}" in
    # Not built by bazel at all: an SMHasher3 plugin, copied into that project by
    # mbo/hash/measurements/build_smhasher3.sh and compiled by ITS cmake. It has
    # no compile command here, so clang-tidy would lint it with flags guessed from
    # unrelated files and report its SMHasher3 includes as missing.
    mbo/hash/measurements/smhasher3/*) continue ;;
    *_test.cc | *_test.cpp | *_test.cxx) TESTS+=("${FILE}") ;;
    *) SOURCES+=("${FILE}") ;;
  esac
done < <(python3 tools/clang_tidy_scope.py compile_commands.json "${@}")

# A source file with no entry in the compile DB is NOT linted with the right
# flags - clang-tidy falls back to guessed defaults, fails to find even <gtest>,
# and then reports a cascade of nonsense (invalid case style for `TEST_F`, and
# so on) from the wreckage of a failed parse. That looks like real findings and
# is entirely an artefact of a stale DB, so fail loudly with the one command
# that fixes it. Only sources are checked: every .cc bazel builds has a compile
# command, whereas headers legitimately may not.
declare -a MISSING=()
for FILE in ${SOURCES[@]+"${SOURCES[@]}"} ${TESTS[@]+"${TESTS[@]}"}; do
  case "${FILE}" in
    *.cc | *.cpp | *.cxx) ;;
    *) continue ;;
  esac
  python3 -c '
import json, os, sys
want = os.path.realpath(sys.argv[1])
db = json.load(open("compile_commands.json"))
for e in db:
    f = e["file"]
    p = f if os.path.isabs(f) else os.path.join(e.get("directory", "."), f)
    if os.path.realpath(p) == want:
        sys.exit(0)
sys.exit(1)
' "${FILE}" || MISSING+=("${FILE}")
done
if [ "${#MISSING[@]}" -gt 0 ]; then
  echo "ERROR: clang-tidy: not in compile_commands.json: ${MISSING[*]}" 1>&2
  echo "       The compile DB is stale. Regenerate it with './compile_commands-update.sh'." 1>&2
  exit 1
fi

# Report only: --header-filter restricts diagnostics to this repo's own headers
# (not the toolchain's force-included / system headers), -p points at the compile
# DB. WarningsAsErrors in .clang-tidy makes any finding a non-zero exit. The
# Python coordinator retains parallel execution but owns output serialization,
# progress reporting, and interruption cleanup; worker diagnostics therefore
# cannot interleave and Ctrl-C cannot leave an xargs process forest behind.
# An explicit template, not `mktemp -t`: BSD mktemp (macOS) takes a bare prefix
# there, while GNU mktemp (Linux, and so CI) requires the trailing X's and fails
# with "too few X's in template". A full path template is accepted by both.
OUTPUT="$(mktemp "${TMPDIR:-/tmp}/clang_tidy_out.XXXXXX")"
CDB_DIR="$(mktemp -d "${TMPDIR:-/tmp}/clang_tidy_cdb.XXXXXX")"
trap 'rm -f "${OUTPUT}"; rm -rf "${CDB_DIR}"' EXIT
python3 tools/clang_tidy_compdb.py compile_commands.json "${CDB_DIR}/compile_commands.json"

RUNNER_ARGS=(
  --clang-tidy "${CLANG_TIDY}"
  --compile-database "${CDB_DIR}"
  --jobs "${PARALLELISM}"
  --output "${OUTPUT}"
  "--test-disabled-checks=${TEST_DISABLED_CHECKS}"
)
for FILE in ${SOURCES[@]+"${SOURCES[@]}"}; do
  RUNNER_ARGS+=(--source "${FILE}")
done
for FILE in ${TESTS[@]+"${TESTS[@]}"}; do
  RUNNER_ARGS+=(--test "${FILE}")
done

STATUS=0
python3 tools/clang_tidy_runner.py "${RUNNER_ARGS[@]}" || STATUS="${?}"

# A `clang-diagnostic-error` means the translation unit did not PARSE - a missing
# header, an unresolvable include. That is a broken environment, not a finding
# about the code, and it must not be reported as one: from the wreckage of a
# failed parse clang-tidy emits confident nonsense. It once claimed a variable
# `can be declared const` where const does not even compile, because the type
# that writes to it had become unknown.
#
# Both known causes are environmental and both are fixed the same way:
#   * a generated header that has not been built yet, and
#   * a dependency reached through a `_virtual_includes` directory, which bazel
#     materialises only once that target is built.
if grep -q 'clang-diagnostic-error' "${OUTPUT}"; then
  {
    echo ""
    echo "ERROR: clang-tidy: a source failed to PARSE (clang-diagnostic-error above)."
    echo "       These are NOT findings about the code. Any other diagnostic reported"
    echo "       for an affected file is unreliable and should not be 'fixed'."
    echo ""
    echo "       The usual cause is a compile DB that does not match the build tree:"
    echo "       generated headers or a dependency's '_virtual_includes' directory"
    echo "       exist only once bazel has built them. Rebuild and re-extract:"
    echo ""
    echo "         bazel build --config=clang-tidy //... && ./compile_commands-update.sh"
    echo ""
  } 1>&2
  exit 1
fi

exit "${STATUS}"
