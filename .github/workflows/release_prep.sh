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

# invoked by release workflow
# (via https://github.com/bazel-contrib/.github/blob/master/.github/workflows/release_ruleset.yaml)

set -euo pipefail

# Custom args to update as needed.
PACKAGE_NAME="mbo"
BAZELMOD_NAME="mboworks_mbo"
PATCHES=(
  ".github/workflows/bazelmod.patch"
)

# Automatic vars from workflow integration.
TAG="${GITHUB_REF_NAME}"

function die() {
  echo "ERROR: ${*}" 1>&2
  exit 1
}

# Computed vars.
PREFIX="${PACKAGE_NAME}-${TAG}"
ARCHIVE="${PACKAGE_NAME}-${TAG}.tar.gz"
BAZELMOD_VERSION="$(sed -rne 's,.*version = "([0-9]+([.][0-9]+)+.*)".*,\1,p' <MODULE.bazel | head -n1)"
CHANGELOG_VERSION="$(sed -rne 's,^# ([0-9]+([.][0-9]+)+.*)$,\1,p' <CHANGELOG.md | head -n1)"

if [ "${BAZELMOD_VERSION}" != "${TAG}" ]; then
  die "Tag = '${TAG}' does not match version = '${BAZELMOD_VERSION}' in MODULE.bazel."
fi
if [ "${CHANGELOG_VERSION}" != "${TAG}" ]; then
  die "Tag = '${TAG}' does not match version = '${CHANGELOG_VERSION}' in CHANGELOG.md."
fi

# Instead of embed the version in MODULE.bazel, we expect it to be correct already.
# perl -pi -e "s/version = \"\d+\.\d+\.\d+\",/version = \"${TAG}\",/g" MODULE.bazel

# Empty `BUILD.bazel`
{
  cat tools/header.txt
  echo ""
  echo "\"\"\"Empty root BUILD for @${BAZELMOD_NAME}.\"\"\""
} >BUILD.bazel

# Apply patches
for patch in ${PATCHES[@]+"${PATCHES[@]}"}; do
  patch -s -p 1 <"${patch}"
done

# Exclude some dev stuff from the archive.
EXCLUDES=(
  ".bcr"
  ".gitattributes"
  ".github"
  ".gitignore"
  ".pre-commit"
  ".pre-commit-config.yaml"
  ".trunk"
  "mbo/hash/measurements"
  "tools"
)
{
  for exclude in "${EXCLUDES[@]}"; do
    echo "${exclude} export-ignore"
    if [[ -d ${exclude} ]]; then
      echo "${exclude}/** export-ignore"
    fi
  done
} >>.gitattributes

# Build the archive from the patched/generated worktree, not the committed
# "${TAG}" tree: `git archive "${TAG}"` reads the commit and would silently drop
# the edits above (the bazelmod.patch hunk, the generated BUILD.bazel). Stage the
# worktree into a THROWAWAY index so the real index/checkout is never touched
# (nothing to undo afterwards), and archive that tree. export-ignore still
# applies via the staged .gitattributes (+ --worktree-attributes).
TMP_INDEX_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mbo-release-index.XXXXXX")"
TMP_INDEX="${TMP_INDEX_DIR}/index"
function cleanup() {
  rm -rf -- "${TMP_INDEX_DIR}"
}
trap cleanup EXIT
GIT_INDEX_FILE="${TMP_INDEX}" git read-tree HEAD
GIT_INDEX_FILE="${TMP_INDEX}" git add --all
ARCHIVE_TREE="$(GIT_INDEX_FILE="${TMP_INDEX}" git write-tree)"
git archive --format=tar.gz --prefix="${PREFIX}/" -o "${ARCHIVE}" --add-virtual-file="${PREFIX}/VERSION:${TAG}" --worktree-attributes "${ARCHIVE_TREE}"

# Print header
echo "# Version ${TAG}"
echo "## [Changelog](https://github.com/mboworks/${PACKAGE_NAME}/blob/${TAG}/CHANGELOG.md)"

# Print Changelog
awk '/^#/{if(NR>1)exit}/^[^#]/{print}' <CHANGELOG.md

cat <<EOF
## For Bazel MODULES.bazel

\`\`\`bzl
bazel_dep(name = "${BAZELMOD_NAME}", version = "${TAG}")
\`\`\`

### Using the provided LLVM

Copy [llvm.MODULE.bazel](https://github.com/mboworks/${PACKAGE_NAME}/blob/main/bazelmod/llvm.MODULE.bazel) to your repository's root directory and add the following line to your MODULES.bazel file or paste the whole contents into it.

\`\`\`bzl
include("//:llvm.MODULE.bazel")
\`\`\`

### Using the provided development modules

Copy [dev.MODULE.bazel](https://github.com/mboworks/${PACKAGE_NAME}/blob/main/bazelmod/dev.MODULE.bazel) to your repository's root directory and add the following line to your MODULES.bazel file or paste the whole contents into it. It provides the dev-only Hedron compile-commands extractor (generates compile_commands.json for clangd) and depend_on_what_you_use.

\`\`\`bzl
include("//:dev.MODULE.bazel")
\`\`\`
EOF

printf '\n'
bash tools/release_notes.sh "${TAG}"
