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

set -euo pipefail

function die() {
  echo "ERROR: ${*}" 1>&2
  exit 1
}

DRY_RUN=false
VERSION=""
for arg in "${@}"; do
  case "${arg}" in
    --dry | --dry-run) DRY_RUN=true ;;
    -*) die "Unknown option '${arg}'. Usage: ${0} [--dry-run] <version>" ;;
    *)
      [[ -z "${VERSION}" ]] || die "Usage: ${0} [--dry-run] <version>"
      VERSION="${arg}"
      ;;
  esac
done
[[ -n "${VERSION}" ]] || die "Usage: ${0} [--dry-run] <version>"
[[ "${VERSION}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] \
  || die "Version must be numeric <major>.<minor>.<patch> (got '${VERSION}')."

IFS=. read -r VERSION_MAJOR VERSION_MINOR VERSION_PATCH <<<"${VERSION}"
NEXT_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.$((VERSION_PATCH + 1))"
NEXT_BRANCH="chore/bump_version_to_${NEXT_VERSION}"

for tool in gh git gpg; do
  command -v "${tool}" >/dev/null 2>&1 || die "Required tool '${tool}' is not installed."
done

git fetch origin main --tags
[[ "$(git branch --show-current)" == "main" ]] || die "Must be run from main."
[[ -z "$(git status --porcelain)" ]] || die "Working tree must be clean."
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || die "Local main must equal origin/main."

BAZELMOD_VERSION="$(sed -rne 's,.*version = "([0-9]+([.][0-9]+)+.*)".*,\1,p' <MODULE.bazel | head -n1)"
CHANGELOG_VERSION="$(sed -rne 's,^# ([0-9]+([.][0-9]+)+.*)$,\1,p' <CHANGELOG.md | head -n1)"
[[ "${BAZELMOD_VERSION}" == "${CHANGELOG_VERSION}" ]] || die "MODULE.bazel (${BAZELMOD_VERSION}) != CHANGELOG.md (${CHANGELOG_VERSION})."
[[ "${VERSION}" == "${BAZELMOD_VERSION}" ]] || die "Requested version (${VERSION}) != repository version (${BAZELMOD_VERSION})."
[[ -z "$(git tag --list "${VERSION}")" ]] || die "Tag '${VERSION}' already exists."
gh release view "${VERSION}" >/dev/null 2>&1 && die "Release '${VERSION}' already exists."

# Pre-flight: release_prep.sh applies .github/workflows/bazelmod.patch to the
# worktree before archiving (it shapes the released MODULE.bazel/mope.bzl). If it
# no longer applies (e.g. context drift after a dependency bump) the release
# would tag and then fail mid-build. Catch it here, before we tag anything.
patch -p1 --dry-run -f -i .github/workflows/bazelmod.patch >/dev/null 2>&1 \
  || die "Patch .github/workflows/bazelmod.patch no longer applies; regenerate it before releasing."

if [[ "${DRY_RUN}" == true ]]; then
  echo "[dry-run] Would create and push signed tag '${VERSION}' at $(git rev-parse HEAD)."
  echo "[dry-run] Would open '${NEXT_BRANCH}' to bump the repository to '${NEXT_VERSION}'."
  exit 0
fi

git tag -s -a "${VERSION}" \
  -m "New release tag version: '${VERSION}'." \
  -m "$(awk '/^#/{if(NR>1)exit}/^[^#]/{print}' <CHANGELOG.md)"
git push origin "refs/tags/${VERSION}"
echo "Pushed signed release tag '${VERSION}'. GitHub Actions will create the release and BCR PR."

# Immediately prepare the next patch version on a reviewable branch. The
# release tag remains at the exact validated main commit above; this branch is
# deliberately not merged or approved by the release script.
sed "1,/version = \"${VERSION}\"/ s/version = \"${VERSION}\"/version = \"${NEXT_VERSION}\"/" \
  MODULE.bazel >MODULE.bazel.tmp
mv MODULE.bazel.tmp MODULE.bazel
{
  printf '# %s\n\n' "${NEXT_VERSION}"
  cat CHANGELOG.md
} >CHANGELOG.md.tmp
mv CHANGELOG.md.tmp CHANGELOG.md

git switch -c "${NEXT_BRANCH}"
git add MODULE.bazel CHANGELOG.md
git commit -m "Bump version from ${VERSION} to ${NEXT_VERSION}"
git push --set-upstream origin "${NEXT_BRANCH}"

PR_BODY="Prepare the repository for development after the ${VERSION} release.

## AG;DR

- Bump the module version from ${VERSION} to ${NEXT_VERSION}.
- Add the empty ${NEXT_VERSION} changelog section for subsequent changes.
- Created automatically by \`${0}\`; this PR still requires normal review."

if gh pr create \
  --title "Bump version from ${VERSION} to ${NEXT_VERSION}" \
  --body "${PR_BODY}"; then
  echo "Opened the version-bump PR for '${NEXT_BRANCH}'. Have another maintainer review and merge it."
else
  echo "WARNING: Could not create the PR automatically; open one from branch '${NEXT_BRANCH}'." 1>&2
fi

git switch main
