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

"""Macro `diff_test` allows to compare two files."""

load("@bazel_skylib//lib:shell.bzl", "shell")
load("@mboworks_bzl//bzl/runfiles:runfiles.bzl", "runfiles")

def bool_arg(arg):
    return "1" if arg else "0"

def diff_test(
        name,
        file_old,
        file_new,
        *,
        file_header_use = "both",
        algorithm = "myers",
        context = -1,
        failure_message = "",
        format = "unified",
        ignore_all_space = False,
        ignore_consecutive_space = False,
        ignore_blank_lines = False,
        ignore_case = False,
        ignore_matching_chunks = True,
        ignore_matching_lines = "",
        ignore_trailing_space = False,
        ignore_missing_final_newline = False,
        minimal = False,
        regex_replace_lhs = "",
        regex_replace_rhs = "",
        size = "small",
        show_chunk_headers = True,
        skip_left_deletions = False,
        strip_comments = "",
        strip_parsed_comments = True,
        width = -1,
        **kwargs):
    """Create a diff test that compares `file_old` vs `file_new`.

    If the test detects differences, then the output will be in `diff -du` form.

    Args:
        name:                     Name of the test
        file_old:                 The old file.
        file_new:                 The new file.
        file_header_use:          Select which file header to use.
        algorithm:                Algorithm to use ('myers', 'naive', 'direct'; 'unified' is a deprecated alias for 'myers' implying unified format).
        context:                  Produces a diff with number of context lines (defaults to 0 for direct diff and normal format, 3 otherwise).
        failure_message:          Additional message to log if the files don't match.
        format:                   Output format to use ('unified', 'context', 'normal', 'side-by-side').
        ignore_all_space:         Ignore all leading, trailing, and consecutive internal whitespace changes.
        ignore_consecutive_space: Ignore all whitespace changes, even if one line has whitespace where the other line has none.
        ignore_blank_lines:       Ignore chunks which include only blank lines.
        ignore_case:              Whether to ignore letter case.
        ignore_matching_chunks:   Whether `ignore_matching_lines` applies to chunks or single lines.
        ignore_matching_lines:    Ignore lines that match this regexp (https://github.com/google/re2/wiki/Syntax).
        ignore_trailing_space:    Ignore traling whitespace changes.
        ignore_missing_final_newline: Ignore a missing final newline (a file with and one without a trailing newline compare equal).
        minimal:                  Guarantee minimal 'myers' diffs (disables its cost cap).
        regex_replace_lhs:        Regular expression and replacement for left side:  <sep><regex><sep><replace><sep>.
        regex_replace_rhs:        Regular expression and replacement for right side: <sep><regex><sep><replace><sep>.
        size:                     Bazel test size; these file comparisons are small by default.
        show_chunk_headers:       Whether to show the chunk headers.
        skip_left_deletions:      Ignore left deletions.
        strip_comments:           Strip out anything starting from `strip_comments`.
        strip_parsed_comments:    Whether to parse lines when stripping comments.
        width:                    Total output width for format = 'side-by-side' (-1 = tool default).
        **kwargs:                 Keyword args to pass down to native rules.
    """
    if algorithm == "unified" and format != "unified":
        fail("The deprecated algorithm = \"unified\" alias implies format = \"unified\" (got \"{}\").".format(format))
    _diff_test(
        name = name,
        file_old = file_old,
        file_new = file_new,
        algorithm = algorithm,
        context = context,
        file_header_use = file_header_use,
        failure_message = failure_message,
        format = format,
        is_windows = select({
            "@platforms//os:windows": True,
            "//conditions:default": False,
        }),
        ignore_all_space = ignore_all_space,
        ignore_consecutive_space = ignore_consecutive_space,
        ignore_blank_lines = ignore_blank_lines,
        ignore_case = ignore_case,
        ignore_matching_lines = ignore_matching_lines,
        ignore_matching_chunks = ignore_matching_chunks,
        ignore_trailing_space = ignore_trailing_space,
        ignore_missing_final_newline = ignore_missing_final_newline,
        minimal = minimal,
        regex_replace_lhs = regex_replace_lhs,
        regex_replace_rhs = regex_replace_rhs,
        size = size,
        show_chunk_headers = show_chunk_headers,
        skip_left_deletions = skip_left_deletions,
        strip_comments = strip_comments,
        strip_parsed_comments = strip_parsed_comments,
        width = width,
        **kwargs
    )

