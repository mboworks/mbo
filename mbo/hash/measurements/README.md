# mbo/hash measurements

Dev-only tooling to run `//mbo/hash:hash_benchmark`, store the results with full
provenance, render the README performance tables, and plot ns-vs-length curves.
This is a **pure development dependency**: the whole `mbo/hash/measurements`
directory is stripped from release archives (`release_prep.sh` EXCLUDES), so it is
never part of any published/BCR offering and consumers of the library never pull
any of it in (including any heavier plotting dependencies). It is an ordinary dev
package of the `mboworks_mbo` module - built and tested in dev/CI - and a
pre-commit guard forbids anything outside it from depending on it, so the release
strip can never leave a dangling reference.

This is a living design doc - update it when the layout or policy changes.

## Why this exists

The `mbo/hash/README.md` numbers are sub-nanosecond and are taken on a shared
developer machine. A single median run is dominated by run-to-run contention
noise: a change smaller than ~0.2 ns is invisible, and medians of two runs can
even invert the true ordering. So we:

- Aggregate by the **mean of the k fastest repetitions** (`k = reps/3`, so 3
  of 9). Contention only ever slows a run down, so the low tail is the
  uncontended cost; taking the _mean of the fastest few_ rejects the
  single-sample noise a pure minimum would keep, while still excluding the
  contended runs the median includes.
- Apply google/benchmark's own precautions (below).
- Store the **full run context** so a number is always attributable to a
  specific machine, load, toolchain, and source revision.

## Layout

```text
mbo/hash/measurements/
  README.md                    # this design doc
  BUILD.bazel                  # dev-only targets: the quality-table verify test (stripped from releases)
  run_measurements.py          # per-machine runner: one full sweep + smhasher -> a data bundle (nothing else)
  hash_benchmark_report.py     # run / store / tables / plot / smhasher / bundle / publish / verify / quality / consistency (stdlib only)
  hash_algorithms.json         # manual/editorial SOT for the quality tables (role, notice, seeded, starlark, ...; NOT measured data)
  quality_sh_test.sh           # bazel test: the README quality tables match hash_algorithms.json x a bundle, and bundles agree
  build_smhasher3.sh           # reproducible SMHasher3 build (clone + fixes + install plugin + container gcc)
  smhasher3/mbohash.cpp        # in-house mumbo/jumbo and dumbo SMHasher3 registration (includes the real headers)
  charts/<slug>_<cc>_*.svg     # published per-machine charts (committed; rendered by `publish`, verifiable)
  data/<slug>_<cc>_*.tgz       # per-machine measurement bundles, Git LFS, flat (see "Data storage")
```

The C++ benchmark itself (`mbo/hash/hash_benchmark.cc`,
`//mbo/hash:hash_benchmark`) stays in the main module - it is tightly coupled to
the hash internals and is already `testonly` + `tags = ["manual"]`, so it is
dev-only but not shipped. Only the orchestration/storage/reporting lives here.

## What is captured (provenance)

Every stored dataset records, from google/benchmark's `context` plus a few
augmentations, enough to know exactly what produced it:

- **date/time** of the measurement.
- **git revision** of the measured source and whether the tree was dirty - a
  number is meaningless without the exact code it measured.
- **host name**, **CPU count**, **CPU nominal MHz**, **CPU brand/model**, cache
  sizes.
- **OS / kernel** (uname).
- **compiler** name, semantic version, vendor version string, and Apple build
  identifier when present (the benchmark is built `-c opt`).
- requested **C++ standard**, the actual `__cplusplus` value, and the standard
  library implementation/version.
- available macOS deployment-target and SDK macros.
- **Bazel version**, exact benchmark command, configs, and explicit target/host
  compiler options.
- **load average** at measurement time (so a noisy run is identifiable).
- **`cpu_scaling_enabled`**: if frequency scaling is on (it is, on macOS), the
  absolute numbers are unreliable; the tool records it and warns. The min
  aggregate mitigates but does not eliminate this.
- benchmark **parameters**: repetitions, min-time, warmup-time, whether random
  interleaving was on, and the aggregate used (best-`k` mean) with its `k`.

## Methodology / precautions

- Build `-c opt`.
- **9 repetitions** by default: google/benchmark's `tools/compare.py` recommends
  at least 9 for its Mann-Whitney U-test, so two stored raw datasets can be
  compared for statistical significance (`compare.py benchmarks a.json b.json`).
- **`--benchmark_enable_random_interleaving`**: interleaves the repetitions of
  every case, spreading each case across the run so thermal drift and "first
  case runs cold" bias do not accrue to whichever benchmark ran first.
- **`--benchmark_min_warmup_time`**: warm caches and CPU frequency before timing.
- Aggregate by the **best-`k` mean** (see above); we also keep `best_cv` (spread
  of those `k`) plus the full-set min / median / mean per case for transparency.
