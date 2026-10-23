# Infrastructure and publishing

This applies the lessons from [proto PR 100](https://github.com/mboworks/proto/pull/100)
and xff PRs 835–848, excluding 841. Run `bazel test //...`,
`python3 -m unittest discover -s tools -p '*_test.py'`, and `pre-commit run --all-files`.

## Local clang-tidy

Build with `bazel build --config=clang-tidy //...`, then run
`./compile_commands-update.sh` before `pre-commit run clang-tidy --all-files`.
The enforcing hook is report-only. Missing prerequisites, stale compilation databases,
parse errors, and findings fail the check. The clang-tidy job also uses that compiler database
to run `mbo/container/experimental/frozen_compile_test.py`: a successful baseline compile,
negative construction diagnostics, and C++26 interface compilation.

`require_serial: true` gives one coordinator ownership of all workers; pre-commit filename
batching cannot multiply worker pools. The default is `max(1, min(2, CPUs - 1))`, further
limited by selected translation units. `CLANG_TIDY_JOBS=1` reduces contention; a positive
integer overrides the default, while `auto` selects it. CI explicitly uses its runner CPU count.
These are worker limits, not operating-system CPU or memory quotas. Independently launched
commands do not share a global limit. Interruptions terminate active children and prevent queued
work from starting. Source-only changes remain focused, while header/build changes retain
mbo's conservative full-sweep behavior and SMHasher exclusions.

## Build caches and CI

With `--config=clang`, the hermetic LLVM module selects Apple's native linker (`auto`)
for macOS ARM64 and x86_64, following [proto PR 118](https://github.com/mboworks/proto/pull/118).
The toolchain supplies the matching linker capabilities; `.bazelrc` does not override the
linker executable or allow unresolved symbols through `dynamic_lookup`. Linux retains
the toolchain's bundled LLD default. Both target and host configurations use Bazel's
`--macos_minimum_os=11.0` settings so Apple platform metadata and compile/link actions
agree on the deployment target.

Linux LLVM jobs and the Bazel compatibility matrix use `ubuntu-24.04`. The pinned LLVM
distribution links its bundled LLD against `libxml2.so.2`; Ubuntu 26.04 provides the incompatible
`libxml2.so.16` ABI. Regular GCC and CMake jobs continue to use `ubuntu-latest`. Compiler pins,
warning policies, sanitizers, coverage requirements, and matrix combinations are unchanged.

Two additional optimized Bazel jobs use distribution Clang 22 and the distribution's default
standard library: one on the `ubuntu-26.04` host, and one in the official `ubuntu:26.10` container
on that host. Both run the full existing test suite, differential hashing, and disabled-mangling
checks through the reusable runner and participate in `done`. They do not add sanitizer, coverage,
or tidy matrix entries. The container covers Ubuntu 26.10 userspace on the host's kernel.
Container bootstrap installs its Git/LFS, Python, and package tooling before checkout. Compiler
probes reject a different compiler family or Clang major version. Cache namespaces include the
Ubuntu userspace release, compiler mode/version, and existing build/dependency axes.

Cache only compiled outputs: Bazel disk caches and the CMake job's ccache directory. Bazel keys separate OS, architecture, compiler/version, Bazel
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

The `cmake-gcc` job builds all CMake library, command-line, and benchmark targets
with GCC 15 on Ubuntu and runs the existing C++ tests through CTest. Builds and tests use all
runner CPUs through `CMAKE_BUILD_PARALLEL_LEVEL` and `CTEST_PARALLEL_LEVEL`. It also repeats the glob
test to verify fresh temporary directories on repeated invocations. A second configuration
enables compiler exceptions and checks requirement exceptions and disabled hash mangling,
including the optional wrappers and Stringify option validation. A separate consumer project verifies
`add_subdirectory` and the transitive C++23, include, and link requirements of `mbo::mbo`.
This job participates in `done`; it does not replace any Bazel, sanitizer, lint, or coverage gate.
The default, exception/configuration, and consumer builds use CMake's C/C++ compiler launchers
with ccache. PRs restore main generations; only successful main jobs save them. CMake keys use a
separate `cmake-ccache-v1` family partitioned by OS, architecture, GCC version, dependency/build-adapter
hash, run, and attempt. Compiler-content checks protect reuse when an installed compiler changes.
The ccache limit is 500 MB; saving synchronously cleans the cache and rejects a payload at or above
600,000,000 bytes. The shared compressed entry ceiling remains 700,000,000 bytes. Retirement requires
an exact usable main replacement and preserves other cache families, configurations, and newer runs.
One-day cache measurements feed the existing final storage report.

A clean rebuild of the configuration test must produce actual direct or preprocessed cache hits.
The job summary separates the initial library/binary/test build counters from the cache-verification
and alternate-build counters. CMake build trees and fetched dependencies remain local and are
excluded from uploads.

## Preparation profiles

The reusable coverage runner serves main, PRs, and releases. The supplemental exception-policy
targets include the experimental frozen containers' construction and lookup failures,
the experimental circular buffer's checked operations and its deque integration,
the shared value-pop suite across all sequence endpoints, Json array pop requirements, and
the optional wrappers' checked access and assignment, and Stringify option validation. The
value-pop exception tests disable optional return-value elision to exercise the single-move
contract. Its primary and exception-policy
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
target URL, including late reports that cannot replace a newer latest report. Aggregate history,
policy and patch data, and original run metadata remain indefinitely; detailed LCOV source pages
expire seven days after original completion. Shared compressed page bases and manifests retain
recent source views without duplicating each full HTML tree. Main stays first; merged
PRs and releases follow by actual merge/tag timestamps, while open and unknown reports follow by
workflow creation time. Closed-unmerged PRs are omitted from the overview while direct reports
remain. Aggregation ancestry is provenance and is verified for squashed parents without changing
measured identities. Replacement freshness is independent: an older slow run cannot overwrite a
newer report.

Both publishers compact the retained tree before committing, then measure the complete deployment
after artwork installation. `storage-report.json` records section totals; 200 MB warns and a 9 GB
payload guard stops packaging. The supported Pages site limit remains 1 GB. Publication snapshots
are fetched shallowly, source ancestry uses `blob:none`, and historical Git objects are not rewritten.
See [published site storage](site-storage.md) for retention, browser requirements, and validation.

Both full-site publishers add shared MBO Works favicons to the staged deployment copy only.
Retained release snapshots remain unchanged. The README uses the shared logo at 64 by 64 pixels.
Release tags remain numeric semantic versions and existing immutable release/BCR flows remain
in place. No dependency or toolchain pins change as part of this infrastructure adaptation.

## Contributor rules

`AGENTS.md` describes contributor obligations; `GIT_RULES.md` owns branch and PR operations.
`STYLE_CPP.md` and `STYLE_SH.md` own language conventions. This rollout corrects stale lint
instructions without replacing mbo's detailed C++ rules or bashtest-specific shell test policy.