def _diff_test_impl(ctx):
    if ctx.attr.is_windows:
        fail("Windows not yet supported")
    else:
        test_bin = ctx.actions.declare_file(ctx.label.name + "-test.sh")
        strip_file_header_prefix = "external/(com_)?mboworks_mbo[^/]*/"
        ctx.actions.write(
            output = test_bin,
            content = r"""#!/usr/bin/env bash

set -xeuo pipefail

OLD="{file_old}"
NEW="{file_new}"
TOOL="{diff_tool}"
if [[ -d "${{RUNFILES_DIR:-/dev/null}}" && "${{RUNFILES_MANIFEST_ONLY:-}}" != 1 ]]; then
  OLD="${{RUNFILES_DIR}}/${{OLD}}"
  NEW="${{RUNFILES_DIR}}/${{NEW}}"
  TOOL="${{RUNFILES_DIR}}/${{TOOL}}"
elif [[ -f "${{RUNFILES_MANIFEST_FILE:-/dev/null}}" ]]; then
  OLD="$(grep -F -m1 "${{OLD}} " "${{RUNFILES_MANIFEST_FILE}}" | sed 's/^[^ ]* //')"
  NEW="$(grep -F -m1 "${{NEW}} " "${{RUNFILES_MANIFEST_FILE}}" | sed 's/^[^ ]* //')"
  TOOL="$(grep -F -m1 "${{TOOL}} " "${{RUNFILES_MANIFEST_FILE}}" | sed 's/^[^ ]* //')"
elif [[ -f "${{TEST_SRCDIR}}/${{OLD}}" && -f "${{TEST_SRCDIR}}/${{NEW}}" ]]; then
  OLD="${{TEST_SRCDIR}}/${{OLD}}"
  NEW="${{TEST_SRCDIR}}/${{NEW}}"
  TOOL="${{TEST_SRCDIR}}/${{TOOL}}"
else
  echo >&2 "ERROR: could not find \"{file_old}\" and \"{file_new}\""
  exit 1
fi
if ! "${{TOOL}}" "${{OLD}}" "${{NEW}}" \
    --algorithm={algorithm} \
    {context} \
    --file_header_use={file_header_use} \
    --format={format} \
    --ignore_all_space={ignore_all_space} \
    --ignore_consecutive_space={ignore_consecutive_space} \
    --ignore_blank_lines={ignore_blank_lines} \
    --ignore_case={ignore_case} \
    --ignore_matching_chunks={ignore_matching_chunks} \
    --ignore_matching_lines={ignore_matching_lines} \
    --ignore_trailing_space={ignore_trailing_space} \
    --ignore_missing_final_newline={ignore_missing_final_newline} \
    --regex_replace_lhs={regex_replace_lhs} \
    --regex_replace_rhs={regex_replace_rhs} \
    --show_chunk_headers={show_chunk_headers} \
    --strip_comments={strip_comments} \
    --strip_file_header_prefix={strip_file_header_prefix} \
    --strip_parsed_comments={strip_parsed_comments} \
; then
  EXTRA_FAILURE_MESSAGE={failure_message}
  echo >&2 "FAIL: files \"{file_old}\" and \"{file_new}\" differ.${{EXTRA_FAILURE_MESSAGE:+ }}${{EXTRA_FAILURE_MESSAGE}}"
  exit 1
fi
""".format(
                diff_tool = runfiles.key(ctx, ctx.executable._diff_tool),
                failure_message = shell.quote(ctx.attr.failure_message),
                file_old = runfiles.key(ctx, ctx.file.file_old),
                file_new = runfiles.key(ctx, ctx.file.file_new),
                algorithm = shell.quote(ctx.attr.algorithm),
                context = "" if ctx.attr.context == -1 else "--context=%d" % ctx.attr.context,
                file_header_use = shell.quote(ctx.attr.file_header_use),
                format = shell.quote(ctx.attr.format),
                ignore_all_space = bool_arg(ctx.attr.ignore_all_space),
                ignore_consecutive_space = bool_arg(ctx.attr.ignore_consecutive_space),
                ignore_blank_lines = bool_arg(ctx.attr.ignore_blank_lines),
                ignore_case = bool_arg(ctx.attr.ignore_case),
                ignore_matching_chunks = bool_arg(ctx.attr.ignore_matching_chunks),
                ignore_matching_lines = shell.quote(ctx.attr.ignore_matching_lines),
                ignore_trailing_space = bool_arg(ctx.attr.ignore_trailing_space),
                ignore_missing_final_newline = bool_arg(ctx.attr.ignore_missing_final_newline),
                minimal = bool_arg(ctx.attr.minimal),
                regex_replace_lhs = shell.quote(ctx.attr.regex_replace_lhs),
                regex_replace_rhs = shell.quote(ctx.attr.regex_replace_rhs),
                show_chunk_headers = bool_arg(ctx.attr.show_chunk_headers),
                skip_left_deletions = bool_arg(ctx.attr.skip_left_deletions),
                width = "" if ctx.attr.width == -1 else "--width=%d" % ctx.attr.width,
                strip_comments = shell.quote(ctx.attr.strip_comments),
                strip_file_header_prefix = shell.quote(strip_file_header_prefix),
                strip_parsed_comments = bool_arg(ctx.attr.strip_parsed_comments),
            ),
            is_executable = True,
        )
    return DefaultInfo(
        executable = test_bin,
        files = depset(direct = [test_bin]),
        runfiles = ctx.runfiles(files = [
            test_bin,
            ctx.executable._diff_tool,
            ctx.file.file_old,
            ctx.file.file_new,
        ]),
    )

