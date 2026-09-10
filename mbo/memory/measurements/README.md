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

| Candidate               | Descriptor                       | Address stability                                   | Intended use                  |
| ----------------------- | -------------------------------- | --------------------------------------------------- | ----------------------------- |
| Pointer                 | pointer plus 32-bit size         | Stable through growing Arena                        | General growing interner      |
| Pointer SoA             | separate pointer and size arrays | Stable through growing Arena                        | General growing interner      |
| Contiguous offset fixed | 32-bit offset plus 32-bit size   | Stable only with fixed/pre-reserved content storage | Fixed-capacity specialization |
| Segmented offset        | segment, offset, and size        | Stable while segments remain allocated              | General growing interner      |
| Contiguous inline fixed | offset plus inline size/content  | Stable only with fixed/pre-reserved content storage | Fixed-capacity specialization |

Insertion and sequential/permuted dense-ID lookup are measured separately. Fixed-capacity
candidates are named as such in every benchmark row: pre-reserving exact corpus space must never be
mistaken for a growing container guarantee. Pointer storage uses a growing Arena rather than an
unfair maximum-size first block. The stable segmented-offset candidate includes its extra segment
lookup and all segment/descriptor reserved bytes.

Post-burst retention is measured as a distinct lifecycle rather than inferred from steady-state
allocation. Each candidate first processes the full workload, including oversized requests, and
then repeatedly processes a smaller ordinary-string workload. The candidates retain the complete
burst-shaped Arena chain with `Reset`, release every block to new/delete, release through a
best-fit cache limited to 2 MiB and blocks no larger than 256 KiB, or release through an 8 MiB cache
that can retain every normal block.

The cache candidates reconstruct a compact chain from the smallest suitable retained blocks.
Reported retained and peak bytes include cache metadata as well as backing blocks, and retained
block count is reported separately. This experiment determines whether intelligent reuse belongs
in a block source or Arena option; it does not expose the benchmark cache as production API.

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

| Machine      | Compiler | Implementation SHA                    | Baseline SHA | Artifact                                                                                                                                                                                                                                                                 | Status  |
| ------------ | -------- | ------------------------------------- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| Apple M5 Pro | Clang 22 | `c913ba9ae`, `673d95295`, `8a82b50cb` | `797b31c24`  | [`layouts`](data/macos-arm64-apple-m5-pro_clang-22_c913ba9ae_arena-layouts.json), [`retention`](data/macos-arm64-apple-m5-pro_clang-22_673d95295_arena-retention.json), [`pointer records`](data/macos-arm64-apple-m5-pro_clang-22_8a82b50cb_arena-pointer-records.json) | valid   |
| AMD Zen 5    | Clang 22 | pending                               | pending      | pending                                                                                                                                                                                                                                                                  | pending |

This table is updated only from validated JSON. The JSON remains the source of truth.

## Apple M5 Pro results

The primary artifact contains 22 benchmark families with exactly nine raw repetitions per family.
It records a clean `c913ba9ae` tree, Apple M5 Pro, Clang 22.1.8, C++20, Bazel 9.2.0, random
interleaving, one-second warmup and minimum time, a 333.58-second run, and load averages
1.70/1.65/2.39. Times below are CPU nanoseconds per 16,384-operation workload. `Fast 3` is the mean
of the three fastest raw repetitions; standard deviation and CV use all nine.

### Growth

