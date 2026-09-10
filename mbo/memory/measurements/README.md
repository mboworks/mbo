# mbo/memory measurements

This directory contains the raw, attributable evidence used to select and tune `mbo::memory::Arena`.
The methodology inherits the repository-wide artifact contract in
[`INTERNING_IMPLEMENTATION.md`](../../strings/INTERNING_IMPLEMENTATION.md) and the statistical
precautions established by [`mbo/hash/measurements`](../../hash/measurements/README.md).

Arena results are not publication decoration. They decide block growth, descriptor layout,
retention, and default source choices. Every selected representation must have comparable JSON from
the Apple M5 Pro and AMD Zen 5 reference machines before the implementation PR merges.

## Layout

```text
mbo/memory/measurements/
  README.md
  data/
    <machine>_<compiler>_<git-sha>_arena.json
```

Files under `data/` are immutable JSON envelopes produced by
[`tools/benchmark_artifact.py`](../../../tools/benchmark_artifact.py). The envelope contains the
unaltered Google Benchmark context and rows plus source, host, toolchain, command, timing, load, and
protocol provenance. Do not hand-edit results or replace raw repetitions with a Markdown summary.

The production-path target is `//mbo/memory:arena_benchmark`. The benchmark-only
`//mbo/memory:arena_layout_benchmark` target compares candidates that must not become API merely to
make an experiment possible. Its first experiment compares fixed, 1.5x, 2x, and 4x block growth
under identical retained and fresh mixed string-like workloads. Every 257th request is a dedicated
64 KiB oversized allocation, so the result also detects accidental distortion of normal growth.
Listed early-block sizes and retention alternatives are added as separate candidate implementations
before the layout proof is considered complete.

The same proof target compares candidate string-record representations independently from hashing:

| Candidate               | Descriptor                      | Address stability                                   | Intended use                  |
| ----------------------- | ------------------------------- | --------------------------------------------------- | ----------------------------- |
| Pointer                 | pointer plus 32-bit size        | Stable through growing Arena                        | General growing interner      |
| Contiguous offset fixed | 32-bit offset plus 32-bit size  | Stable only with fixed/pre-reserved content storage | Fixed-capacity specialization |
| Segmented offset        | segment, offset, and size       | Stable while segments remain allocated              | General growing interner      |
| Contiguous inline fixed | offset plus inline size/content | Stable only with fixed/pre-reserved content storage | Fixed-capacity specialization |

Insertion and sequential/permuted dense-ID lookup are measured separately. Fixed-capacity
candidates are named as such in every benchmark row: pre-reserving exact corpus space must never be
mistaken for a growing container guarantee. Pointer storage uses a growing Arena rather than an
unfair maximum-size first block. The stable segmented-offset candidate includes its extra segment
lookup and all segment/descriptor reserved bytes.

## Reference commands

Run from a clean checkout of the exact implementation commit after dependencies have already been
downloaded and built. The output filename is descriptive only; metadata inside the file is
authoritative.

Apple M5 Pro:

```sh
python3 tools/benchmark_artifact.py run \
  --component Arena \
  --target //mbo/memory:arena_benchmark \
  --output mbo/memory/measurements/data/macos-arm64-apple-m5-pro_clang-22_<sha>_arena.json \
  --config clang \
  --config opt_apple_m5 \
  --baseline-commit <parent-sha> \
  -- bazel run //mbo/memory:arena_benchmark --config=clang --config=opt_apple_m5 -c opt --
```

AMD Zen 5:

```sh
python3 tools/benchmark_artifact.py run \
  --component Arena \
  --target //mbo/memory:arena_benchmark \
  --output mbo/memory/measurements/data/linux-x86-64-amd-ryzen-9-9950x_clang-22_<sha>_arena.json \
  --config clang \
  --config opt_zen5 \
  --baseline-commit <parent-sha> \
  -- bazel run //mbo/memory:arena_benchmark --config=clang --config=opt_zen5 -c opt --
```

Use the default nine repetitions, random interleaving, one-second warmup, and one-second minimum
time. A smoke run may shorten time controls only when marked `suspect` with a reason; it is never
merge evidence.

Validate every artifact:

```sh
python3 tools/benchmark_artifact.py validate mbo/memory/measurements/data/*.json
```

## Required experiment matrix

| Dimension       | Required cases                                                                  |
| --------------- | ------------------------------------------------------------------------------- |
| Allocation size | 8, 16, 32, 64, 256, 1,024, and 4,096 bytes plus mixed string-like distributions |
| Alignment       | 8, 16, 64, 256, `max_align_t`, and unsupported alignment failure                |
| Source          | New/delete, standard allocator adapter, PMR adapter, fixed external, and inline |
| Growth          | Fixed, repeated, listed, geometric, and dedicated oversized blocks              |
| Lifecycle       | Fresh allocation, retained allocation, reset, release, and reuse                |
| Representation  | Intrusive block headers and every serious pointer/offset alternative            |
| Baseline        | Direct new/delete and `std::pmr::monotonic_buffer_resource`                     |
| Failure         | Source exhaustion, maximum alignment, arithmetic limit, and hard/try APIs       |
| Memory          | Used/reserved bytes, blocks, padding, fragmentation, and peak memory            |

## Review method

Compare matched benchmark names from one randomly interleaved run. Retain all repetitions. Headline
summaries use the mean of the fastest three of nine samples, matching the hash methodology, while
also reporting the full minimum, median, mean, standard deviation, and coefficient of variation.

A candidate is selected only after inspecting latency distributions and memory cost together.
Faster allocation does not excuse materially worse fragmentation or an incomplete failure/lifetime
contract. A regression or noisy result remains in the JSON and is explained in the pull request; it
is never removed merely because it complicates the conclusion.

## Evidence status

| Machine      | Compiler | Implementation SHA | Baseline SHA | Artifact | Status  |
| ------------ | -------- | ------------------ | ------------ | -------- | ------- |
| Apple M5 Pro | Clang 22 | pending            | pending      | pending  | pending |
| AMD Zen 5    | Clang 22 | pending            | pending      | pending  | pending |

This table is updated only from validated JSON. The JSON remains the source of truth.