- Fixed RNG seeds in the benchmark give identical key bytes per length across
  runs (so only timing varies). Throughput is hot-cache (the same key is reused
  in the loop) by design; the latency benchmark serializes a dependency chain
  over a fixed 1024-key set - the cost profile a hash table actually pays.

## Statistics stored per case

Per (width, algorithm, length): `best` (the headline mean of the fastest `k`),
`best_cv` (coefficient of variation **of those `k` samples** - how stable the
estimate is; a full-set CV would just re-report the contention we excluded),
plus `min`, `median`, `mean` (full-set, for context) and the repetition count.
The README tables use `best`; `best_cv` flags a case whose fast tail is itself
noisy and worth re-measuring.

## Data storage, commit policy, contributions

Authoritative numbers cannot come from CI (shared, virtualized runners with CPU
scaling on - see "CI"), so measurements are contributed from real machines
(Apple M5 Pro, AMD Zen5, Intel, ...). Each run produces a fair amount of data:
the raw google/benchmark JSON gzips to ~123 KB (fast) / ~340 KB (full), the
distilled canonical JSON is ~38 KB / ~100 KB, plus the per-algorithm SMHasher3
logs. Across several machines that accumulates, so:

- **Per-machine bundle, Git LFS**
  (`data/<slug>_<cores>c_<compiler>_<gitsha8>_<stamp>.tgz`): one
  gzipped tarball per run holds the _whole_ dataset - the canonical
  `results.json`, the raw `*_raw.json.gz` (for `compare.py` U-tests), and the
  `smhasher.json` + per-algorithm logs. It is Git-LFS-tracked (`.gitattributes`),
  so the blobs stay out of the main pack; unpack to inspect or A/B. Keep the
  current bundle per machine (replace on re-measure); LFS history retains olds.
- **Published charts + tables, plain git** (`charts/<slug>_<compiler>_{64,128}.svg`
  - the hash README perf section): rendered by `hash_benchmark_report.py publish`
    from a SELECTED set of committed bundles - one labeled block per machine. Charts
    carry the machine identifier (subtitle); the tables are the curated `kReadmeSizes`
    subset extracted from the FULL dataset via the `readme_sizes` the benchmark emits
    into its own context (no separate fast run, no second size list to drift). And -
    the point - _verifiable_: `verify` re-renders from the same bundles (a manifest
    publish leaves in the README) and diffs, so every figure is provably from
    committed data and cannot be hand-edited (`_svg_plot` is deterministic). This is
    the ONLY thing that writes to the tree; the measurement run writes nothing but
    the bundle, so it stays authoritative and several runs can go back-to-back.
- The loose run outputs in `data/` are staging only and git-ignored; `bundle`
  packs them into the `.tgz`. This directory is dev-only and stripped from release
  archives, so it never enters a published/BCR offering (the release archive
  carries LFS pointer files here, which is harmless).

## Regenerate / contribute a machine

Two decoupled steps: each machine produces a data bundle (that is ALL the run
writes to the tree, so it stays authoritative and runs can go back-to-back), then
a single `publish` renders the README from the machines you choose.

**1. Measure, per machine** (repo root, clean `main` for authoritative provenance):

```sh
# ALWAYS add the machine's `opt_<machine>` config (defined in .bazelrc): without
# -march/-mcpu the benchmark measures generic codegen, which understates
# SIMD-heavy competitors (xxh3 +27% at 1 KiB under znver5) and can hide
# target-specific pathologies (mumbo is 2x slower under clang znver5 >= 1 KiB).
mbo/hash/measurements/run_measurements.py --config clang --config opt_zen5 --jobs 4      # AMD Zen 5
mbo/hash/measurements/run_measurements.py --config clang --config opt_apple_m5 --jobs 4  # Apple M5
# faster: in-house family only, or perf only
mbo/hash/measurements/run_measurements.py --algos mumbo,jumbo,dumbo --jobs 1
mbo/hash/measurements/run_measurements.py --skip-smhasher
```

New machine: add a `common:opt_<machine> --copt=-march=...` (or `-mcpu=...`)
entry to `.bazelrc` first. The `--config` values are recorded in the dataset
context, so the tuning target is part of the bundle's provenance.

ONE full perf sweep runs first and alone (SMHasher3 would contend for CPU and
skew the sub-ns numbers), then the battery (`--jobs` at once; pass/fail is
load-independent, so that only trades cores for wall-clock). SMHasher3 is built
and run in a container (`build_smhasher3.sh`, via `docker run`). The run packs a
per-machine Git-LFS bundle and prints its path - commit it:

```sh
git add mbo/hash/measurements/data/<bundle>.tgz
git lfs push origin HEAD && git push
```

