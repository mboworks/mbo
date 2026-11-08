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

"""Rules mope() and mope_test().

* Rule `mope` takes one or more `srcs` and produces matching `outs`.

* Rule `mope_test` uses `mope` to generate outputs and compares them to existing golden files.
"""

load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("//mbo/diff:diff.bzl", "diff_test")

# The `clang-format` tool is selected as follows:
# 1) If custom bazel flag `--//mbo/mope:clang_format` is set, then that value will be used.
# 2) If the custom flag is empty and this variable is a non empty string, then use its value.
#    Otherwise use `@llvm_toolchain_llvm//:clang-format`.
# 3) If the resulting value is `clang-format-auto` (or the above result is not found or not
#    executable), then the rule tries to find the tool:
#    a) clang-format found through `toolchains_llvm` in bazel's external directory
#    b) `${LLVM_PATH}/bin/clang-format` (requires additional patching to pass down the environment variable).
#    c) `$(which "clang_format")`
#    d) `clang-format-23` ... `clang-format-15`, `clang-format`
#    e) If even (d) fails, then we will just copy the file.
_CLANG_FORMAT_BINARY = ""  # Ignore clang-format from repo with: "clang-format-auto"

def _get_clang_format(ctx):
    """Get the selected clang-format from `--//mbo/mope:clang_format` bazel flag."""
    return ctx.attr._clang_format_flag[BuildSettingInfo].value

def _clang_format_impl(ctx, src, dst):
    """Clang-format a file.

    Creates an action that performs clang-format on a file and writes the output
    to a separate file.

    Args:
      ctx: The current rule's context object.
      src: The source file.
      dst: The name of the output file.

    Returns:
      The output file.
    """
    execution_requirements = {"no-sandbox": "1"} if _CLANG_FORMAT_BINARY else {}
    clang_config = ctx.files._clang_format_config[0]
    clang_format_tool = [] if _CLANG_FORMAT_BINARY else [ctx.attr._clang_format_tool[DefaultInfo].files_to_run]
    clang_format = _get_clang_format(ctx)
    if not clang_format:
        clang_format = ctx.attr._clang_format_tool if _CLANG_FORMAT_BINARY else ctx.executable._clang_format_tool.path
    ctx.actions.run_shell(
        outputs = [dst],
        inputs = [src, clang_config],
        tools = clang_format_tool,
        command = """
            CLANG_FORMAT="{clang_format}"
            if [ "{clang_format}" == "clang-format-auto" ] || [ ! -x "${{CLANG_FORMAT}}" ]; then
                declare -a CLANG_FORMAT_LOCATIONS=(
                    # Bazel workspaces
                    "external/llvm_toolchain_llvm/bin/clang-format"
                    "external/llvm_toolchain/bin/clang-format"
                    # Bazel modules < 8
                    "external/toolchains_llvm~~llvm~llvm_toolchain_llvm/bin/clang-format"
                    "external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/bin/clang-format"
                    # Bazel modules >= 8
                    "external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-format"
                    "external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang-format"
                    "${{LLVM_PATH}}/bin/clang-format"  # Requires additional env var pass through.
                    "$(which clang-format-23)"
                    "$(which clang-format-22)"
                    "$(which clang-format-21)"
                    "$(which clang-format-20)"
                    "$(which clang-format-19)"
                    "$(which clang-format-18)"
                    "$(which clang-format-17)"
                    "$(which clang-format-16)"
                    "$(which clang-format-15)"
                    "$(which clang_format)"
                )
                CLANG_FORMAT=
                for CLANG_FORMAT_LOCATION in "${{CLANG_FORMAT_LOCATIONS[@]}}"; do
                    if [ -x "${{CLANG_FORMAT_LOCATION}}" ]; then
                        CLANG_FORMAT="${{CLANG_FORMAT_LOCATION}}"
                        echo "INFO: ClangFormat: $(${{CLANG_FORMAT}} --version): ${{CLANG_FORMAT}}"
                        break
                    fi
                done
            fi;
            if [ -n "${{CLANG_FORMAT}}" ]; then
                # Must cat (<), so that --assume-filename works, so that incldue order gets correct.
                ${{CLANG_FORMAT}} \\
                    --assume-filename={assume_filename} \\
                    --fallback-style={fallback_style} \\
                    --sort-includes={sort_includes} \\
                    --style=file:{clang_config} \\
                    --Werror \\
                    < {src} > {dst} || (echo "CLANG($("${{CLANG_FORMAT}}" --version)) = '${{CLANG_FORMAT}}'" ; false)
            else
                echo "WARNING: No clang-format found for: {dst}" > /dev/stderr
                cp {src} {dst}
            fi;
            """.format(
            assume_filename = dst.short_path.removesuffix(".gen"),
            clang_format = clang_format,
            clang_config = clang_config.path,
            dst = dst.path,
            fallback_style = ctx.attr._clang_fallback_style,
            sort_includes = "1" if ctx.attr.sort_includes else "0",
            src = src.path,
        ),
        execution_requirements = execution_requirements,
        mnemonic = "ClangFormat",
        progress_message = "Clang-Format on file: %s" % (src.path),
        use_default_shell_env = True,
    )

