# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Verify the experimental adapter against its Bazel source of truth."""

import ast
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import generate


ROOT = Path(__file__).resolve().parent.parent


class GenerateTest(unittest.TestCase):
    def test_every_component_and_binary_is_represented(self):
        targets = generate.declarations(ROOT, "gcc", False)
        result = generate.manifest(ROOT, targets)
        components = {target["package"].split("/")[1] for target in targets.values()}
        self.assertEqual(len(components), 14)
        for component in components:
            self.assertEqual(result.count(f"mbo_add_library({component}\n"), 1)
        for target in targets.values():
            if target["kind"] == "cc_binary":
                name = "mbo_bin_" + target["name"]
                self.assertIn(f"mbo_add_executable({name}\n", result)
        self.assertIn("mbo/testing/runfiles_dir.cc", result)

    def test_compiler_and_requirement_selects(self):
        expression = ast.parse(
            "['-fexceptions'] + select({'@rules_cc//cc/compiler:gcc': ['-Wno-terminate'], '//conditions:default': []})",
            mode="eval",
        ).body
        self.assertEqual(
            generate.evaluate(expression, "gcc", False),
            ["-fexceptions", "-Wno-terminate"],
        )
        self.assertEqual(
            generate.evaluate(expression, "clang", False), ["-fexceptions"]
        )
        targets = generate.declarations(ROOT, "gcc", True)
        self.assertEqual(
            targets["//mbo/json:json_require_exceptions_test"][
                "target_compatible_with"
            ],
            [],
        )

    def test_unsupported_expressions_fail(self):
        expression = ast.parse("unknown_macro(['input.cc'])", mode="eval").body
        with self.assertRaises(ValueError):
            generate.evaluate(expression, "gcc", False)

    def test_hash_generation_reuses_the_canonical_template(self):
        tree = ast.parse((ROOT / "mbo/hash/internal/hash_mangle_seed.bzl").read_text())
        header = next(
            ast.literal_eval(node.value)
            for node in tree.body
            if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == "_HEADER"
        )
        self.assertEqual(
            generate.mangle_header(ROOT, "0.16.0", "", 0),
            header.replace("@MANGLE_CONSTANT@", "0x0ULL"),
        )
        self.assertEqual(
            generate.mangle_header(ROOT, "0.16.0", "any seed", 1),
            header.replace("@MANGLE_CONSTANT@", "0x45828334C99AF44FULL"),
        )
        with self.assertRaises(ValueError):
            generate.mangle_header(ROOT, "0.16.0", "\n", 8)
        with self.assertRaises(ValueError):
            generate.mangle_header(ROOT, "0.16.0", "", -1)

    def test_dependency_names(self):
        self.assertEqual(
            generate.dependency("//mbo/container/experimental:frozen_map_cc"),
            "mbo::container",
        )
        self.assertEqual(
            generate.dependency("@abseil-cpp//absl/flags:parse"), "absl::flags_parse"
        )
        self.assertEqual(
            generate.dependency("@abseil-cpp//absl/log:initialize"),
            "absl::log_initialize",
        )
        with self.assertRaises(KeyError):
            generate.dependency("@unknown//:library")

    def test_configuration_options_preserve_the_bazel_template(self):
        template = (ROOT / "mbo/config/internal/config.h.in").read_text()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            command = [
                sys.executable,
                str(ROOT / "cmake/generate.py"),
                "--source",
                str(ROOT),
                "--output",
                str(output),
                "--compiler",
                "gcc",
            ]
            subprocess.run(command, check=True, capture_output=True)
            header = output / "mbo/config/config_gen.h"
            self.assertEqual(header.read_text(), template)
            subprocess.run(
                command
                + [
                    "--require-throws",
                    "--unroll=24",
                    "--seed=config test",
                    "--buckets=0",
                ],
                check=True,
                capture_output=True,
            )
            expected = template.replace(
                "kUnrollMaxCapacityDefault = 16;", "kUnrollMaxCapacityDefault = 24;"
            )
            expected = expected.replace(
                "kRequireThrows = false;", "kRequireThrows = true;"
            )
            self.assertEqual(header.read_text(), expected)
            for invalid in (3, 33):
                result = subprocess.run(
                    command + [f"--unroll={invalid}"], capture_output=True
                )
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
