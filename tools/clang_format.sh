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

# Use the formatter from the pinned toolchains_llvm distribution in every compiler mode.
set -euo pipefail

bazel build --config=clang --ui_event_filters=-info --noshow_progress \
  @llvm_toolchain_llvm//:clang-format
CLANG_FORMAT_FILES="$(bazel cquery --config=clang --ui_event_filters=-info --noshow_progress \
  --output=files @llvm_toolchain_llvm//:clang-format)"
EXEC_ROOT="$(bazel info execution_root)"
# cquery can retain the same tool in target and exec configurations. Each path is a
# candidate for the pinned formatter, not another line of a single executable name.
while IFS= read -r CLANG_FORMAT; do
  [[ -n "${CLANG_FORMAT}" ]] || continue
  if [[ "${CLANG_FORMAT}" != /* ]]; then
    CLANG_FORMAT="${EXEC_ROOT}/${CLANG_FORMAT}"
  fi
  if [[ -x "${CLANG_FORMAT}" ]]; then
    exec "${CLANG_FORMAT}" "${@}"
  fi
done <<<"${CLANG_FORMAT_FILES}"
printf '%s\n' "ERROR: no executable clang-format output from the pinned toolchain" >&2
exit 1
