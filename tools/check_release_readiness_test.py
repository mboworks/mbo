# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Release tags require the latest exact-SHA main CI and its aggregate gate."""

import copy
import unittest

import check_release_readiness as readiness


class ReleaseReadinessTest(unittest.TestCase):
    def setUp(self):
        self.head = "a" * 40
        self.run = {
            "headSha": self.head,
            "headBranch": "main",
            "event": "push",
            "status": "completed",
            "conclusion": "success",
            "jobs": [{"name": "done", "status": "completed", "conclusion": "success"}],
        }

    def test_latest_main_run_does_not_reuse_a_different_commit_or_pr(self):
        valid = dict(self.run, databaseId=1, createdAt="2026-11-08T12:00:00Z", event="push")
        wrong_head = dict(valid, databaseId=2, headSha="b" * 40)
        pr_run = dict(valid, databaseId=3, event="pull_request")
        branch_run = dict(valid, databaseId=4, headBranch="a-feature")
        self.assertEqual(readiness.latest_main_run([wrong_head, pr_run, branch_run, valid], self.head), valid)

    def test_latest_attempt_cannot_be_replaced_with_an_older_success(self):
        older = dict(self.run, databaseId=1, createdAt="2026-11-08T12:00:00Z", event="push")
        latest = dict(older, databaseId=2, createdAt="2026-11-08T13:00:00Z", status="in_progress")
        selected = readiness.latest_main_run([latest, older], self.head)
        with self.assertRaisesRegex(ValueError, "latest main CI run"):
            readiness.require_success(selected, self.head)

    def test_missing_main_run_is_not_ready(self):
        with self.assertRaisesRegex(ValueError, "no main push CI run"):
            readiness.latest_main_run([], self.head)

    def test_completed_exact_commit_and_aggregate_gate_are_ready(self):
        readiness.require_success(self.run, self.head)

    def test_wrong_commit_is_not_ready_even_with_a_green_gate(self):
        with self.assertRaisesRegex(ValueError, "exact release commit"):
            readiness.require_success(self.run, "b" * 40)

    def test_other_branch_or_pr_context_is_not_ready(self):
        for changes in ({"headBranch": "a-feature"}, {"event": "pull_request"}):
            with self.subTest(changes=changes):
                with self.assertRaisesRegex(ValueError, "main-branch push"):
                    readiness.require_success(dict(self.run, **changes), self.head)

    def test_unfinished_or_unsuccessful_workflow_is_not_ready(self):
        for status, conclusion in (("in_progress", ""), ("queued", ""),
                                   ("completed", "failure"), ("completed", "cancelled"),
                                   ("completed", "skipped")):
            with self.subTest(status=status, conclusion=conclusion):
                run = dict(self.run, status=status, conclusion=conclusion)
                with self.assertRaisesRegex(ValueError, "latest main CI run"):
                    readiness.require_success(run, self.head)

    def test_missing_or_unsuccessful_aggregate_gate_is_not_ready(self):
        for jobs in ([], [{"name": "a different job", "status": "completed", "conclusion": "success"}],
                     [{"name": "done", "status": "in_progress", "conclusion": ""}],
                     [{"name": "done", "status": "completed", "conclusion": "skipped"}],
                     [{"name": "done", "status": "completed", "conclusion": "failure"}]):
            with self.subTest(jobs=jobs):
                run = copy.deepcopy(self.run)
                run["jobs"] = jobs
                with self.assertRaisesRegex(ValueError, "full-matrix done gate"):
                    readiness.require_success(run, self.head)


if __name__ == "__main__":
    unittest.main()
