#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Execute the reusable runner's compiler selection and probe checks."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest

WORKFLOW = (
    Path(__file__).resolve().parents[1] / ".github/workflows/test.yml"
).read_text()


def step_script(name):
    step = WORKFLOW.split(f"      - name: {name}\n", 1)[1].split("\n      - ", 1)[0]
    return textwrap.dedent(step.split("        run: |\n", 1)[1])


class UbuntuClangTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / ".bazelversion").write_text("9.2.0\n")
        self.environment_file = self.root / "environment"
        self.arguments = self.root / "arguments.json"
        executable = self.root / "bazel"
        executable.write_text(
            f"#!{sys.executable}\n"
            "import json, os, pathlib, sys\n"
            "if sys.argv[1:] == ['--version']:\n"
            "    print('bazel 9.2.0')\n"
            "else:\n"
            "    pathlib.Path(os.environ['ARGUMENTS']).write_text(json.dumps(sys.argv[1:]))\n"
            "    print(os.environ['COMPILER_REPORT'])\n"
        )
        executable.chmod(0o755)
        self.environment = dict(
            os.environ,
            PATH=f"{self.root}{os.pathsep}{os.environ['PATH']}",
            GITHUB_ENV=str(self.environment_file),
            ARGUMENTS=str(self.arguments),
        )

    def run_step(self, name, compiler="clang-system", version="22", **environment):
        inputs = dict(
            compiler=compiler,
            llvm_version=version,
            gcc_version="",
            bazel_config="opt",
            bazel_version="",
        )
        script = re.sub(
            r"\$\{\{inputs\.(\w+)\}\}",
            lambda match: inputs[match[1]],
            step_script(name),
        )
        return subprocess.run(
            ["bash", "-eo", "pipefail", "-c", script],
            cwd=self.root,
            env=dict(self.environment, **environment),
            capture_output=True,
            text=True,
            check=False,
        )

    def select_compiler(self):
        result = self.run_step("Setup")
        self.assertEqual(result.returncode, 0, result.stderr)
        for line in self.environment_file.read_text().splitlines():
            key, value = line.split("=", 1)
            self.environment[key] = value

    def test_distribution_compiler_is_used_without_hermetic_registration(self):
        self.select_compiler()
        self.assertEqual(self.environment["CC"], "/usr/bin/clang-22")
        self.assertEqual(self.environment["CXX"], "/usr/bin/clang++-22")
        result = self.run_step(
            "Show compiler",
            COMPILER_REPORT="Compiler: Clang\nCompiler version: 22.1.8\n",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(self.arguments.read_text())
        self.assertIn("--repo_env=CC=/usr/bin/clang-22", arguments)
        self.assertIn("--repo_env=CXX=/usr/bin/clang++-22", arguments)
        self.assertIn("--//mbo/mope:clang_format=/usr/bin/clang-format-22", arguments)
        self.assertNotIn("--config=clang", arguments)
        self.assertIn("opt", arguments)

    def test_compiler_probe_rejects_gcc_fallback(self):
        self.select_compiler()
        result = self.run_step(
            "Show compiler", COMPILER_REPORT="Compiler: GCC\nCompiler version: 15.2.0\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected compiler prefix", result.stdout)

    def test_compiler_probe_rejects_wrong_clang_major(self):
        self.select_compiler()
        result = self.run_step(
            "Show compiler",
            COMPILER_REPORT="Compiler: Clang\nCompiler version: 21.1.8\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected distribution Clang major version 22", result.stdout)

    def test_existing_hermetic_mode_still_registers_its_toolchain(self):
        result = self.run_step("Setup", compiler="clang", version="")
        self.assertEqual(result.returncode, 0, result.stderr)
        environment = self.environment_file.read_text()
        self.assertIn("--config=clang", environment)
        self.assertNotIn("--repo_env=CC=", environment)


if __name__ == "__main__":
    unittest.main()
