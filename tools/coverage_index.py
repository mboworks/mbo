#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Generate per-run and retained-site HTML coverage indexes."""

from __future__ import annotations

import argparse
import datetime
import html
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import coverage_policy

_METRICS = ("lines", "branches", "functions")


def _page(title: str, body: str) -> str:
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{html.escape(title)}</title>
    <style>
      body {{ font: 16px/1.5 system-ui, sans-serif; margin: 2rem auto; max-width: 96rem; padding: 0 1rem; }}
      a {{ color: #0969da; }}
      table {{ border-collapse: collapse; margin: 1rem 0 2rem; }}
      th, td {{ border: 1px solid #d0d7de; padding: .35rem .65rem; text-align: right; }}
      .reportTitle {{ text-align: center; }}
      .coverageTable {{ border-collapse: separate; border-spacing: 1px; margin-left: auto; margin-right: auto; }}
      .coverageTable th, .coverageTable td {{ border: 0; }}
      .coverageTable th {{ background: #6688d4; color: #fff; }}
      .coverageTable thead tr:first-child th {{ font-size: 120%; text-align: center; }}
      .coverageTable th:first-child, .coverageTable td:first-child,
      .reportsTable th:nth-child(-n+6), .reportsTable td:nth-child(-n+6) {{ text-align: left; }}
      .coverageTable td:first-child {{ background: #dae7fe; color: #284fa8; }}
      .coverageTable td {{ white-space: nowrap; }}
      .coverageTable td:nth-child(n+3), .patchTable td:nth-child(n+2), .reportsTable td:nth-child(n+7) {{ font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-variant-numeric: tabular-nums; }}
      .low {{ background: #f8cecc; }}
      .medium {{ background: #fff2cc; }}
      .high {{ background: #d5e8d4; }}
      .coverageTable .policyGap {{ background: #fff; min-width: .5rem; padding: 0; }}
      .coverageTable .policyCell {{ background: #dae7fe; font: 11px/1.25 ui-monospace, SFMono-Regular, Consolas, monospace; text-align: center; }}
      .coverageTable .policy-stricter {{ background: #a7fc9d; }}
      .coverageTable .policy-weaker {{ background: #ffea20; }}
      .coverageTable .policy-mixed {{ background: #ffd580; }}
      .policyNote {{ font-size: 12px; margin: -1.5rem auto 2rem; max-width: 72rem; text-align: left; }}
      .status-good {{ background: #d5e8d4; }}
      .status-ok {{ background: #fff2cc; }}
      .status-bad {{ background: #f8cecc; color: #cf222e; font-weight: bold; }}
    </style>
  </head>
  <body>
{body}
  </body>
</html>
"""


def _percent(value: dict) -> str:
    return "n/a" if value["percent"] is None else f'{value["percent"]:.2f}%'


def _policy(summary: dict, category: str) -> dict[str, coverage_policy.MetricPolicy]:
    return {
        metric: coverage_policy.MetricPolicy(
            summary["minimums"][category][metric],
            summary["targets"][category][metric],
            summary["enforcement"][category][metric],
        )
        for metric in _METRICS
        if metric in summary["minimums"].get(category, {})
    }


def _status(metrics: dict, policy: dict[str, coverage_policy.MetricPolicy]) -> str:
    names = tuple(name for name in _METRICS if name in policy)
    failures = [name for name in names if not coverage_policy.passes(metrics[name]["percent"], policy[name])]
    if failures:
        return "BAD: " + "/".join(name[0].upper() for name in failures)
    if all(coverage_policy.rating(metrics[name]["percent"], policy[name]) == "high" for name in names):
        return "GOOD"
    return "OK"


def _status_class(status: str) -> str:
    if status == "GOOD":
        return "status-good"
    if status == "OK":
        return "status-ok"
    return "status-bad"


def _full_table(summary: dict) -> str:
    rows = []
    overall = _policy(summary, "overall")
    reasons = summary.get("reasons", {})
    for category, metrics in summary["measurements"].items():
        policy = _policy(summary, category)
        status = _status(metrics, policy)
        reason = reasons.get(category, "")
        reason_attr = f' title="{html.escape(reason, quote=True)}"' if reason else ""
        cells: list[tuple[str, str | None]] = [
            (html.escape(category), reason_attr),
            (html.escape(status), f'class="{_status_class(status)}"'),
        ]
        for metric in _METRICS:
            if metric not in metrics:
                cells.extend((('n/a', 'class="na"'),) * 3)
                continue
            value = metrics[metric]
            if metric not in policy:
                cells.extend((("n/a", 'class="na"'), (str(value["covered"]), 'class="na"'), (str(value["total"]), 'class="na"')))
                continue
            rating = coverage_policy.rating(value["percent"], policy[metric])
            metric_policy = policy[metric]
            title = (
                f"{rating}; enforce {metric_policy.enforce}; medium at {metric_policy.minimum:g}%; "
                f"high at {metric_policy.target:g}%"
            )
            attrs = f'class="{rating}" title="{html.escape(title, quote=True)}"'
            rate = _percent(value)
            cells.extend(
                (
                    (rate, attrs),
                    (str(value["covered"]), attrs),
                    (str(value["total"]), attrs),
                )
            )
        row = f"      <tr><td{cells[0][1] or ''}>{cells[0][0]}</td><td {cells[1][1]}>{cells[1][0]}</td>"
        row += "".join(
            f'<td{f" {attrs}" if attrs else ""}>{cell}</td>' for cell, attrs in cells[2:]
        )
        row += '<td class="policyGap"></td>'
        for metric in _METRICS:
            if metric not in policy:
                row += '<td class="policyCell">n/a</td>'
                continue
            value = policy[metric]
            if category == "overall":
                label = _compact_policy(value)
                css = "policyCell"
            elif value == overall[metric]:
                label = "default"
                css = "policyCell"
            else:
                direction = _policy_direction(value, overall[metric])
                word = {
                    "policy-weaker": "lower",
                    "policy-stricter": "higher",
                    "policy-mixed": "mixed",
                }[direction]
                label = f"{word}<br>{_compact_policy(value)}"
                css = f"policyCell {direction}"
            row += f'<td class="{css}">{label}</td>'
        row += "</tr>"
        rows.append(row)
    return """    <table class="coverageTable">
      <thead>
        <tr><th rowspan="2">Category</th><th rowspan="2">Status</th><th colspan="3">Lines</th><th colspan="3">Branches</th><th colspan="3">Functions</th><th class="policyGap" rowspan="2"></th><th colspan="3">Policy vs default<sup>*</sup></th></tr>
        <tr><th>Rate</th><th>Covered</th><th>Total</th><th>Rate</th><th>Covered</th><th>Total</th><th>Rate</th><th>Covered</th><th>Total</th><th>Lines</th><th>Branches</th><th>Functions</th></tr>
      </thead>
      <tbody>
""" + "\n".join(rows) + """
      </tbody>
    </table>
    <p class="policyNote"><sup>*</sup> Policy values are <code>medium/high&middot;enforced-band</code>.
    For example, <code>90/92&middot;M</code> means medium starts at 90%, high starts at 92%, and medium is
    enforced. Lower, higher, and mixed compare policy strictness, including the enforced band, not
    measured coverage.</p>"""


def _compact_policy(policy: coverage_policy.MetricPolicy) -> str:
    boundaries = f"{policy.minimum:g}" if policy.minimum == policy.target else f"{policy.minimum:g}/{policy.target:g}"
    return f"{boundaries}&middot;{policy.enforce[0].upper()}"


def _policy_direction(
    value: coverage_policy.MetricPolicy, inherited: coverage_policy.MetricPolicy
) -> str:
    enforcement = {"medium": 0, "high": 1}
    weaker = (
        value.minimum < inherited.minimum
        or value.target < inherited.target
        or enforcement[value.enforce] < enforcement[inherited.enforce]
    )
    stricter = (
        value.minimum > inherited.minimum
        or value.target > inherited.target
        or enforcement[value.enforce] > enforcement[inherited.enforce]
    )
    if weaker and stricter:
        return "policy-mixed"
    return "policy-weaker" if weaker else "policy-stricter"


def render_report(summary: dict, target: str) -> str:
    """Returns the landing page for one retained report."""
    overview = "../" * len(target.split("/"))
    body = f'    <h1 class="reportTitle">mbo coverage: {html.escape(target)}</h1>\n{_full_table(summary)}\n'
    if "patch" in summary:
        patch = summary["patch"]
        serialized = summary["patch_policy"]
        patch_policy = {
            metric: coverage_policy.MetricPolicy(
                serialized["minimum"][metric],
                serialized["target"][metric],
                serialized["enforce"][metric],
            )
            for metric in ("lines", "branches")
        }
        patch_status = _status(patch, patch_policy)
        values = []
        for metric in ("lines", "branches"):
            rating = coverage_policy.rating(patch[metric]["percent"], patch_policy[metric])
            values.append(f'<td class="{rating}">{_percent(patch[metric])}</td>')
        status_class = _status_class(patch_status)
        body += (
            "    <h2>Changed coverable lines</h2>\n"
            "    <table class=\"patchTable\"><thead><tr><th>Status</th><th>Lines</th><th>Branches</th></tr></thead><tbody><tr>"
            f'<td class="{status_class}">{patch_status}</td>{"".join(values)}</tr></tbody></table>\n'
        )
    body += (
        '    <p><a href="lcov/">Browse detailed LCOV source coverage</a> · '
        '<a href="coverage-summary.json">Coverage data (JSON)</a> · '
        '<a href="coverage-meta.json">Report metadata (JSON)</a> · '
        f'<a href="{overview}">All reports</a></p>'
    )
    return _page(f"mbo coverage: {target}", body)


def report_metadata(
    summary: dict,
    target: str,
    created_at: str,
    started_at: str,
    completed_at: str,
    run_id: int,
    run_attempt: int,
    head_sha: str,
) -> dict:
    """Returns the retained identity and overview data for one report."""
    return {
        "schema": 1,
        "target": target,
        "source": {
            "created_at": created_at,
            "started_at": started_at,
            "completed_at": completed_at,
            "run_id": run_id,
            "run_attempt": run_attempt,
            "head_sha": head_sha,
        },
        "coverage": summary["measurements"]["overall"],
    }


def is_newer(candidate: dict, current: dict) -> bool:
    """Whether candidate may replace current, including an idempotent replay."""
    def key(value: dict) -> tuple[str, int, int]:
        source = value["source"]
        return (source["created_at"], source["run_id"], source["run_attempt"])

    return key(candidate) >= key(current)


def latest_metadata(values: list[dict]) -> dict[str, dict]:
    """Returns the newest metadata record for every target."""
    result = {}
    for value in values:
        target = value["target"]
        if target not in result or is_newer(value, result[target]):
            result[target] = value
    return result


def _short_row(metadata: dict, report_path: str | None = None) -> str:
    target = metadata["target"]
    if target == "main":
        label = "main"
        source = '<a href="https://github.com/mboworks/mbo/tree/main">main branch</a>'
    elif target.startswith("tag/"):
        release = target.removeprefix("tag/")
        label = f"release {release}"
        source = (
            f'<a href="https://github.com/mboworks/mbo/releases/tag/{html.escape(release)}">'
            f"release {html.escape(release)}</a>"
        )
    else:
        number = target.removeprefix("pr/")
        label = f"PR {number}"
        source = f'<a href="https://github.com/mboworks/mbo/pull/{html.escape(number)}">PR #{html.escape(number)}</a>'
    source_metadata = metadata["source"]
    run_id = source_metadata["run_id"]
    if run_id == 0:  # Reports retained before metadata was introduced.
        timestamp = commit = run = "n/a"
    else:
        completed = datetime.datetime.fromisoformat(source_metadata["completed_at"].replace("Z", "+00:00"))
        timestamp = completed.astimezone(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        sha = source_metadata["head_sha"]
        commit = (
            f'<a href="https://github.com/mboworks/mbo/commit/{html.escape(sha)}">'
            f"<code>{html.escape(sha[:7])}</code></a>"
        )
        attempt = source_metadata["run_attempt"]
        run = f'<a href="https://github.com/mboworks/mbo/actions/runs/{run_id}">run {run_id}</a>'
        if attempt > 1:
            run += f" (attempt {attempt})"
    values = [_percent(metadata["coverage"][metric]) for metric in _METRICS]
    report_path = html.escape(target if report_path is None else report_path)
    report = f'<a href="{report_path}/">{html.escape(label)}</a>'
    data = f'<a href="{report_path}/coverage-summary.json">JSON</a>'
    details = (report, data, source, timestamp, commit, run)
    return "        <tr>" + "".join(f"<td>{value}</td>" for value in (*details, *values)) + "</tr>"


def _metadata_paths(root: Path) -> list[Path]:
    sources = list((root / "main").glob("coverage-meta.json"))
    sources.extend((root / "tag").glob("*/coverage-meta.json"))
    sources.extend((root / "pr").glob("*/coverage-meta.json"))
    return sources


def archive_reports(root: Path, incoming: Path | None = None) -> None:
    """Retain each published run/attempt once, including its detailed source pages."""
    metadata_paths = _metadata_paths(root)
    if incoming is not None:
        metadata_paths.append(incoming / "coverage-meta.json")
    for metadata_path in metadata_paths:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        source = metadata["source"]
        run, attempt = int(source["run_id"]), int(source["run_attempt"])
        if run <= 0 or attempt <= 0:
            continue  # Legacy reports have no authentic run identity to archive.
        destination = root / "runs" / str(run) / str(attempt)
        if destination.exists():
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".archive-", dir=destination.parent) as temporary:
            candidate = Path(temporary) / "report"
            shutil.copytree(metadata_path.parent, candidate)
            # Archive URLs are deeper than main/pr/tag URLs. Keep report-local links
            # intact and relocate only the generated links back to the overview.
            for page in candidate.rglob("*.html"):
                text = page.read_text(encoding="utf-8")
                depth = len(destination.relative_to(root).parts) + len(page.relative_to(candidate).parent.parts)
                relocated = re.sub(
                    r'(<a href=")(?:\.\./)+(index\.html)?(">All (?:coverage )?reports</a>)',
                    lambda match: match[1] + "../" * depth + (match[2] or "") + match[3], text,
                )
                if relocated != text:
                    page.write_text(relocated, encoding="utf-8")
            candidate.rename(destination)
    (root / "runs").mkdir(parents=True, exist_ok=True)
    (root / "runs/index.html").write_text(render_run_history(root), encoding="utf-8")


def render_run_history(root: Path) -> str:
    """Render all archived runs, without collapsing attempts or PR identities."""
    reports = []
    for path in (root / "runs").glob("*/*/coverage-meta.json"):
        metadata = json.loads(path.read_text(encoding="utf-8"))
        reports.append((metadata, path.parent.relative_to(root / "runs").as_posix()))
    reports.sort(key=lambda item: (
        item[0]["source"]["created_at"], item[0]["source"]["run_id"], item[0]["source"]["run_attempt"]
    ), reverse=True)
    body = (
        '    <h1>mbo coverage run history</h1>\n'
        '    <p><a href="../index.html">Current coverage overview</a></p>\n'
        '    <p>Immutable snapshots of published reports, including detailed source coverage, '
        'identified by their original CI run and attempt. Newest runs appear first.</p>\n'
    )
    if reports:
        headings = ("Report", "Data", "Source", "Completed", "Commit", "Workflow", "Lines", "Branches", "Functions")
        body += '    <table class="reportsTable"><thead><tr>'
        body += "".join(f"<th>{heading}</th>" for heading in headings)
        body += "</tr></thead><tbody>\n"
        body += "\n".join(_short_row(metadata, path) for metadata, path in reports)
        body += "\n    </tbody></table>\n"
    else:
        body += "    <p>No archived coverage runs are available.</p>\n"
    return _page("mbo coverage run history", body)


def _integration_position(repository: Path, commit: str, commits: list[str]) -> int | None:
    """Find the first main commit containing a merge made on an integration branch."""
    def contains(index: int) -> bool:
        return subprocess.run(
            ["git", "-C", str(repository), "merge-base", "--is-ancestor", commit, commits[index]],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
        ).returncode == 0

    if not commits or not contains(len(commits) - 1):
        return None
    first, last = 0, len(commits) - 1
    while first < last:
        middle = (first + last) // 2
        if contains(middle):
            last = middle
        else:
            first = middle + 1
    return first


def _squashed_integration_position(repository: Path, pull: dict, merges: dict,
                                   positions: dict, fetch_heads: bool) -> int | None:
    """Verify membership in an aggregation whose final merge was squashed."""
    base = pull.get("base", {})
    for parent in merges.values():
        head = parent.get("head", {})
        position = positions.get(parent["merge_commit_sha"])
        if (position is None or not base.get("ref") or base["ref"] != head.get("ref")
                or base.get("repo", {}).get("full_name") != head.get("repo", {}).get("full_name")
                or pull["merged_at"] > parent["merged_at"]):
            continue
        sha = head.get("sha", "")
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            continue
        present = subprocess.run(
            ["git", "-C", str(repository), "cat-file", "-e", f"{sha}^{{commit}}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
        ).returncode == 0
        if not present and fetch_heads:
            subprocess.run(["git", "-C", str(repository), "fetch", "--no-tags", "origin", sha], check=True)
        if _integration_position(repository, pull["merge_commit_sha"], [sha]) is not None:
            return position
    return None


def _tag_reference_time(repository: Path, tag: str) -> str | None:
    """Tagger time for annotated tags; commit time for lightweight tags."""
    result = subprocess.run(
        ["git", "-C", str(repository), "for-each-ref", "--format=%(creatordate:unix)",
         f"refs/tags/{tag}"], capture_output=True, text=True, check=False,
    )
    value = result.stdout.strip()
    if result.returncode or not value.isdecimal():
        return None
    return datetime.datetime.fromtimestamp(int(value), datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def update_history(root: Path, repository: Path, pull_requests: list[dict],
                   fetch_heads: bool = False) -> None:
    """Attach reports to main chronology, including merges through aggregation PRs."""
    commits = subprocess.check_output(
        ["git", "-C", str(repository), "rev-list", "--first-parent", "--reverse", "HEAD"],
        text=True,
    ).splitlines()
    positions = {sha: index for index, sha in enumerate(commits)}
    pulls_by_target = {f"pr/{pull['number']}": pull for pull in pull_requests}
    merges = {
        f"pr/{pull['number']}": pull
        for pull in pull_requests
        if pull.get("merged_at")
    }
    for path in _metadata_paths(root):
        metadata = json.loads(path.read_text(encoding="utf-8"))
        target = metadata["target"]
        current_pull = pulls_by_target.get(target, {})
        metadata["pull_state"] = ("merged" if current_pull.get("merged_at")
                                  else current_pull.get("state", "unknown")) if target.startswith("pr/") else None
        pull = merges.get(target)
        metadata["reference_time"] = pull["merged_at"] if pull else None
        sha = pull["merge_commit_sha"] if pull else None
        if re.fullmatch(r"tag/\d+\.\d+\.\d+", target):
            tag = target.removeprefix("tag/")
            metadata["reference_time"] = _tag_reference_time(repository, tag)
            resolved = subprocess.run(
                ["git", "-C", str(repository), "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}"],
                capture_output=True,
                text=True,
                check=False,
            )
            sha = resolved.stdout.strip() if resolved.returncode == 0 else None
        history = {"commit": sha, "position": positions[sha]} if sha in positions else None
        if sha and history is None:
            position = _integration_position(repository, sha, commits)
            if position is None and pull:
                position = _squashed_integration_position(repository, pull, merges, positions, fetch_heads)
            if position is not None:
                history = {
                    "commit": sha,
                    "position": position,
                    "integration_commit": commits[position],
                    "merged_at": pull["merged_at"] if pull else "",
                }
        metadata["history"] = history
        path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def _report_order(metadata: dict) -> tuple:
    target = metadata["target"]
    reference_time = metadata.get("reference_time")
    source = metadata["source"]
    return (
        target == "main",
        reference_time is not None,
        reference_time or source["created_at"],
        source["run_id"] if reference_time is None else 0,
        target,
    )


def render_site(root: Path) -> str:
    """Returns the overview, main first then newest PR merge/tag timestamp first."""
    metadata = latest_metadata(
        [json.loads(source.read_text(encoding="utf-8")) for source in _metadata_paths(root)]
    )
    reports = sorted(
        (report for report in metadata.values() if report.get("pull_state") != "closed"),
        key=_report_order, reverse=True,
    )
    rows = "\n".join(_short_row(metadata) for metadata in reports)
    body = (
        "    <h1>mbo coverage reports</h1>\n"
        '    <p><a href="runs/">All retained coverage runs and attempts</a></p>\n'
        "    <p>Main first, then PRs by actual merge time and releases by tag creation time, newest first. "
        "Lightweight tags have no creation timestamp, so their tagged commit time is used. "
        "Aggregated PRs retain their own reports and individual merge times. "
        "Open PRs and reports without a reference timestamp follow, newest CI run first. "
        "PRs closed without merging are omitted here; their reports remain in run history.</p>\n"
    )
    if rows:
        body += """    <table class="reportsTable"><thead><tr><th>Report</th><th>Data</th><th>Source</th><th>Completed</th><th>Commit</th><th>Workflow</th><th>Lines</th><th>Branches</th><th>Functions</th></tr></thead>
      <tbody>
""" + rows + """
      </tbody>
    </table>
"""
    if not reports:
        body += "    <p>No coverage reports are available.</p>\n"
    return _page("mbo coverage reports", body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    report = subparsers.add_parser("report")
    report.add_argument("summary", type=Path)
    report.add_argument("target")
    report.add_argument("output", type=Path)
    site = subparsers.add_parser("site")
    site.add_argument("root", type=Path)
    site.add_argument("output", type=Path)
    metadata = subparsers.add_parser("metadata")
    metadata.add_argument("summary", type=Path)
    metadata.add_argument("target")
    metadata.add_argument("output", type=Path)
    metadata.add_argument("--created-at", required=True)
    metadata.add_argument("--started-at", required=True)
    metadata.add_argument("--completed-at", required=True)
    metadata.add_argument("--head-sha", required=True)
    metadata.add_argument("--run-attempt", required=True, type=int)
    metadata.add_argument("--run-id", required=True, type=int)
    history = subparsers.add_parser("history")
    history.add_argument("root", type=Path)
    history.add_argument("repository", type=Path)
    history.add_argument("pull_requests", type=Path)
    history.add_argument("--fetch-aggregation-heads", action="store_true")
    archive = subparsers.add_parser("archive")
    archive.add_argument("root", type=Path)
    archive.add_argument("--incoming", type=Path)
    newer = subparsers.add_parser("newer")
    newer.add_argument("candidate", type=Path)
    newer.add_argument("current", type=Path)
    args = parser.parse_args()
    if args.command == "report":
        args.output.write_text(render_report(json.loads(args.summary.read_text(encoding="utf-8")), args.target), encoding="utf-8")
    elif args.command == "site":
        args.output.write_text(render_site(args.root), encoding="utf-8")
    elif args.command == "archive":
        archive_reports(args.root, args.incoming)
    elif args.command == "history":
        pages = json.loads(args.pull_requests.read_text(encoding="utf-8"))
        update_history(args.root, args.repository, [pull for page in pages for pull in page],
                       args.fetch_aggregation_heads)
    elif args.command == "metadata":
        summary = json.loads(args.summary.read_text(encoding="utf-8"))
        value = report_metadata(
            summary,
            args.target,
            args.created_at,
            args.started_at,
            args.completed_at,
            args.run_id,
            args.run_attempt,
            args.head_sha,
        )
        args.output.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    else:
        candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
        current = json.loads(args.current.read_text(encoding="utf-8"))
        return 0 if is_newer(candidate, current) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
