# StringInterner benchmark harness

`//mbo/strings:string_interner_benchmark` is a manual, test-only Google Benchmark binary.
This is the initial harness, not the final benchmark report. Run final comparisons only after
the complete implementation stack has passed local and own-context CI validation, following
[`INTERNING_IMPLEMENTATION.md`](INTERNING_IMPLEMENTATION.md). Compilation alone does not select
a winning configuration.

The initial matrix includes:

- flat and node HAMT fragment widths 4, 5, 6, and 7;
- standard unordered, Abseil flat, and Abseil node index adapters;
- the same `mbo::hash::DefaultHasher` 64-bit hash across the initial index comparison;
- 32-bit dense IDs in the index comparison, plus 8-, 16-, and 64-bit ID profiles for flat 5-bit HAMT;
- 64 and 1,024 unique strings, with exact lengths of 16, 64, and 512 bytes;
- ordinary bytes and an embedded NUL in the final byte, without truncating string views;
- unique insertion lifecycle, duplicate insertion, and forward/reverse parent, local, and missing
  lookups through chains of depth 1, 2, 8, and 32.

Every index/width profile also has explicit empty-string lifecycle, parent duplicate, forward
lookup, and reverse lookup cases. Setup verifies that ID zero is a valid result and the empty
string reserves no character bytes. These cases still allocate index/control storage; zero
character reservation is not a claim that the whole operation is allocation-free.

Additional `Fold32` profiles exercise flat HAMT fragments 4–7 and node HAMT with a 32-bit hash
result. They use mbo's XOR-folding of the same 64-bit default hash, not a native 32-bit algorithm;
the hash computation and reduction costs are included. A folded-hash profile with 64-bit IDs
demonstrates that the dimensions remain independent. Every row reports both `hash_bits` and
`id_bits`; do not describe a hash-folding comparison as a native 32-bit hash performance result.

`FindMixed` and `RfindMixed` alternate root hit, leaf hit, and miss in equal
proportions. Query construction and expected-presence checks happen outside
timing. Each iteration performs `3 * strings_per_level` operations; normalize
with that explicit batch size. At depth one the two hit classes coincide. This
deterministic balanced workload is not a claim to model arbitrary production
hit distributions or randomized request order.

Cascade depth includes the root. The total string count stays fixed and is divided equally across
levels; changing depth therefore changes both traversal distance and local index size. The
`strings_per_level` counter makes that distinction explicit. Parent-hit cases query the root's
strings; local-hit cases query the leaf's strings. At depth one those hit workloads coincide.
Fixtures explicitly destroy descendants first rather than relying on vector destruction order,
including when setup fails partway through a chain.

The `id_bits` counter records ID width for every case. Changing ID width does not change the hash function.
Registration omits input counts outside that ID representation's capacity rather than timing
setup failures as successful insertions; the 8-bit profile currently uses only the 64-string cases.

Input generation and cascade population happen before lookup timing. Setup checks uniqueness and
both lookup directions verify the hit/miss workload before timing. `UniqueLifecycle` deliberately
includes container construction, insertion, failure checks, and destruction: it is not pure
insertion latency. Duplicate and lookup cases operate on already populated containers. Times are
reported per batch; use the items-processed counter to normalize them per operation. Unique
lifecycle also reports bytes processed. Cascade character reservation is a partial storage
counter, not total process memory, index memory, or peak allocation.

Names carry the index configuration, operation, string count, string length, and embedded-NUL mode.
The binary records the hash, ID width, language baseline, and compiler version in raw benchmark
context. The shared artifact runner must add complete machine, build, Git, timing, and command
provenance; this context alone is not sufficient for a publishable dataset.

For a registration-only check, build the target and invoke the binary with
`--benchmark_list_tests=true`. This enumerates 8,036 cases without running their timed loops.
The manual target is explicitly registered for the CI-owned clang-tidy compilation database.

A one-iteration sanitizer smoke run may validate fixture setup, hit/miss preflight, and cleanup
before final measurements. Such debug, instrumented results are validation data only: retain them
separately and mark them nonpublishable, rather than folding their timings into a performance report.

Before the final report, extend the matrix with
character/dense-storage configurations, capacity exhaustion, distribution and access-order
variations, and detailed memory diagnostics. These initial sequential synthetic scans must not be
presented as all representative mbo, xff, or proto workloads. Measurements need nine interleaved
repetitions, warmup, raw JSON, charts regenerated from that JSON, and a provisional configuration
decision matrix. AMD Zen 5 measurements follow later and may change the recommendations.

## Retained-result reporting

Run from the repository root, using new output paths:

```sh
python3 -m tools.benchmark_report retained.json --summary summary.json \
  --svg comparison.svg --name 'EXACT_CASE_NAME' --operations-per-iteration 64
```

Repeat `--name` to select comparable cases. Output files must not already exist;
the retained input is never overwritten. Use the actual batch size, not 64 by
default: empty-string cases perform one operation per iteration.

`tools.benchmark_report.summarize` validates the immutable artifact checksum and
requires valid measurements from clean sources and an optimized build. Every case
must contain at least nine uniquely indexed raw repetitions; Google Benchmark
aggregate rows cannot substitute for observations. Sanitizer smoke results are
not publishable performance datasets.

Summaries retain median, minimum, maximum, and the mean of the best three samples.
The default unit is CPU nanoseconds per iteration. Pass the explicit
`operations_per_iteration` batch size to report CPU nanoseconds per operation;
never compare differently sized batches without normalization. Generate separate
reports when batch sizes differ. `render_svg` takes an explicit list of up to
twenty case names, draws median bars and observed min/max whiskers, and embeds
the source artifact checksum. Whiskers are not confidence intervals. Summaries
preserve complete measurement provenance (host, toolchain, flags, source,
timing, and controls); charts also identify CPU, compiler, standard, and commit.
Keep the raw artifact alongside the generated report and chart. This helper does not
establish that selected workloads are comparable; that remains part of the
documented experiment design.
