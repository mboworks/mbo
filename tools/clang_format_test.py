# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Exercise the pinned formatter launcher with target and exec cquery outputs."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().with_name("clang_format.sh")


class ClangFormatLauncherTest(unittest.TestCase):
    def run_launcher(self, paths, executable=None):
        with tempfile.TemporaryDirectory(prefix="mbo formatter ") as temporary:
            root = Path(temporary)
            binary = root / "bazel"
            binary.write_text(
                "#!/bin/sh\n"
                'case "$1" in\n'
                '  build) exit 0 ;;\n'
                '  cquery) printf "%s\\n" "$FORMATTER_TEST_FILES" ;;\n'
                '  info) printf "%s\\n" "$FORMATTER_TEST_ROOT" ;;\n'
                "  *) exit 2 ;;\n"
                "esac\n"
            )
            binary.chmod(0o755)
            if executable is not None:
                formatter = root / executable
                formatter.parent.mkdir(parents=True, exist_ok=True)
                formatter.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
                formatter.chmod(0o755)
            outputs = [str(root / value) if absolute else value for value, absolute in paths]
            environment = os.environ.copy()
            environment.update(
                PATH=str(root) + os.pathsep + environment["PATH"],
                FORMATTER_TEST_FILES="\n".join(outputs),
                FORMATTER_TEST_ROOT=str(root),
            )
            return subprocess.run(
                ["bash", str(SCRIPT), "-i", "source with spaces.cc"],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )

    def test_relative_target_output(self):
        result = self.run_launcher([("target/clang-format", False)], "target/clang-format")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "-i\nsource with spaces.cc\n")

    def test_absolute_output(self):
        result = self.run_launcher([("target/clang-format", True)], "target/clang-format")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "-i\nsource with spaces.cc\n")

    def test_multiple_configurations_use_available_executable(self):
        result = self.run_launcher(
            [("exec/clang-format", False), ("target/clang-format", False)], "target/clang-format"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "-i\nsource with spaces.cc\n")

    def test_multiple_available_configurations_invoke_one_formatter(self):
        result = self.run_launcher(
            [("target/clang-format", False), ("target/clang-format", True)], "target/clang-format"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "-i\nsource with spaces.cc\n")

    def test_missing_outputs_fail_clearly(self):
        for paths in ([], [("missing/clang-format", False)]):
            with self.subTest(paths=paths):
                result = self.run_launcher(paths)
                self.assertEqual(result.returncode, 1)
                self.assertIn("no executable clang-format output", result.stderr)


if __name__ == "__main__":
    unittest.main()
