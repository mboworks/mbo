# Infrastructure and publishing

This applies the lessons from [proto PR 100](https://github.com/mboworks/proto/pull/100)
and xff PRs 835–848, excluding 841. Run `bazel test //...`,
`python3 -m unittest discover -s tools -p '*_test.py'`, and `pre-commit run --all-files`.

## Local clang-tidy

Build with `bazel build --config=clang-tidy //...`, then run
`./compile_commands-update.sh` before `pre-commit run clang-tidy --all-files`.
The enforcing hook is report-only. Missing prerequisites, stale compilation databases,
parse errors, and findings fail the check.

`require_serial: true` gives one coordinator ownership of all workers; pre-commit filename
batching cannot multiply worker pools. The default is `max(1, min(2, CPUs - 1))`, further
limited by selected translation units. `CLANG_TIDY_JOBS=1` reduces contention; a positive
integer overrides the default, while `auto` selects it. CI explicitly uses its runner CPU count.
These are worker limits, not operating-system CPU or memory quotas. Independently launched
commands do not share a global limit. Interruptions terminate active children and prevent queued
work from starting. Source-only changes remain focused, while header/build changes retain
mbo's conservative full-sweep behavior and SMHasher exclusions.

## Build caches and CI

Cache only Bazel compiled outputs. Keys separate OS, architecture, compiler/version, Bazel
version, build configuration, dependency hash, run, and attempt. Only main saves generations;
PRs and releases restore main caches. Legacy main prefixes provide a migration fallback.
Do not upload extracted LLVM distributions or downloaded module repositories.

Before upload, stop the correct Bazel server and synchronously trim largest files first,
oldest first at equal size. The starting limit is 600,000,000 uncompressed bytes and a compressed
entry must be strictly below 700,000,000 bytes. These conservative initial budgets replace the
old policy that discarded an entire upload above 500 MiB. They are not measured optimal sizes
for mbo. A read-only inventory on 2026-09-17 found 108 entries totaling 7,933,025,070
bytes; the largest main build cache was macOS ASan at 344,079,951 compressed bytes.
Compressed sizes do not establish uncompressed payload needs. Use the one-day cache measurement artifacts, final job inventory, and Bazel disk-cache
hit statistics to decide whether configuration-specific budgets should change. The inventory
uses 10 GB as a planning budget, not a claim about account-specific limits.

Retire old main generations only after the exact usable replacement appears in the inventory.
Missing, empty, oversized, equal-age, or newer generations do not authorize retirement.
Trusted main-only maintenance removes closed-PR/tag caches, superseded generations, and
oversized entries. A cache API failure defers immediate retirement.

Coverage starts alongside lint. Every existing matrix job remains in the final required gate.
The existing GCC, Clang, macOS, sanitizer, and Bazel compatibility matrix stays intact.

## Preparation profiles

The reusable coverage runner serves main, PRs, and releases. Its primary and exception-policy
coverage invocations write separate `coverage-preparation.json.gz` and
`coverage-exceptions-preparation.json.gz` traces. Later `bazel info` calls do not overwrite them.
The `coverage-preparation-profile` artifact retains both for seven days, even after failure.
Missing traces warn without hiding the underlying coverage failure.

The traces include repository fetches, Starlark repository functions, Starlark builtins, and
fetch events. Compare their durations with elapsed time, action critical path, executed actions,
and cache hits before deciding that downloading, extraction, analysis, or compilation dominates.
xff's timing improvements are evidence for measuring these phases, not predicted mbo speedups.

## Coverage and publishing

The trusted coverage publisher archives every identified report and attempt before replacing a
target URL, including late reports that cannot replace a newer latest report. The immutable
run-history index retains detailed LCOV pages and original run metadata. Main stays first; merged
PRs and releases follow by actual merge/tag timestamps, while open and unknown reports follow by
workflow creation time. Closed-unmerged PRs are omitted from the overview while direct reports
remain. Aggregation ancestry is provenance and is verified for squashed parents without changing
measured identities. Replacement freshness is independent: an older slow run cannot overwrite a
newer report.

Both full-site publishers add shared MBO Works favicons to the staged deployment copy only.
Retained release snapshots remain unchanged. The README uses the shared logo at 64 by 64 pixels.
Release tags remain numeric semantic versions and existing immutable release/BCR flows remain
in place. No dependency or toolchain pins change as part of this infrastructure adaptation.

## Contributor rules

`AGENTS.md` describes contributor obligations; `GIT_RULES.md` owns branch and PR operations.
`STYLE_CPP.md` and `STYLE_SH.md` own language conventions. This rollout corrects stale lint
instructions without replacing mbo's detailed C++ rules or bashtest-specific shell test policy.