_clang_format_common_attrs = {
    "sort_includes": attr.bool(
        doc = "Passes --sort-includes to clang-tidy tool if True.",
        default = True,
    ),
    "_clang_fallback_style": attr.string(
        doc = "The fllback stype to pass to clang-format, e.g. 'None' or 'Google'.",
        default = "Google",
    ),
    "_clang_format_config": attr.label(
        doc = "The `.clang-format` file.",
        default = Label("//:clang-format"),
        allow_single_file = [".clang-format"],
    ),
    "_clang_format_flag": attr.label(
        doc = "The flag for the clang-format executable.",
        default = Label("//mbo/mope:clang_format"),
    ),
    "_clang_format_tool": attr.string(
        doc = "The target of the clang-format executable.",
        default = _CLANG_FORMAT_BINARY or "clang-format-auto",
    ) if _CLANG_FORMAT_BINARY else attr.label(
        doc = "The target of the clang-format executable.",
        default = Label("@llvm_toolchain_llvm//:clang-format"),
        allow_single_file = True,
        executable = True,
        cfg = "exec",
    ),
}

def _rule_name(str):
    res = ""
    for pos in range(len(str)):
        if str[pos].isalnum():
            res += str[pos]
        else:
            res += "_"
    return res

def _base_no_ext(file):
    # Other files.
    if type(file) == "string":
        name = file.split(":")[-1]
    else:
        name = file.basename
    if not "." in name:
        return name
    return ".".join(name.split(".")[:-1])

def _template_base_no_ext(file):
    # Templates are defined to only accept '.tpl' or '.mope'.
    if type(file) == "string":
        name = file.split(":")[-1]
    else:
        name = file.basename
    if name.endswith(".mope"):
        return name.removesuffix(".mope")
    else:
        return name.removesuffix(".tpl")

def _template_path_no_ext(file):
    # Templates are defined to only accept '.tpl' or '.mope'.
    if type(file) == "string":
        name = file
    else:
        name = file.short_path
    if name.endswith(".mope"):
        return name.removesuffix(".mope")
    else:
        return name.removesuffix(".tpl")

def _gen_name_base_no_ext(file):
    # Remove the optional ".golden" and ".gen" extension and the remaining extension.
    if type(file) == "string":
        name = file.split(":")[-1]
    else:
        name = file.basename
    if name.endswith(".golden"):
        return name.removesuffix(".golden")
    else:
        return name.removesuffix(".gen")

def _build_relative(ctx, file):
    path = "/".join(ctx.build_file_path.split("/")[:-1])
    res = path + "/" + file.basename
    return res

