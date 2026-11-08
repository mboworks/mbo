# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Release archive safety, metadata, and independent consumer regressions."""

import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import release_artifact as artifact


class ReleaseArtifactTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = {
            "MODULE.bazel": 'module(name = "mboworks_mbo", version = "0.16.0")\n'
                            'bazel_dep(name = "rules_cc", version = "0.2.28")\n',
            "VERSION.txt": "0.16.0",
            "LICENSE": "license",
            "NOTICE": "notice",
            "CMakeLists.txt": "",
            "mbo/strings/string_interner.h": "",
            "mbo/container/experimental/hamt/hamt_flat_map.h": "",
        }

    def archive(self, extra=None):
        archive = self.root / "release.tar.gz"
        with tarfile.open(archive, "w:gz") as package:
            for name, content in self.files.items():
                data = content.encode()
                member = tarfile.TarInfo(f"mbo-0.16.0/{name}")
                member.size = len(data)
                package.addfile(member, io.BytesIO(data))
            if extra:
                package.addfile(extra)
        return archive

    def test_extract_checks_release_metadata_and_public_files(self):
        source = artifact.extract(self.archive(), self.root / "extracted")
        self.assertEqual(source.name, "mbo-0.16.0")
        self.assertEqual((source / "VERSION.txt").read_text(), "0.16.0")
        self.assertTrue((source / "mbo/strings/string_interner.h").is_file())

    def test_reject_case_insensitive_standard_header_collisions(self):
        for name in ("VERSION", "version", "Version"):
            with self.subTest(name=name):
                member = tarfile.TarInfo(f"mbo-0.16.0/{name}")
                with self.assertRaisesRegex(ValueError, "standard <version>"):
                    artifact.extract(self.archive(member), self.root / "extracted")

    def test_reject_path_traversal_and_links_before_writing(self):
        for name, kind in (("mbo-0.16.0/../escape", tarfile.REGTYPE),
                           ("mbo-0.16.0/link", tarfile.SYMTYPE),
                           ("mbo-0.16.0/link", tarfile.LNKTYPE)):
            with self.subTest(name=name, kind=kind):
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = "../../escape"
                destination = self.root / "extracted"
                with self.assertRaisesRegex(ValueError, "unsafe archive member"):
                    artifact.extract(self.archive(member), destination)
                self.assertFalse(destination.exists())

    def test_reject_multiple_archive_roots(self):
        with self.assertRaisesRegex(ValueError, "one numeric-version mbo prefix"):
            artifact.extract(self.archive(tarfile.TarInfo("other/file")), self.root / "extracted")

    def test_reject_inconsistent_version_metadata(self):
        original_files = self.files.copy()
        for name, content in (("VERSION.txt", "0.15.0"),
                              ("MODULE.bazel", 'module(name = "mboworks_mbo", version = "0.15.0")')):
            with self.subTest(name=name):
                self.files = original_files.copy()
                self.files[name] = content
                with self.assertRaisesRegex(ValueError, "must agree"):
                    artifact.extract(self.archive(), self.root / name)

    def test_reject_missing_public_files(self):
        del self.files["LICENSE"]
        with self.assertRaisesRegex(ValueError, "missing LICENSE"):
            artifact.extract(self.archive(), self.root / "extracted")

    def test_reject_dev_only_bazel_package(self):
        self.files["bazelmod/BUILD.bazel"] = 'load("@hedron_compile_commands//:unused.bzl", "unused")'
        with self.assertRaisesRegex(ValueError, "dev-only Hedron"):
            artifact.extract(self.archive(), self.root / "extracted")

    def test_consumer_uses_the_archive_without_dependency_compatibility_overrides(self):
        source = artifact.extract(self.archive(), self.root / "extracted")
        consumer = self.root / "consumer"
        artifact.consumer(source, consumer)
        module = (consumer / "MODULE.bazel").read_text()
        self.assertIn('version = "0.16.0"', module)
        self.assertIn('version = "0.2.28"', module)
        self.assertIn(str(source), module)
        self.assertNotIn("single_version_override", module)
        self.assertNotIn("archive_override", module)
        self.assertNotIn("llvm", module)
        cmake = (consumer / "CMakeLists.txt").read_text()
        self.assertIn("add_subdirectory", cmake)
        self.assertNotIn("CMAKE_CXX_STANDARD", cmake)

    def test_build_runs_both_consumers_and_propagates_the_selected_bazel_version(self):
        source = artifact.extract(self.archive(), self.root / "extracted")
        with mock.patch.object(artifact, "run") as run:
            artifact.build(source, self.root / "consumer", "all", 3, "9.2.0")
        calls = run.call_args_list
        self.assertEqual(len(calls), 5)
        self.assertEqual(calls[0].kwargs["env"]["USE_BAZEL_VERSION"], "9.2.0")
        self.assertIn("--jobs=3", calls[0].args[0])
        self.assertIn("--cxxopt=-std=c++23", calls[0].args[0])
        self.assertIn("@mboworks_mbo//...", calls[1].args[0])
        self.assertEqual(calls[-1].args[0][0], "ctest")

    def test_build_failure_is_not_hidden(self):
        source = artifact.extract(self.archive(), self.root / "extracted")
        failure = subprocess.CalledProcessError(1, "bazel")
        with mock.patch.object(artifact, "run", side_effect=failure):
            with self.assertRaises(subprocess.CalledProcessError):
                artifact.build(source, self.root / "consumer", "all", 2, "9.3.2")


if __name__ == "__main__":
    unittest.main()
