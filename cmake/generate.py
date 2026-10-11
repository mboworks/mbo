# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Adapt the existing Bazel C++ declarations and generators for CMake.

BUILD files are parsed, never executed. Only the literal lists and compiler/
requirement-policy selects used by MBO's C++ targets are supported. Unsupported
expressions fail configuration instead of silently changing the build graph.
"""

import argparse
import ast
from pathlib import Path
from types import SimpleNamespace


def evaluate(node, compiler, require_throws):
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return evaluate(node.left, compiler, require_throws) + evaluate(
            node.right, compiler, require_throws
        )
    if (
        isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "select"
    ):
        choices = ast.literal_eval(node.args[0])
        supported = {
            "@rules_cc//cc/compiler:gcc",
            "@rules_cc//cc/compiler:clang",
            "//mbo/config:require_throws_enabled",
            "//conditions:default",
        }
        if set(choices) - supported:
            raise ValueError(
                "unsupported select conditions: " + str(set(choices) - supported)
            )
        for condition in (
            "//mbo/config:require_throws_enabled" if require_throws else "",
            "@rules_cc//cc/compiler:" + compiler,
            "//conditions:default",
        ):
            if condition in choices:
                return choices[condition]
        raise ValueError("select has no applicable condition")
    return ast.literal_eval(node)


def declarations(root, compiler, require_throws):
    targets = {}
    for path in sorted((root / "mbo").rglob("BUILD.bazel")):
        for statement in ast.parse(path.read_text()).body:
            if not isinstance(statement, ast.Expr) or not isinstance(
                statement.value, ast.Call
            ):
                continue
            call = statement.value
            if not isinstance(call.func, ast.Name) or call.func.id not in (
                "cc_library",
                "cc_binary",
                "cc_test",
            ):
                continue
            attrs = {kw.arg: kw.value for kw in call.keywords}
            if "linkopts" in attrs or (
                call.func.id == "cc_library" and ({"copts", "defines"} & attrs.keys())
            ):
                raise ValueError(
                    f"unsupported library compiler options or link options in {path}"
                )
            package = path.parent.relative_to(root).as_posix()
            name = ast.literal_eval(attrs["name"])
            values = {
                key: evaluate(attrs[key], compiler, require_throws)
                for key in (
                    "srcs",
                    "deps",
                    "implementation_deps",
                    "copts",
                    "defines",
                    "testonly",
                    "target_compatible_with",
                )
                if key in attrs
            }
            targets[f"//{package}:{name}"] = dict(
                values, kind=call.func.id, package=package, name=name
            )
    return targets


def normalize(label, package):
    if label.startswith(":"):
        return "//" + package + label
    if not label.startswith(("//", "@")):
        return "//" + package + ":" + label
    return label


def dependency(label):
    if label.startswith("//mbo/"):
        return "mbo::" + label.removeprefix("//mbo/").split("/")[0].split(":")[0]
    if label.startswith("@abseil-cpp//absl/"):
        package, _, name = label.removeprefix("@abseil-cpp//absl/").partition(":")
        name = name or package.split("/")[-1]
        if package == "flags":
            name = "flags" if name == "flag" else "flags_" + name
        elif package == "log" and name in ("flags", "globals", "initialize"):
            name = "log_" + name
        elif package == "algorithm" and name == "container":
            name = "algorithm_container"
        return "absl::" + name
    return {
        "@googletest//:gtest": "GTest::gmock",
        "@googletest//:gtest_main": "GTest::gmock_main",
        "@re2": "re2::re2",
        "@rules_cc//cc/runfiles": "",
        "@com_github_google_benchmark//:benchmark": "benchmark::benchmark",
        "@xxhash_reference//:xxhash": "xxhash",
    }[label]


def emit(command, name, **sections):
    lines = [f"{command}({name}"]
    for key, values in sections.items():
        if values:
            lines.append("  " + key)
            # Bracket arguments preserve paths, semicolons, dollars and quotes.
            ordered = values if key in ("COPTS", "DEFINES") else sorted(set(values))
            lines.extend("    [==[" + value + "]==]" for value in ordered)
    return "\n".join(lines) + "\n)\n"


def manifest(root, targets):
    libraries = {}
    for target in targets.values():
        if target["kind"] != "cc_library":
            continue
        if target.get("testonly") and target["name"] != "runfiles_dir_cc":
            continue
        group = target["package"].split("/")[1]
        library = libraries.setdefault(group, {"SOURCES": [], "DEPS": []})
        library["SOURCES"].extend(
            (root / target["package"] / src).as_posix()
            for src in target.get("srcs", [])
            if src.endswith(".cc")
        )
        for label in target.get("deps", []) + target.get("implementation_deps", []):
            dep = dependency(normalize(label, target["package"]))
            if dep and dep != "mbo::" + group:
                library["DEPS"].append(dep)
    # Declare every component before linking the components to each other.
    result = "".join(
        emit("mbo_add_library", name, SOURCES=attrs["SOURCES"])
        for name, attrs in sorted(libraries.items())
    )
    result += "".join(
        emit("mbo_link_library", name, DEPS=attrs["DEPS"])
        for name, attrs in sorted(libraries.items())
    )
    for target in targets.values():
        if target["kind"] not in ("cc_binary", "cc_test"):
            continue
        if "@platforms//:incompatible" in target.get("target_compatible_with", []):
            continue
        deps = set()
        pending = [(target["package"], dep) for dep in target.get("deps", [])]
        visited = set()
        while pending:
            package, dep = pending.pop()
            label = normalize(dep, package)
            if label in visited:
                continue
            visited.add(label)
            mapped = dependency(label)
            if mapped:
                deps.add(mapped)
            # Test-only/header harness dependencies also belong to the executable.
            if label in targets:
                lib = targets[label]
                pending.extend((lib["package"], item) for item in lib.get("deps", []))
        kind = "test" if target["kind"] == "cc_test" else "binary"
        name = (
            "mbo_"
            + target["package"].replace("/", "_").removeprefix("mbo_")
            + "_"
            + target["name"]
            if kind == "test"
            else "mbo_bin_" + target["name"]
        )
        result += emit(
            "mbo_add_executable",
            name,
            KIND=[kind],
            OUTPUT=[target["name"]],
            SOURCES=[
                (root / target["package"] / src).as_posix()
                for src in target.get("srcs", [])
            ],
            DEPS=list(deps),
            COPTS=target.get("copts", []),
            DEFINES=target.get("defines", []),
        )
    return result


def fail(message):
    raise ValueError(message)


def write_if_changed(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != contents:
        path.write_text(contents)


def mangle_header(root, version, seed, buckets):
    # These two repository-owned generators use Python-compatible Starlark,
    # apart from string.elems(), type(...), and struct(). Reuse their arithmetic
    # and header template so no second hash implementation or constant can drift.
    source = (root / "mbo/hash/hash.bzl").read_text()
    source = source.replace(".elems()", "").replace(
        'type(data) != "string"', "not isinstance(data, str)"
    )
    scope = {"struct": SimpleNamespace, "fail": fail}
    exec(compile(source, "hash.bzl", "exec"), scope)
    tree = ast.parse((root / "mbo/hash/internal/hash_mangle_seed.bzl").read_text())
    nodes = [
        node
        for node in tree.body
        if (
            isinstance(node, ast.Assign)
            and any(
                isinstance(t, ast.Name)
                and t.id in ("_BASE", "_SPREAD", "_MASK64", "_HEADER")
                for t in node.targets
            )
        )
        or (isinstance(node, ast.FunctionDef) and node.name == "mangle_constant")
    ]
    exec(
        compile(
            ast.Module(body=nodes, type_ignores=[]), "hash_mangle_seed.bzl", "exec"
        ),
        scope,
    )
    constant = scope["mangle_constant"](version, seed, buckets)
    return scope["_HEADER"].replace("@MANGLE_CONSTANT@", f"0x{constant:X}ULL")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", choices=("gcc", "clang"), required=True)
    parser.add_argument("--require-throws", action="store_true")
    parser.add_argument("--unroll", type=int, default=16)
    parser.add_argument("--seed", default="")
    parser.add_argument("--buckets", type=int, default=8)
    args = parser.parse_args()
    if not 4 <= args.unroll <= 32:
        parser.error("--unroll must be in [4, 32]")
    root = args.source.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    module = ast.parse((root / "MODULE.bazel").read_text())
    versions = {}
    for statement in module.body:
        if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call):
            call = statement.value
            if isinstance(call.func, ast.Name) and call.func.id in (
                "module",
                "bazel_dep",
            ):
                attrs = {kw.arg: ast.literal_eval(kw.value) for kw in call.keywords}
                versions[attrs["name"]] = attrs.get("version", "").split(".bcr.")[0]
    write_if_changed(
        output / "versions.cmake",
        "".join(
            f'set(MBO_VERSION_{name.replace("-", "_")} "{version}")\n'
            for name, version in versions.items()
        ),
    )
    write_if_changed(
        output / "targets.cmake",
        manifest(root, declarations(root, args.compiler, args.require_throws)),
    )
    config = (root / "mbo/config/internal/config.h.in").read_text()
    config = config.replace(
        "kUnrollMaxCapacityDefault = 16;", f"kUnrollMaxCapacityDefault = {args.unroll};"
    )
    config = config.replace(
        "kRequireThrows = false;",
        "kRequireThrows = " + str(args.require_throws).lower() + ";",
    )
    for relative, contents in (
        ("mbo/config/config_gen.h", config),
        (
            "mbo/hash/hash_mangle_seed_gen.h",
            mangle_header(root, versions["mboworks_mbo"], args.seed, args.buckets),
        ),
    ):
        write_if_changed(output / relative, contents)


if __name__ == "__main__":
    main()