def _mope_rule_impl(ctx):
    srcs = {_template_base_no_ext(src): src for src in ctx.files.srcs}
    outs = {_gen_name_base_no_ext(out): out for out in ctx.outputs.outs}
    inis = {_base_no_ext(ini): ini.path for ini in ctx.files.data if ini.extension == "ini"}
    if srcs.keys() != outs.keys():
        fail("Files in `srcs` and `outs` do not match on basenames without extensions.")
    for key, src in srcs.items():
        out = outs[key]
        if ctx.attr.clang_format:
            gen = ctx.actions.declare_file(_build_relative(ctx, out) + ".tmp")
        else:
            gen = out
        ctx.actions.run(
            arguments = ctx.attr.args + [
                "--template=" + src.path,
                "--generate=" + gen.path,
                "--ini=" + inis.get(key, ""),
            ],
            executable = ctx.executable._mope_tool,
            inputs = ctx.files.srcs + ctx.files.data,
            mnemonic = "Mope",
            outputs = [gen],
            progress_message = "Mope over " + key,
            tools = ctx.attr._mope_tool.files,
            use_default_shell_env = True,
        )
        if ctx.attr.clang_format:
            _clang_format_impl(ctx, gen, out)
    return [
        DefaultInfo(
            files = depset(ctx.outputs.outs),
            runfiles = ctx.runfiles(files = ctx.outputs.outs),
        ),
    ]

_mope_attrs = {
    "args": attr.string_list(
        doc = "Args to be passed to the mope tool.",
    ),
    "clang_format": attr.bool(
        doc = "Whether to apply clang-format to the generated result.",
        default = False,
    ),
    "data": attr.label_list(
        allow_files = [".ini"],
        default = [],
        doc = "All data files (excl. sources and outs).",
        mandatory = False,
    ),
    "outs": attr.output_list(
        doc = "List of generated file(s). This must match srcs by matching the basenames without extensions (" +
              "e.g. a template `foo.cc.tpl` must have an output `foo.cc`).",
        mandatory = True,
    ),
    "srcs": attr.label_list(
        allow_files = [".tpl", ".mope"],
        doc = "List of templates.",
        mandatory = True,
    ),
    "_mope_tool": attr.label(
        cfg = "exec",
        default = Label("//mbo/mope"),
        doc = "The `mope` tool.",
        executable = True,
    ),
}

mope = rule(
    attrs = dicts.add(
        _mope_attrs,
        _clang_format_common_attrs,
    ),
    doc = "Expand the given `srcs` template(s) to generate `outs`. For each file in `srcs` the required '.tpl' or " +
          "'.mope' extension will be stripped of. The resulting filename must have a corresponding metch in outs " +
          "when a possible '.gen' or '.golden' extension is stripped of.",
    implementation = _mope_rule_impl,
    output_to_genfiles = True,
    provides = [DefaultInfo],
)

def mope_test(
        name,
        srcs,
        outs,
        data = [],
        clang_format = False,
        args = [],
        **kwargs):
    """Run mope over all `srcs` and compare the results with `outs`.

    Args:
        name: Name of the resulting test_suite
        srcs: The mope template(s).
        outs: The corresponding golden generated files.
        data: additional data files (e.g. INI files).
        clang_format: Whether to apply clang-fromat to the generated results.
        args: Arguments to pass to the mope tool.
        **kwargs: Other args (e.g. tags, visibility).
    """
    if name.endswith("_test"):
        base_name = name.removesuffix("_test")
    elif name.endswith("_tests"):
        base_name = name.removesuffix("_tests")
    else:
        fail("mope_test rule names must end in '_test' or '_tests'.")
    gens = {_template_base_no_ext(src): _template_path_no_ext(src) + ".gen" for src in srcs}
    outs = {_gen_name_base_no_ext(out): out for out in outs}
    if gens.keys() != outs.keys():
        fail("Files in `srcs` and `outs` do not match on basenames without extensions.")
    mope(
        name = base_name + "_mope",
        args = args,
        clang_format = clang_format,
        data = data,
        outs = gens.values(),
        srcs = srcs,
        testonly = 1,
        **kwargs
    )
    tests = []
    for key, gen in gens.items():
        if len(gens) > 1:
            test_name = base_name + "_" + _rule_name(key) + "_diff_test"
        else:
            test_name = base_name + "_diff_test"
        tests.append(test_name)
        diff_test(
            name = test_name,
            file_old = outs[key],
            file_new = gen,
            failure_message = "The existing old file and the newly generated file differ.",
            **kwargs
        )
    native.test_suite(
        name = name,
        tests = tests,
        **kwargs
    )