_diff_test = rule(
    attrs = {
        "algorithm": attr.string(
            default = "myers",
            doc = "The diff algorithm to use ('unified' is a deprecated alias for 'myers', implying unified format).",
            values = ["direct", "myers", "naive", "unified"],
        ),
        "context": attr.int(
            default = -1,
            doc = "Produces a diff with number of context lines.",
        ),
        "failure_message": attr.string(
            doc = "An extra failure message to append to diff failure lines.",
        ),
        "file_header_use": attr.string(
            default = "both",
            doc = "Select which file header to use",
            values = ["both", "left", "none", "right"],
        ),
        "file_new": attr.label(
            allow_single_file = True,
            doc = "The new (right) file of the comparison.",
            mandatory = True,
        ),
        "file_old": attr.label(
            allow_single_file = True,
            doc = "The old (left) file of the comparison.",
            mandatory = True,
        ),
        "format": attr.string(
            default = "unified",
            doc = "The diff output format to use.",
            values = [
                "context",
                "normal",
                "side-by-side",
                "unified",
            ],
        ),
        "ignore_all_space": attr.bool(
            doc = "Ignore all leading, trailing, and consecutive internal whitespace changes.",
            default = False,
        ),
        "ignore_blank_lines": attr.bool(
            doc = "Ignore chunks which include only blank lines.",
            default = False,
        ),
        "ignore_case": attr.bool(
            doc = "Whether to ignore letter case.",
            default = False,
        ),
        "ignore_consecutive_space": attr.bool(
            doc = "Ignore all whitespace changes, even if one line has whitespace where the other line has none.",
            default = False,
        ),
        "ignore_matching_chunks": attr.bool(
            doc = "Whether `ignore_matching_lines` applies to chanks or single lines.",
            default = True,
        ),
        "ignore_matching_lines": attr.string(
            doc = "Ignore lines that match this regexp (https://github.com/google/re2/wiki/Syntax).",
        ),
        "ignore_trailing_space": attr.bool(
            doc = "Ignore traling whitespace changes.",
            default = False,
        ),
        "ignore_missing_final_newline": attr.bool(
            doc = "Ignore a missing final newline (a file with and one without a trailing newline compare equal).",
            default = False,
        ),
        "is_windows": attr.bool(
            mandatory = True,
        ),
        "minimal": attr.bool(
            doc = "Guarantee minimal 'myers' diffs (disables its cost cap).",
            default = False,
        ),
        "regex_replace_lhs": attr.string(
            doc = "Regular expression and replacement for left side:  <sep><regex><sep><replace><sep>.",
            default = "",
        ),
        "regex_replace_rhs": attr.string(
            doc = "Regular expression and replacement for right side:  <sep><regex><sep><replace><sep>.",
            default = "",
        ),
        "show_chunk_headers": attr.bool(
            doc = "Whether to show the chunk headers.",
            default = True,
        ),
        "skip_left_deletions": attr.bool(
            doc = "Ignore left deletions.",
            default = False,
        ),
        "strip_comments": attr.string(
            doc = "Strip out any thing starting from `strip_comments`.",
        ),
        "strip_parsed_comments": attr.bool(
            doc = "Whether to parse lines when stripping comments.",
            default = True,
        ),
        "width": attr.int(
            default = -1,
            doc = "Total output width for format = 'side-by-side' (-1 = tool default).",
        ),
        "_diff_tool": attr.label(
            doc = "The diff tool executable.",
            default = Label("//mbo/diff"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
        ),
    },
    test = True,
    implementation = _diff_test_impl,
    provides = [DefaultInfo],
)