| Lifecycle | Growth      | Fast 3 | Median |     Mean | Stddev |    CV | Blocks | Reserved |   Waste |
| --------- | ----------- | -----: | -----: | -------: | -----: | ----: | -----: | -------: | ------: |
| Fresh     | Fixed 4 KiB | 119832 | 127135 | 126416.8 | 6335.1 | 5.01% |    452 |  5726081 |  164466 |
| Fresh     | 1.5x        |  75754 |  76577 |  77022.6 | 1366.6 | 1.77% |     20 |  6708794 | 1147179 |
| Fresh     | 2x          |  76168 |  77252 |  77181.5 |  895.4 | 1.16% |     15 |  6418558 |  856943 |
| Fresh     | 4x          |  63842 |  72436 |  69822.4 | 4785.1 | 6.85% |     10 |  6639616 | 1078001 |
| Fresh     | Listed      |  59949 |  60930 |  62186.9 | 3272.1 | 5.26% |     15 |  5984634 |  423019 |
| Retained  | Fixed 4 KiB |  46860 |  47053 |  47111.7 |  252.6 | 0.54% |    452 |  5726081 |  164466 |
| Retained  | 1.5x        |  41178 |  41208 |  41219.1 |   42.5 | 0.10% |     20 |  6708794 | 1147179 |
| Retained  | 2x          |  41142 |  41176 |  41214.2 |   89.5 | 0.22% |     15 |  6418558 |  856943 |
| Retained  | 4x          |  41104 |  41269 |  41388.9 |  446.8 | 1.08% |     10 |  6639616 | 1078001 |
| Retained  | Listed      |  38546 |  38794 |  38729.9 |  150.2 | 0.39% |     15 |  5984634 |  423019 |

On this machine the listed sequence is the only candidate that wins both lifecycle timings while
remaining near the low-memory frontier. Compared with fixed 4 KiB blocks it reserves 258,553 more
bytes, but removes 437 block acquisitions and improves the fastest-three fresh and retained times
by 50.0% and 17.7%. The choice remains provisional until the Zen 5 artifact agrees or explains a
different tradeoff.

### String-record layouts

| Operation         | Layout                  | Fast 3 | Median |     Mean |  Stddev |     CV | Reserved |
| ----------------- | ----------------------- | -----: | -----: | -------: | ------: | -----: | -------: |
| Insert            | Pointer                 | 154401 | 160142 | 160011.8 |  5245.5 |  3.28% |  2293760 |
| Insert            | Contiguous offset fixed | 181020 | 185917 | 185313.8 |  3963.3 |  2.14% |  1568768 |
| Insert            | Segment offset          | 143980 | 168371 | 170980.8 | 26462.0 | 15.48% |  1704192 |
| Insert            | Contiguous inline fixed | 203771 | 206732 | 207492.9 |  3277.9 |  1.58% |  1568768 |
| Lookup sequential | Pointer                 |   8689 |   8709 |   8705.4 |    14.5 |  0.17% |  2293760 |
| Lookup sequential | Contiguous offset fixed |   6122 |   6167 |   6160.3 |    32.7 |  0.53% |  1568768 |
| Lookup sequential | Segment offset          |  13303 |  13312 |  13327.3 |    47.0 |  0.35% |  1704192 |
| Lookup sequential | Contiguous inline fixed |  13485 |  13846 |  13774.7 |   262.0 |  1.90% |  1568768 |
| Lookup permuted   | Pointer                 |  12566 |  12619 |  12601.6 |    37.9 |  0.30% |  2293760 |
| Lookup permuted   | Contiguous offset fixed |   7752 |   7768 |   7765.0 |    13.6 |  0.18% |  1568768 |
| Lookup permuted   | Segment offset          |  16247 |  16262 |  16267.2 |    25.0 |  0.15% |  1704192 |
| Lookup permuted   | Contiguous inline fixed |  17298 |  17409 |  17619.0 |   415.9 |  2.36% |  1568768 |

The exact-reservation contiguous offset layout is the fastest and smallest, but it is evidence only
for a fixed-capacity specialization: growing its content vector would invalidate previously
returned views. Among general growing layouts, pointer records use 589,568 more reserved bytes than
segmented offsets but improve fastest-three sequential and permuted lookup by 34.7% and 22.7%.

