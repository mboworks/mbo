# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep release compiler selection, registry templates, and setup notes usable."""

import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import unittest

import release_artifact as artifact

ROOT = Path(__file__).resolve().parents[1]


class ReleaseConfigurationTest(unittest.TestCase):
    def test_release_build_explicitly_selects_the_hermetic_compiler(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        command = re.search(r'^      bazel_test_command: "([^"]+)"$', workflow, re.MULTILINE)
        self.assertIsNotNone(command)
        arguments = shlex.split(command[1])
        self.assertEqual(arguments[:2], ["bazel", "test"])
        for argument in ("--config=clang", "//...", "--test_tag_filters=-lfs"):
            with self.subTest(argument=argument):
                self.assertIn(argument, arguments)
        configuration = (ROOT / ".bazelrc").read_text()
        self.assertIn("common:clang --extra_toolchains=@llvm_toolchain_llvm//:all", configuration)

    def test_release_coverage_matches_the_primary_llvm_module(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        coverage = workflow.split("  coverage:\n", 1)[1].split("\n  release:", 1)[0]
        module = (ROOT / "bazelmod/llvm.MODULE.bazel").read_text()
        version = re.search(r'^    llvm_version = "([^"]+)"', module, re.MULTILINE)[1]
        self.assertIn("      compiler: clang\n", coverage)
        self.assertIn(f"      llvm_version: {version}\n", coverage)

    def test_registry_linux_uses_a_supported_native_compiler(self):
        presubmit = (ROOT / ".bcr/presubmit.yml").read_text()
        linux = presubmit.split("  verify_targets_linux:\n", 1)[1].split("  verify_targets_macos:\n", 1)[0]
        self.assertIn("    platform: ubuntu2404\n", linux)
        self.assertIn("      CC: gcc-15\n", linux)
        self.assertIn("      CXX: g++-15\n", linux)
        self.assertIn("sudo add-apt-repository --yes ppa:ubuntu-toolchain-r/test", linux)
        self.assertIn("sudo apt-get update -o Acquire::Retries=3", linux)
        self.assertIn("sudo apt-get install --yes gcc-15 g++-15", linux)
        self.assertIn("    - 9.2.0\n", presubmit)
        self.assertIn("    - 9.x\n", presubmit)
        macos = presubmit.split("  verify_targets_macos:\n", 1)[1]
        self.assertIn("    platform: macos_arm64\n", macos)
        self.assertNotIn("gcc-15", macos)
        for task in (linux, macos):
            with self.subTest(task=task):
                self.assertIn('      - "--cxxopt=-std=c++23"', task)
                self.assertIn('      - "--host_cxxopt=-std=c++23"', task)
        self.assertIn("    build_targets: &build_targets\n", linux)
        self.assertIn("    build_targets: *build_targets\n", macos)

    def test_registry_templates_describe_the_uploaded_release_archive(self):
        source = json.loads((ROOT / ".bcr/source.template.json").read_text())
        version = artifact.dependency_version((ROOT / "MODULE.bazel").read_text(), "mboworks_mbo")
        parameters = dict(OWNER="mboworks", REPO="mbo", TAG=version, VERSION=version)
        self.assertEqual(source["url"].format(**parameters),
                         f"https://github.com/mboworks/mbo/releases/download/{version}/mbo-{version}.tar.gz")
        self.assertEqual(source["strip_prefix"].format(**parameters), f"mbo-{version}")
        self.assertEqual(source["integrity"], "")
        metadata = json.loads((ROOT / ".bcr/metadata.template.json").read_text())
        self.assertEqual(metadata["homepage"], "https://github.com/mboworks/mbo")
        self.assertIn("github:mboworks/mbo", metadata["repository"])
        self.assertTrue(metadata["maintainers"])
        maintainer = next(entry for entry in metadata["maintainers"] if entry.get("github") == "helly25")
        self.assertEqual(maintainer["github_user_id"], 6420169)


class ReleasePreparationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix="mbo-release-configuration-")
        cls.addClassCleanup(temporary.cleanup)
        cls.checkout = Path(temporary.name)
        # Exercise the real packager with current source files, without changing
        # the contributor's checkout, index, commits, tags, or remotes.
        files = (
            "MODULE.bazel", "CHANGELOG.md", "mbo/mope/mope.bzl", "tools/header.txt",
            "tools/release_notes.sh", ".github/release-notes.md.template",
            ".github/workflows/release_prep.sh", ".github/workflows/bazelmod.patch",
        )
        for name in files:
            destination = cls.checkout / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, destination)
        for arguments in (
                ["git", "init", "--quiet"],
                ["git", "add", "--all"],
                ["git", "-c", "user.name=Release test", "-c", "user.email=release-test@example.invalid",
                 "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null",
                 "commit", "--quiet", "-m", "Release fixture"]):
            subprocess.run(arguments, cwd=cls.checkout, check=True, capture_output=True, text=True)
        cls.version = artifact.dependency_version((cls.checkout / "MODULE.bazel").read_text(), "mboworks_mbo")
        result = subprocess.run(
            ["bash", ".github/workflows/release_prep.sh"], cwd=cls.checkout,
            env=dict(os.environ, GITHUB_REF_NAME=cls.version), check=True, capture_output=True, text=True,
        )
        cls.notes = result.stdout

    def test_packaged_module_is_not_the_development_module(self):
        source = json.loads((ROOT / ".bcr/source.template.json").read_text())
        parameters = dict(OWNER="mboworks", REPO="mbo", TAG=self.version, VERSION=self.version)
        prefix = source["strip_prefix"].format(**parameters)
        archive = self.checkout / source["url"].format(**parameters).rsplit("/", 1)[1]
        with tarfile.open(archive) as package:
            self.assertEqual({Path(name).parts[0] for name in package.getnames()}, {prefix})
            module = package.extractfile(f"{prefix}/MODULE.bazel").read().decode()
            self.assertEqual(artifact.dependency_version(module, "mboworks_mbo"), self.version)
            self.assertNotRegex(module, r"(?m)^include\(")
            self.assertEqual(package.extractfile(f"{prefix}/VERSION.txt").read().decode(), self.version)
            mope = package.extractfile(f"{prefix}/mbo/mope/mope.bzl").read().decode()
            self.assertIn('_CLANG_FORMAT_BINARY = "clang-format-auto"', mope)

    def test_generated_module_instructions_select_a_toolchain_and_pin_links(self):
        self.assertIn("## For Bazel MODULE.bazel\n", self.notes)
        self.assertNotIn("MODULES.bazel", self.notes)
        self.assertNotIn("/blob/main/", self.notes)
        self.assertIn(f'/blob/{self.version}/bazelmod/llvm.MODULE.bazel', self.notes)
        self.assertIn('include("//:llvm.MODULE.bazel")\n'
                      'register_toolchains("@llvm_toolchain_llvm//:all")', self.notes)
        self.assertIn("--extra_toolchains=@llvm_toolchain_llvm//:all", self.notes)
        self.assertIn("--cxxopt=-std=c++23 --host_cxxopt=-std=c++23", self.notes)
        self.assertNotIn('include("//:dev.MODULE.bazel")', self.notes)
        self.assertIn("not a standalone module", self.notes)


if __name__ == "__main__":
    unittest.main()
