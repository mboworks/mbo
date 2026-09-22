# Interning implementation and measurement plan

This document defines the implementation dependency graph for `Arena`, `SegmentedSequence`, HAMT,
and `StringInterner`. The semantic contracts live in the component design documents. This plan
controls how implementations, experiments, measurements, and pull requests turn those contracts
into production code.

No implementation pull request merges until its relevant optimized benchmarks have been measured
on both reference machines:

- Apple M5 Pro, macOS arm64, with the repository's supported Clang toolchain;
- AMD Ryzen 9 9950X (Zen 5), Linux x86-64, with the repository's supported Clang toolchain and GCC
  where compiler-sensitive code generation is plausible.

Measurement chooses representations; it does not weaken correctness, lifetime, stability,
exhaustion, or API guarantees already settled by the design documents.

## Dependency graph

```text
design and measurement contract
             |
             v
shared benchmark artifact tooling
             |
             v
block-source concept and Arena
             |
             +-----------------------+
             |                       |
             v                       v
SegmentedSequence              HAMT experiments
             |                       |
             |                       v
             |                selected production HAMT
             |                       |
             +-----------+-----------+
                         |
                         v
                   StringInterner
                         |
                         v
             repository-wide CI collection
```

`StringInterner` depends on `Arena` and `SegmentedSequence`. It depends on HAMT only if HAMT wins the
index benchmarks. HAMT remains a general container deliverable even if another index wins for the
interner.

## Branch and pull-request sequence

Each production branch is based on the preceding production branch unless the table explicitly
marks it as an independent proof. Pull-request descriptions must identify their parent and include
the required `## AG;DR` detail section.

| Order | Branch                                 | Deliverable                                             | Base                         | Merge evidence                                      |
| ----: | -------------------------------------- | ------------------------------------------------------- | ---------------------------- | --------------------------------------------------- |
|     0 | `design/string-interning`              | Contracts, dependency graph, measurement requirements   | `main`                       | Documentation validation                            |
|     1 | `perf/benchmark-artifacts`             | Shared JSON runner, metadata schema, validation tooling | `design/string-interning`    | Self-tests and example schema validation            |
|     2 | `feature/arena`                        | Block source adapters and raw byte `Arena`              | `perf/benchmark-artifacts`   | M5 Pro and Zen 5 Arena JSON                         |
|    2a | `proof/arena-layouts`                  | Pointer/offset and growth candidates                    | `feature/arena`              | Comparative JSON; never merged wholesale            |
|     3 | `feature/segmented-sequence`           | Production `SegmentedSequence`                          | `feature/arena`              | M5 Pro and Zen 5 sequence JSON                      |
|    3a | `proof/segmented-sequence-layouts`     | Directory, mapping, reuse, and retention candidates     | `feature/segmented-sequence` | Comparative JSON; selected commits only             |
|    4a | `proof/hamt-layouts`                   | Fragment, bitmap, collision, ownership candidates       | `feature/arena`              | Comparative JSON; never merged wholesale            |
|     4 | `feature/hamt`                         | Selected node/flat persistent and transient HAMT        | `feature/segmented-sequence` | M5 Pro and Zen 5 HAMT JSON                          |
|     5 | `feature/string-interner`              | Arena-backed cascading `StringInterner`                 | `feature/hamt`               | M5 Pro and Zen 5 end-to-end interner JSON           |
|     6 | `perf/ci-benchmark-collection`         | Optimized CI benchmark artifact collection              | `feature/string-interner`    | CI artifact schema and collection integration tests |
|     7 | `perf/benchmark-history-and-reporting` | Artifact-store ingestion, comparisons, and chart inputs | previous                     | Fixture history, regression tests, generated charts |

Proof branches are disposable experimental histories. Their source is not part of the production
stack unless a measured winner is deliberately implemented or selected into the corresponding
production branch. Their JSON results remain reviewable evidence.

The production stack may be split further when a reviewable unit becomes too large, but a split
must preserve this dependency order and must add its own tests, benchmark coverage, and two-machine
evidence before merge. The active pull-request graph is kept small enough to avoid wasting CI on
heads made obsolete by a parent update.

## Measurement artifact contract

Every benchmark invocation writes raw Google Benchmark JSON. A checked-in measurement JSON file
contains or accompanies the following metadata without relying on its filename:

| Field              | Requirement                                                                |
| ------------------ | -------------------------------------------------------------------------- |
| Schema             | Stable schema name and integer version                                     |
| Component          | Arena, SegmentedSequence, HAMT, or StringInterner                          |
| Git identity       | Full commit SHA, dirty state, branch, and repository                       |
| Source relation    | Candidate/options name and baseline or parent SHA                          |
| Host               | OS, architecture, CPU model, physical/logical core counts, and memory      |
| Toolchain          | Compiler family/full version, C++ baseline, Bazel version, and build flags |
| Build              | Optimized Bazel configuration and target                                   |
| Timing             | UTC start/end, duration, load average, and benchmark time source           |
| Benchmark controls | Repetitions, minimum time, warmup, random interleaving, and filters        |
| Result             | Unmodified Google Benchmark context and benchmark rows                     |
| Provenance         | Runner version and exact command                                           |