**2. Publish, once** - render the README from the bundles you want to feature:

```sh
mbo/hash/measurements/hash_benchmark_report.py publish \
    --bundles data/<m1>.tgz data/<m2>.tgz data/<m3>.tgz
git add mbo/hash/README.md mbo/hash/measurements/charts
```

`publish` cuts the marker region in the hash README and re-appends one labeled
block per bundle (charts + curated tables), in the listed order; `--bundles` is
the selection. `verify` (no args, from the repo root) re-checks every featured
bundle against the committed charts, reading the machine list back from the
README's manifest.

The pipeline steps are also usable individually: `run` (perf -> canonical JSON),
`bundle` (pack a run), `tables` / `plot` (render from any canonical JSON _or_ a
bundle `.tgz` directly - given positionally or via `--results`/`--bundle`),
`publish`, `verify`. `plot --kind` selects the curves: `throughput` (the 64- and
128-bit one-shot charts, default), `latency` (the mixed-length latency curve, now
that it sweeps the full size set), or `all`. `plot --scale` selects the axes:
`log-log` (default - ns spans ~4 decades, so a linear y crushes the fast
algorithms) or `linear-log` (linear y, log x, to read absolute ns gaps in a
narrow range); x is always log since lengths are sampled geometrically.

## SMHasher3 quality (`smhasher`)

`hash_benchmark_report.py smhasher --algos all --smhasher3 <path>` runs the
SMHasher3 battery over a set of algorithms (default `all`, which **includes the
legacy `dumbo`**) and stores a pass/fail + failing-family summary as JSON with
the same provenance, plus each run's full log under `data/`.

It drives a **built SMHasher3 executable** (`--smhasher3`). `build_smhasher3.sh`
is the reproducible build: it clones SMHasher3 at the pinned commit, applies the
two documented fixes (missing `<cstdlib>` in `lib/AEStest.cpp`; replace
`-march=native`), and builds it with gcc in a container - matching the
methodology in `../README.md`. The third-party algorithms are SMHasher3
built-ins and work immediately.

The in-house `mumbo`/`jumbo` and `dumbo`, `fambo` are registered by
`smhasher3/mbohash.cpp`,
which `#include`s the ACTUAL `mbo/hash` headers (so the real implementation is
verified, not a transcription). `build_smhasher3.sh` copies the headers + plugin
into the tree and registers the source, so one script run yields a SMHasher3
that recognizes `mumbo-64` / `jumbo-128` and `dumbo-64`. Registration names live
in `_SMHASHER_NAMES` and match `SMHasher3 --list`.

Last verified run (2026-07): **mumbo-64/jumbo-128** and **dumbo-64** all PASS
188/188 (dumbo redesigned into a compact single-lane MUM hash).

### Generated quality tables (`quality`, `consistency`)

The two SMHasher3 tables in `../README.md` (the "Algorithm overview" and the
"Results" table) are GENERATED, not hand-edited. `hash_benchmark_report.py quality
--bundle <bundle>` renders both between their `<!-- BEGIN/END ... -->` markers,
merging `hash_algorithms.json` (the manual/editorial columns) with the measured
verdict/score/failures re-parsed from the bundle's logs (the stored `smhasher.json`
predates parser fixes, so the LOGS are the truth). `quality --check` verifies the
committed tables without writing - the `quality_sh_test` bazel test / CI gate.

Two names the committed bundles have no real data for (`FNV-1a-64` /
`MurmurHash3-128`, run under invalid short names) are supplied by a hardcoded
`_MISSING_MEASURED` stopgap, warned about on every render; delete each once a
bundle carries the real measurement.

`hash_benchmark_report.py consistency` cross-checks that all bundles from the same
source git SHA report identical measurements (a SMHasher3 verdict is a property of
the algorithm, not the machine), so a divergent or broken run is caught.

## Output filenames

Loose staging files the tool writes are prefixed `YYYYMMDD_HHMMSS_` (local wall
clock, one stamp per invocation) so runs never overwrite each other. The
committed artifact is the per-machine bundle
`data/<slug>_<cores>c_<compiler>_<gitsha8>_<stamp>.tgz` - the slug
derived from the dataset's own `uname` + CPU brand, the `compiler` reported by the
benchmark binary (`clang-NN` / `gcc-NN`, so GCC and Clang builds on one machine
stay distinct), the SHA from its provenance - so a bundle is self-identifying and
collision-free across machines and toolchains.

## Comparison charts

Multiple measurement bundles can be compared graphically using the `diff-charts`
sub command of the `hash_benchmark_report.py` tool. Example:

```sh
./mbo/hash/measurements/hash_benchmark_report.py diff-charts --bundles mbo/hash/measurements/data/linux*.tgz --algos mumbo dumbo rapidhash
```
