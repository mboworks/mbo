#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Require successful main-context CI for the exact commit about to be tagged."""

import argparse
import json
import subprocess


def latest_main_run(runs, head):
    candidates = [run for run in runs if run.get("headSha") == head and run.get("event") == "push"
                  and run.get("headBranch") == "main"]
    if not candidates:
        raise ValueError(f"no main push CI run exists for {head}")
    return max(candidates, key=lambda run: (run["createdAt"], run["databaseId"]))


def require_success(run, head):
    if run.get("headSha") != head:
        raise ValueError("CI run does not validate the exact release commit")
    if run.get("headBranch") != "main" or run.get("event") != "push":
        raise ValueError("CI run is not a main-branch push validation")
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        raise ValueError("the latest main CI run has not completed successfully")
    if not any(job.get("name") == "done" and job.get("conclusion") == "success"
               and job.get("status") == "completed" for job in run.get("jobs", [])):
        raise ValueError("the full-matrix done gate has not completed successfully")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--head", required=True)
    parser.add_argument("--repo", default="mboworks/mbo")
    args = parser.parse_args()
    listing = subprocess.check_output(
        ["gh", "run", "list", "--repo", args.repo, "--workflow", "main.yml", "--branch", "main",
         "--commit", args.head, "--event", "push", "--limit", "20", "--json",
         "databaseId,headSha,headBranch,event,createdAt"], text=True,
    )
    try:
        selected = latest_main_run(json.loads(listing), args.head)
        details = subprocess.check_output(
            ["gh", "run", "view", str(selected["databaseId"]), "--repo", args.repo,
             "--json", "headSha,headBranch,event,status,conclusion,jobs,url"], text=True,
        )
        run = json.loads(details)
        require_success(run, args.head)
    except ValueError as error:
        parser.exit(1, f"Release CI guard: {error}\n")
    print(f"Validated main CI for {args.head}: {run['url']}")


if __name__ == "__main__":
    main()