The broad run's segmented-offset insertion samples were noisy. A second validated artifact,
[`arena-record-insertion.json`](data/macos-arm64-apple-m5-pro_clang-22_c913ba9ae_arena-record-insertion.json),
isolated pointer and segmented-offset insertion under the same full protocol. Its all-nine means
were 159,287 ns and 161,820 ns, with CVs of 6.68% and 10.56%. That confirms insertion is too close
and variable to select the representation; lookup and memory are the present discriminators. No
final record-layout decision is made before Zen 5 measurement and integration with the actual index.

A third validated artifact isolates array-of-structures pointer records from a structure-of-arrays
alternative. It records a clean `8a82b50cb` tree and the same compiler and benchmark controls. The
run ended at elevated load averages of 2.90/8.77/7.94, so its raw samples and variability are
reported explicitly. The lookup CVs remain below 0.82%, and every operation shows a much larger
penalty than the run-to-run variation.

| Operation         | Layout      | Fast 3 | Median |     Mean | Stddev |    CV | Reserved |
| ----------------- | ----------- | -----: | -----: | -------: | -----: | ----: | -------: |
| Insert            | Pointer AoS | 153123 | 156572 | 155993.5 | 3045.6 | 1.95% |  2293760 |
| Insert            | Pointer SoA | 183193 | 187831 | 187219.2 | 4773.6 | 2.55% |  2228224 |
| Lookup sequential | Pointer AoS |   8681 |   8684 |   8684.9 |    4.5 | 0.05% |  2293760 |
| Lookup sequential | Pointer SoA |  10437 |  10463 |  10480.2 |   59.3 | 0.57% |  2228224 |
| Lookup permuted   | Pointer AoS |  12590 |  12596 |  12600.4 |   13.1 | 0.10% |  2293760 |
| Lookup permuted   | Pointer SoA |  15357 |  15564 |  15515.6 |  125.7 | 0.81% |  2228224 |

SoA removes the four bytes of padding in each 16-byte pointer record, saving exactly 65,536 bytes
or 2.86% of total reserved storage. In exchange it makes fastest-three insertion 19.6% slower,
sequential lookup 20.2% slower, and permuted lookup 22.0% slower. The memory saving is insufficient
to justify three allocations and split-field access as the general default. AoS pointer records
remain the provisional growing-layout candidate, subject to Zen 5 confirmation and measurement
inside the complete interner.

### Post-burst retention

The retention artifact records a clean `673d95295` tree and the same toolchain and controls as the
layout artifact. It contains four families with nine raw repetitions each and ran for 72.77 seconds.
The recorded load averages were 3.54/4.52/4.61. Cache-family CV remains below 0.35%; release-all is
explicitly retained despite its higher 5.73% CV.

| Strategy                | Fast 3 | Median |    Mean | Stddev |    CV | Peak bytes | Retained bytes | Blocks |
| ----------------------- | -----: | -----: | ------: | -----: | ----: | ---------: | -------------: | -----: |
| Retain complete chain   |  10210 |  10262 | 10263.6 |   62.9 | 0.61% |    6418558 |        6418558 |     15 |
| Release all             |  10655 |  11275 | 11341.0 |  649.7 | 5.73% |     520192 |              0 |      0 |
| Cache small blocks      |   7686 |   7696 |  7706.5 |   26.5 | 0.34% |     652926 |         652926 |      9 |
| Cache all within budget |   7722 |   7746 |  7744.8 |   18.7 | 0.24% |    6420094 |        6420094 |     15 |

The bounded small-block cache improves fastest-three time by 24.7% relative to retaining the full
burst-shaped chain and by 27.9% relative to releasing everything. It is within 0.5% of caching the
full chain while retaining only 10.2% as many bytes. This supports reusable blocks living in an
intelligently bounded source rather than unconditional Arena retention. The exact 2 MiB/256 KiB
limits are not selected defaults: this workload naturally retained only 652,926 bytes, and Zen 5
plus additional burst shapes must establish useful thresholds.
