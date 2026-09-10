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

"""Manual targets that must still be indexed for clang-tidy.

A `manual` target is excluded from wildcard patterns, and aquery applies that
exclusion while expanding `//...` - before any `attr(tags, ...)` filter can see
it. So a tag alone cannot pull these into `compile_commands.json`; the labels
have to be named explicitly in `//bazelmod:refresh_compile_commands`.

The `clang-tidy` tag on each of these targets remains the source of truth for
INTENT ("this manual target is meant to be linted"), and the
`clang-tidy-targets-match-tag` pre-commit hook fails if this list and the tag
query disagree - so the list cannot silently drift as targets are added,
removed or retagged.

Note this covers everything bazel builds. The one repository source that is not
in `compile_commands.json`, `mbo/hash/measurements/smhasher3/mbohash.cpp`, cannot
be: it is a plugin for SMHasher3, copied into that project by
`mbo/hash/measurements/build_smhasher3.sh` and compiled by ITS cmake. There is no
BUILD file for it here and thus nothing to tag, and linting it against this
repository's flags would only produce bogus errors for the SMHasher3 headers
(`Platform.h`, `Hashlib.h`) it includes.
"""

CLANG_TIDY_MANUAL_TARGETS = [
    "//mbo/container:limited_set_benchmark",
    "//mbo/container:ordered_lookup_benchmark",
    "//mbo/container:segmented_sequence_benchmark",
    "//mbo/container:segmented_sequence_element_shape_benchmark",
    "//mbo/container:segmented_sequence_layout_benchmark",
    "//mbo/container:segmented_sequence_lifecycle_benchmark",
    "//mbo/container:segmented_sequence_pool_benchmark",
    "//mbo/container:segmented_sequence_retention_benchmark",
    "//mbo/diff:diff_benchmark",
    "//mbo/hash:hash_benchmark",
    "//mbo/hash:hash_differential_test",
    "//mbo/memory:arena_benchmark",
    "//tools:show_compiler",
]
