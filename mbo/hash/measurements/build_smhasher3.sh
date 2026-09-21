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

# Reproducible SMHasher3 build for mbo/hash quality verification.
#
# Clones SMHasher3 at the pinned commit, applies the two documented build fixes,
# and builds it with gcc inside a linux container, matching the methodology in
# mbo/hash/README.md ("Methodology (reproduction)"). Prints the path to the
# built SMHasher3 binary; drive it with
#   hash_benchmark_report.py smhasher --smhasher3 <path> --algos all
#
# The third-party algorithms (rapidhash, xxh3, xxh64, murmur3, siphash, fnv1a)
# are SMHasher3 built-ins and work immediately. The in-house mumbo/jumbo and dumbo
# need a registration source dropped into SMHasher3's hashes/ tree BEFORE the
# build (a transcription registering "mumbo-64" / "jumbo-128" and "dumbo-64",
# matching mbo/hash/hash_mumbo.h and hash_dumbo.h) - that plugin is the
# outstanding piece; see mbo/hash/measurements/README.md.
#
# Usage: build_smhasher3.sh [work_dir]   (default: ./.smhasher3)

set -euo pipefail

# Pinned upstream (gitlab.com/fwojcik/smhasher3), same as the README table.
readonly SMHASHER3_REPO="https://gitlab.com/fwojcik/smhasher3.git"
readonly SMHASHER3_COMMIT="6ab4343396fbe0f7a1c7ac4f01d0eb9acffe4202"
# gcc 13 on linux; -march=armv8-a+crc for arm64 (drop CRC on x86_64 - see below).
readonly BUILD_IMAGE="gcc:13"

WORK_DIR="${1:-$(pwd)/.smhasher3}"
SRC_DIR="${WORK_DIR}/smhasher3"

mkdir -p "${WORK_DIR}"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
  git clone "${SMHASHER3_REPO}" "${SRC_DIR}"
fi
git -C "${SRC_DIR}" fetch --depth=1 origin "${SMHASHER3_COMMIT}"
git -C "${SRC_DIR}" checkout --quiet "${SMHASHER3_COMMIT}"

# Fix 1: lib/AEStest.cpp is missing <cstdlib> (used for abort()/exit()).
if ! grep -q '#include <cstdlib>' "${SRC_DIR}/lib/AEStest.cpp"; then
  sed -i.bak '1i\
#include <cstdlib>
' "${SRC_DIR}/lib/AEStest.cpp"
fi

# Fix 2: CMakeLists.txt uses -march=native, which emits SHA3 (eor3) instructions
# the container toolchain rejects. Pin an explicit, portable arch instead.
#   arm64 -> armv8-a+crc ; x86_64 -> x86-64-v2 (has the CRC/SSE SMHasher3 wants).
arch="$(uname -m)"
if [[ "${arch}" == "x86_64" || "${arch}" == "amd64" ]]; then
  march="x86-64-v2"
else
  march="armv8-a+crc"
fi
sed -i.bak "s/-march=native/-march=${march}/g" "${SRC_DIR}/CMakeLists.txt"

# Install the in-house plugin: it #includes the ACTUAL mbo/hash headers (so the
# real implementation is verified, not a transcription). Copy the four headers
# preserving the mbo/hash/ path, drop in the registration source, register it,
# and switch to C++23 (the mbo baseline).
REPO="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
HASH_SRC="${REPO}/mbo/hash"
HASH_DST="${SRC_DIR}/mbo_include/mbo/hash"
mkdir -p "${HASH_DST}"
cp "${HASH_SRC}/hash_dumbo.h" "${HASH_DST}/"
cp "${HASH_SRC}/hash_fambo.h" "${HASH_DST}/"
cp "${HASH_SRC}/hash_internal_util.h" "${HASH_DST}/"
cp "${HASH_SRC}/hash_mumbo.h" "${HASH_DST}/"
cp "${HASH_SRC}/hash_tembo.h" "${HASH_DST}/"
cp "${HASH_SRC}/hash_types.h" "${HASH_DST}/"
cp "${HASH_SRC}/measurements/smhasher3/mbohash.cpp" "${SRC_DIR}/hashes/mbohash.cpp"
grep -q 'hashes/mbohash.cpp' "${SRC_DIR}/hashes/Hashsrc.cmake" \
  || sed -i.bak 's#\(set(HASH_SRC_FILES\)#\1\n  hashes/mbohash.cpp#' "${SRC_DIR}/hashes/Hashsrc.cmake"
sed -i.bak 's/set(CMAKE_CXX_STANDARD 11)/set(CMAKE_CXX_STANDARD 23)/' "${SRC_DIR}/CMakeLists.txt"
grep -q 'set(CMAKE_CXX_STANDARD 23)' "${SRC_DIR}/CMakeLists.txt" \
  || {
    echo "Could not select C++23 in SMHasher3 CMakeLists.txt" >&2
    exit 1
  }
# The ${CMAKE_*} tokens are meant to land literally in CMakeLists.txt, not expand here.
# shellcheck disable=SC2016
grep -q 'mbo_include' "${SRC_DIR}/CMakeLists.txt" \
  || sed -i.bak 's#\(include_directories(${CMAKE_BINARY_DIR}/include)\)#\1\ninclude_directories(${CMAKE_SOURCE_DIR}/mbo_include)#' \
    "${SRC_DIR}/CMakeLists.txt"

# Build with gcc in a container (native on arm64 hosts; matches the README rig).
# NOTE: the work dir must be under a path Docker Desktop shares with the VM
# (e.g. under $HOME on macOS); /private/tmp is not shared by default.
docker run --rm -v "${SRC_DIR}:/src" -w /src "${BUILD_IMAGE}" bash -c '
  set -euo pipefail
  command -v cmake >/dev/null || { apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq cmake >/dev/null; }
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
'

echo "SMHasher3 built: ${SRC_DIR}/build/SMHasher3"
echo "In-house hashes: ${SRC_DIR}/build/SMHasher3 --list | grep -Ei 'mumbo|jumbo|dumbo|fambo|tembo'"
