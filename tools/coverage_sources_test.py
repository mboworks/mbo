#!/usr/bin/env python3
"""Tests for tools/coverage_sources.py."""

import tempfile
import unittest
from pathlib import Path

from tools import coverage_sources


class CoverageSourcesTest(unittest.TestCase):
    def test_normalizes_records_without_rewriting_source_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            source = workspace / "mbo/container/any_scan.h"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// LCOV_MERGE_FUNC_LINE\n"
                "template<typename T>\n"
                "void scan(T value) {\n"
            )
            report = (
                "SF:mbo/container/any_scan.h\n"
                "FN:2,scan_int\nFN:2,scan_long\n"
                "FNDA:0,scan_int\nFNDA:3,scan_long\n"
                "DA:2,3\nFNF:2\nFNH:1\nLF:1\nLH:1\nend_of_record\n"
            )

            actual = coverage_sources.normalized(report, workspace)

            self.assertIn("SF:mbo/container/any_scan.h", actual)
            self.assertIn("FN:2,__mbo_lcov_merged_function_at_line_1", actual)
            self.assertIn("FNDA:3,__mbo_lcov_merged_function_at_line_1", actual)
            self.assertNotIn("scan_int", actual)

            relocated = report.replace(
                "SF:mbo/container/any_scan.h",
                "SF:/remote/runner/work/mbo/mbo/mbo/container/any_scan.h",
            )
            relocated_actual = coverage_sources.normalized(relocated, workspace)
            self.assertIn("FNDA:3,__mbo_lcov_merged_function_at_line_1", relocated_actual)

    def test_groups_selected_sources_by_policy_category(self):
        policy = {
            "include": ["mbo/**"],
            "exclude": ["**/*_test.cc"],
            "categories": {
                "file": {"include": ["mbo/file/**"]},
                "hash": {"include": ["mbo/hash/**"]},
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            (workspace / "mbo/file").mkdir(parents=True)
            (workspace / "mbo/hash").mkdir(parents=True)
            (workspace / "mbo/file/file.cc").touch()
            (workspace / "mbo/hash/hash.h").touch()
            root = Path(directory) / "grouped"
            report = (
                "SF:mbo/file/file.cc\nDA:1,1\nLF:1\nend_of_record\n"
                "SF:mbo/hash/hash.h\nDA:1,1\nLF:1\nend_of_record\n"
                "SF:mbo/file/file_test.cc\nDA:1,1\nLF:1\nend_of_record\n"
                "SF:mbo/config/config.h\nFNF:0\nFNH:0\nLH:0\nLF:0\nend_of_record\n"
            )
            actual = coverage_sources.grouped(report, policy, root, workspace)
            self.assertIn(f"SF:{root.resolve()}/file/mbo/file/file.cc", actual)
            self.assertIn(f"SF:{root.resolve()}/hash/mbo/hash/hash.h", actual)
            self.assertNotIn("file_test.cc", actual)
            self.assertNotIn("config.h", actual)
            self.assertTrue((root / "file/mbo/file/file.cc").is_symlink())

    def test_rejects_sources_in_multiple_categories(self):
        policy = {
            "categories": {
                "all": {"include": ["mbo/**"]},
                "file": {"include": ["mbo/file/**"]},
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "belongs to 2 policy categories"):
                coverage_sources.grouped(
                    "SF:mbo/file/file.cc\nLF:1\nend_of_record\n",
                    policy,
                    Path(directory),
                    Path(directory),
                )

    def test_applies_exclusions_and_merges_repeated_template_records(self):
        policy = {"categories": {"file": {"include": ["mbo/file/**"]}}}
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text(
                "int excluded() {  // LCOV_EXCL_FUNC_LINE, LCOV_EXCL_LINE\n"
                "int shared() {  // LCOV_MERGE_FUNC_LINE\n"
                "return true;  // LCOV_MERGE_BR_LINE 2\n"
            )
            report = (
                "SF:mbo/file/file.h\n"
                "FN:1,excluded\nFN:2,shared_int\nFN:2,shared_long\n"
                "FNDA:0,excluded\nFNDA:3,shared_int\nFNDA:0,shared_long\nFNF:3\nFNH:1\n"
                "BRDA:3,0,0,2\nBRDA:3,0,1,0\nBRDA:3,0,2,0\nBRDA:3,0,3,4\nBRF:4\nBRH:2\n"
                "DA:1,0\nDA:2,3\nDA:3,3\nLF:3\nLH:2\nend_of_record\n"
            )

            actual = coverage_sources.grouped(
                report, policy, Path(directory) / "grouped", workspace
            )

            self.assertNotIn("excluded", actual)
            self.assertIn("FN:2,__mbo_lcov_merged_function_at_line_2", actual)
            self.assertIn("FNDA:3,__mbo_lcov_merged_function_at_line_2", actual)
            self.assertIn("FNF:1\nFNH:1", actual)
            self.assertIn("BRDA:3,0,0,2\nBRDA:3,0,1,4\nBRF:2\nBRH:2", actual)
            self.assertIn("DA:2,3\nDA:3,3\nLF:2\nLH:2", actual)

    def test_merges_function_instances_on_the_same_source_line(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text("template<typename T> void shared(T value) {}\n", encoding="utf-8")
            report = (
                "SF:mbo/file/file.h\n"
                "FN:1,shared_int\nFN:1,shared_long\n"
                "FNDA:0,shared_int\nFNDA:3,shared_long\n"
                "FNF:2\nFNH:1\nend_of_record\n"
            )

            actual = coverage_sources.normalized(report, workspace)

            self.assertIn("FN:1,__mbo_lcov_merged_function_at_line_1", actual)
            self.assertIn("FNDA:3,__mbo_lcov_merged_function_at_line_1", actual)
            self.assertIn("FNF:1\nFNH:1", actual)

    def test_excludes_explicit_ranges_from_all_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// LCOV_EXCL_START: compile-time-only implementation.\n"
                "constexpr int probe() { return 1; }\n"
                "// LCOV_EXCL_STOP\n"
                "int runtime() { return 2; }\n",
                encoding="utf-8",
            )
            report = (
                "SF:mbo/file/file.h\n"
                "FN:2,probe\nFN:4,runtime\nFNDA:0,probe\nFNDA:1,runtime\n"
                "BRDA:2,0,0,0\nBRDA:4,0,0,1\n"
                "DA:2,0\nDA:4,1\nend_of_record\n"
            )

            actual = coverage_sources.normalized(report, workspace)

            self.assertNotIn("probe", actual)
            self.assertIn("FN:4,runtime\nFNDA:1,runtime", actual)
            self.assertIn("BRDA:4,0,0,1\nBRF:1\nBRH:1", actual)
            self.assertIn("DA:4,1\nLF:1\nLH:1", actual)

    def test_rejects_unbalanced_exclusion_ranges(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text("// LCOV_EXCL_STOP\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "without LCOV_EXCL_START"):
                coverage_sources.normalized(
                    "SF:mbo/file/file.h\nDA:1,0\nend_of_record\n", workspace
                )

    def test_rejects_an_invalid_branch_merge_width(self):
        policy = {"categories": {"file": {"include": ["mbo/file/**"]}}}
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text("return true;  // LCOV_MERGE_BR_LINE 2\n")
            with self.assertRaisesRegex(ValueError, "cannot merge 3 branch records"):
                coverage_sources.grouped(
                    "SF:mbo/file/file.h\n"
                    "BRDA:1,0,0,1\nBRDA:1,0,1,0\nBRDA:1,0,2,0\n"
                    "BRF:3\nBRH:1\nend_of_record\n",
                    policy,
                    Path(directory) / "grouped",
                    workspace,
                )

    def test_selects_the_largest_compatible_branch_merge_width(self):
        policy = {"categories": {"file": {"include": ["mbo/file/**"]}}}
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text("return true;  // LCOV_MERGE_BR_LINE 4,2\n")
            report = (
                "SF:mbo/file/file.h\n"
                "BRDA:1,0,0,1\nBRDA:1,0,1,0\nBRDA:1,0,2,0\nBRDA:1,0,3,1\n"
                "BRF:4\nBRH:2\nend_of_record\n"
            )

            actual = coverage_sources.grouped(
                report, policy, Path(directory) / "grouped", workspace
            )

            self.assertIn(
                "BRDA:1,0,0,1\nBRDA:1,0,1,0\nBRDA:1,0,2,0\nBRDA:1,0,3,1\n"
                "BRF:4\nBRH:2",
                actual,
            )

    def test_applies_standalone_branch_merge_marker_to_function_declaration(self):
        policy = {"categories": {"file": {"include": ["mbo/file/**"]}}}
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            source = workspace / "mbo/file/file.h"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// LCOV_MERGE_BR_LINE 1: template instances\n"
                "template<typename T>\n"
                "bool function(T value) {\n"
            )

            actual = coverage_sources.grouped(
                "SF:mbo/file/file.h\n"
                "BRDA:2,0,0,1\nBRDA:2,0,1,0\nBRF:2\nBRH:1\nend_of_record\n",
                policy,
                Path(directory) / "grouped",
                workspace,
            )

            self.assertIn("BRDA:2,0,0,1\nBRF:1\nBRH:1", actual)


if __name__ == "__main__":
    unittest.main()