Raw observations are immutable. Derived summaries and charts are regenerated from them. A later
artifact store may retain the same JSON outside Git, but ingestion must preserve content hashes and
the schema so results remain comparable across pull requests and time.

### Default measurement protocol

- Build with Bazel optimized mode and the reference machine-specific optimization configuration.
- Download and build dependencies before timing; benchmark measurements never include network or
  repository-fetch latency.
- Use nine repetitions with Google Benchmark random interleaving.
- Use a nonzero warmup and sufficient minimum time for stable samples.
- Record every repetition, aggregate statistics, and benchmark counters in JSON.
- Keep the machine otherwise idle and record load so noisy runs can be rejected rather than hidden.
- Measure a candidate and its baseline in one interleaved invocation where possible.
- Repeat the complete suite when code, compiler, flags, benchmark inputs, or machine configuration
  changes; never combine incomparable samples silently.
- Preserve failed or surprising measurements with an explicit validity field and explanation.

The runner validates required context before accepting a JSON file. Human-written Markdown tables
may summarize results, but they never replace the raw JSON evidence.

## Component measurement scope

### Arena

- successful fast-path latency by size and alignment;
- exhaustion and `TryAllocate` failure latency;
- fixed, repeated, listed, geometric, and oversized block growth;
- standard allocator, PMR, caller-owned fixed, and growing sources;
- reset, release, block reuse, and retention;
- requested bytes, used bytes, reserved bytes, blocks, padding, and fragmentation;
- pointer descriptors, segment-relative offsets, and inline records under representative string
  size distributions.

### SegmentedSequence

- append, `try_emplace_back`, unchecked append, pop, and `pop_back_value`;
- indexed access and iteration across segment boundaries;
- uniform power-of-two, compile-time list, and hybrid mappings;
- segment directory layouts and generated-code size;
- exact-fit, close-fit, and largest-fit retained segment reuse;
- retention budgets, burst growth/pop cycles, `trim_capacity`, and `release`;
- trivial, movable, non-trivial, small, large, and over-aligned element types;
- comparison with `std::vector`, `std::deque`, and other relevant segmented containers.

### HAMT

- successful and unsuccessful heterogeneous lookup, insertion, update, and erasure;
- persistent path-copy mutation and transient in-place mutation;
- transient-to-persistent conversion and branch creation;
- 4-, 5-, 6-, and 7-bit fragments;
- CHAMP and credible bitmap/node alternatives;
- stored versus recomputed hashes and full-hash collision layouts;
- node and flat layouts;
- atomic, non-atomic, domain, arena, and generation ownership candidates;
- iterator strategies, traversal, memory per entry, allocation count, cache misses, and branch
  behavior;
- comparison with standard unordered containers, Abseil flat/node containers, and suitable trie
  implementations.

### StringInterner

- hits and misses in forward and reverse directions across root, shallow, and deep chains;
- unique insertion and duplicate insertion for short, medium, long, empty, and embedded-NUL strings;
- string-size and duplication distributions representative of mbo, xff, and proto;
- character storage, dense sequence, and index costs both separately and end to end;
- default 32-bit IDs plus relevant 8-, 16-, and 64-bit configurations;
- 32- and 64-bit hash paths without coupling hash width to ID width;
- standard, Abseil, and HAMT index candidates;
- bounded exhaustion for ID, character, entry, and index storage;
- allocation count, collisions, chain depth, memory breakdown, fragmentation, and peak memory;
- diagnostics-enabled overhead and proof of zero production overhead when disabled.

## Per-PR merge gate

An implementation pull request is not merge-ready until all of the following are true:

1. The implementation and user-visible options match the relevant design contract.
2. Unit, constexpr, bounded-capacity, failure, sanitizer, and documentation tests pass.
3. Benchmarks cover every representation or option selected by that pull request.
4. Raw JSON from the M5 Pro and Zen 5 machines is attached or checked in and validates against the
   shared schema.
5. The pull-request description identifies the compared commit SHAs and summarizes statistically
   meaningful results without discarding regressions.
6. The selected implementation is not materially worse on an important workload without a stated,
   reviewed tradeoff supported by the complete measurements.
7. The branch is synchronized with its current base and its authoritative CI run is green.

## Later CI and artifact history

The initial implementation PRs establish benchmark targets and portable JSON production. A later
CI PR runs every registered benchmark test in optimized configurations, uploads the raw artifacts,
and emits a manifest even when a benchmark fails. Artifact-store ingestion is deliberately a
separate layer: it verifies schema and hashes, indexes results by commit/PR/machine/toolchain, and
supports baseline comparison and chart generation without changing benchmark execution.

The storage provider and retention policy remain deployment choices. The repository-facing
contract is provider-neutral JSON plus a manifest, so adopting Artifactory or another store does not
couple benchmark code to one service.
