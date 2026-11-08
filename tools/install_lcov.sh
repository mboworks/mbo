#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly version="2.5"
readonly sha256="7e5e5a154bd5f3557659c328cab376764e7abd238bb403c424472c296b175126"
readonly destination="${1:?usage: install_lcov.sh DESTINATION}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/lcov.tar.gz"
checksum="${work}/lcov.sha256"

curl --fail --location --retry 20 --retry-delay 5 \
  "https://github.com/linux-test-project/lcov/releases/download/v${version}/lcov-${version}.tar.gz" \
  --output "${archive}"
printf '%s  %s\n' "${sha256}" "${archive}" >"${checksum}"
sha256sum -c "${checksum}" >/dev/null
mkdir -p "${destination}"
# The executables resolve the bundled Perl libraries relative to this release
# tree. Keep that layout instead of invoking the unrelated documentation build.
tar -xzf "${archive}" --directory "${destination}" --strip-components=1
"${destination}/bin/genhtml" --version
