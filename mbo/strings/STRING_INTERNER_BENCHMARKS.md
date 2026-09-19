# StringInterner benchmark harness

`//mbo/strings:string_interner_benchmark` is a manual, test-only Google Benchmark binary.
The retained initial-host report, raw artifacts, charts, and provisional configuration matrix are
in [`measurements/STRING_INTERNER.md`](measurements/STRING_INTERNER.md). Additional comparisons
remain necessary before cross-machine or universal conclusions. Follow
[`INTERNING_IMPLEMENTATION.md`](INTERNING_IMPLEMENTATION.md); compilation alone does not select a
winning configuration.

The initial matrix includes:

- flat and node HAMT fragment widths 4, 5, 6, and 7;
- standard unordered, Abseil flat, and Abseil node index adapters;
- the same `mbo::hash::DefaultHasher` 64-bit hash across the initial index comparison;
- 32-bit dense IDs in the index comparison, plus 8-, 16-, and 64-bit ID profiles for flat 5-bit HAMT;
- 64 and 1,024 unique strings, with exact lengths of 16, 64, and 512 bytes;
- ordinary bytes and an embedded NUL in the final byte, without truncating string views;
- unique insertion lifecycle, duplicate insertion, and forward/reverse parent, local, and missing
  lookups through chains of depth 1, 2, 8, and 32.

The representative map matrix compares flat and node HAMT, standard unordered-map, and Abseil flat
and node indexes with a `uint64_t` mapped value. `StringInternerMap/UniqueLifecycle` includes mapped
object construction and destruction in addition to string interning. `FindMapped` performs the
content lookup followed by dense-ID mapped-value resolution. `Iterate` reads both the key view and
mapped value in dense-ID order. These cases distinguish the map composition cost from the set-only
core without charging set users for unused mapped storage. Cascading map semantics and stable
mapped addresses remain correctness requirements covered by tests; the initial performance matrix
isolates local mapped storage so index and mapped-sequence costs can be compared directly.

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

`FindMixedShuffled` and `RfindMixedShuffled` use the same balanced queries shuffled once during
untimed setup with `std::mt19937` seeded with `0x4d424f`. Every timed batch reuses that order;
there is no timed random-number generation. This controls request order within one standard
library implementation, not a portable byte-for-byte shuffle sequence or a production traffic
model. Hashing, hit ratios, query count, and cascade population stay unchanged.

`FindRootHeavyShuffled`, `FindLocalHeavyShuffled`, and `FindMissHeavyShuffled`, plus
their `Rfind` counterparts, vary the root/local/miss proportions to 80/10/10,
10/80/10, and 10/10/80 percent. They reuse the same mixed-lookup core, fixed-seed
untimed shuffle, and population. Each batch performs `10 * strings_per_level`
queries, including repeated requests for each string. The explicit
`queries_per_iteration` and three query-fraction counters identify the workload
and normalization. These are controlled sensitivity experiments, not measured
production distributions; at depth one root and local hits still coincide.
Before shuffling, preflight checks both presence and the exact dense ID for every
root and leaf request, in both search directions. This validation is outside timing.

Cascade depth includes the root. The total string count stays fixed and is divided equally across
levels; changing depth therefore changes both traversal distance and local index size. The
`strings_per_level` counter makes that distinction explicit. Parent-hit cases query the root's
strings; local-hit cases query the leaf's strings. At depth one those hit workloads coincide.
Fixtures explicitly destroy descendants first rather than relying on vector destruction order,
including when setup fails partway through a chain.

The `id_bits` counter records ID width for every case. Changing ID width does not change the hash function.
Registration omits input counts outside that ID representation's capacity rather than timing
setup failures as successful insertions; the 8-bit profile currently uses only the 64-string cases.

Separate `IdExhaustion` and `DuplicateAtIdCapacity` cases prepopulate all 256 eight-bit IDs.
Each timed iteration attempts one missing string or one existing string, respectively.
Untimed preflight verifies failure or duplicate success and unchanged size. These measure ID
exhaustion, not arena exhaustion: duplicates remain valid when no new ID can be assigned.

`EntryExhaustion` and `DuplicateAtEntryCapacity` use 32-bit IDs and a descriptor sequence capped
at 64 entries. The shared capacity core performs one attempt per iteration after untimed setup
and preflight. The `capacity` counter identifies the saturated entry count; the case name identifies
whether ID space or descriptor storage supplies the bound.

`IndexExhaustion` and `DuplicateAtIndexCapacity` cap the flat 5-bit HAMT at 64
keys through its maximum-size option, retaining 32-bit IDs and default character
and descriptor storage. The same capacity core verifies `kIndexExhausted`,
rollback of staged descriptors/character copies, and successful duplicate IDs.
This isolates index cardinality exhaustion, not exhaustion of a node allocator
or control source; those allocation bounds require separate configurations.

`CharacterExhaustion` and `DuplicateAtCharacterCapacity` use one 4,096-byte inline
arena source, 32-bit IDs, the default descriptor sequence, and the flat 5-bit HAMT.
Untimed setup inserts until the specific character-storage exhaustion error occurs.
The actual entry capacity depends on string length and arena metadata, so it is
reported rather than assuming all 4,096 bytes are character payload. The failed
next string or last successful duplicate enters the same capacity timing core.
Character used/reserved counters distinguish payload from the source bound.
Only character storage is inline/bounded: descriptor directories and HAMT
nodes/control storage may still allocate. This is not a whole-interner no-heap profile.

Untimed detailed-result preflight checks the specific ID/descriptor exhaustion
reason, or the duplicate's exact existing ID and false insertion flag. Preflight
and post-timing checks require unchanged size and committed character bytes;
descriptor failure must roll back the staged character copy. These checks stay
outside the timed optional-ID adapter loop.

Input generation and cascade population happen before lookup timing. Setup checks uniqueness and
both lookup directions verify the hit/miss workload before timing. `UniqueLifecycle` deliberately
includes container construction, insertion, failure checks, and destruction: it is not pure
insertion latency. Duplicate and lookup cases operate on already populated containers. Times are
reported per batch; use the items-processed counter to normalize them per operation. Unique
lifecycle also reports bytes processed. Cascade character reservation is a partial storage
counter, not total process memory, index memory, or peak allocation.

Cascade cases also record live and reserved descriptor bytes, lookup-directory reservation,
and segment-directory reservation, summed across the constructed chain during untimed setup.
These are the SegmentedSequence diagnostics, not allocator bookkeeping or whole-process memory.

Names carry the index configuration, operation, string count, string length, and embedded-NUL mode.
The binary records the hash, ID width, language baseline, and compiler version in raw benchmark
context. The shared artifact runner must add complete machine, build, Git, timing, and command
provenance; this context alone is not sufficient for a publishable dataset.

For a registration-only check, build the target and invoke the binary with
`--benchmark_list_tests=true`. This enumerates 24,576 cases without running their timed loops.
The manual target is explicitly registered for the CI-owned clang-tidy compilation database.

Storage profiles hold the flat HAMT at five fragment bits and change one storage setting at a
time: `Arena512` and `Arena16384` change the initial character block size from the default 4,096
bytes, retaining the default doubling growth and maximum block size. `Entries64` and `Entries1024`
change descriptor segment capacity from the default 256 views. They reuse the same benchmark core,
inputs, preflight, and cascade lifetime handling as the default profile. Compare lifecycle cases
for allocation effects and populated lookup cases for layout effects; these are candidates, not
measured recommendations.

`Iterate` and `ReverseIterate` traverse every visible string in dense-ID order and reverse order,
respectively, across the same cascade depths. Both orders are checked against the original inputs
before timing. These cases measure string-view traversal, not hashing or string-byte scanning;
normalize each batch by the total string count. Descriptor layout and ancestor depth may affect
them differently from hash lookup.

`VisitStringSizes` measures explicitly requesting the cold-path diagnostic visitor
across the visible chain, using a nothrow collector that counts strings and sums
their lengths. Untimed preflight checks both totals. Each batch visits the total
string count and reports `string_bytes_reported`, not bytes scanned: the visitor
reads descriptors, not string contents. The escaped leaf pointer and per-batch
memory barrier prevent whole-walk hoisting. This measures an explicit diagnostic
request, not a diagnostics-enabled insertion mode or proof of hot-path overhead;
no insertion or lookup counters are introduced by this benchmark.

`FindLateParent` and `RfindLateParent` create the normal cascade, then append
`strings / depth` new strings to its root during untimed setup. At depths greater
than one those strings must remain invisible to the leaf despite existing in
the root's current index. Preflight checks both directions and frozen visible
size. Depth one has no child cutoff and is explicitly a successful root-only
control, including exact new IDs. `hidden_parent_growth` distinguishes the control
from cutoff misses; do not combine them as one miss workload. Each batch queries
all late strings, and counters record late count, visible size, and total retained
chain storage. This isolates cutoff filtering after parent growth, not ordinary
missing-key lookup or a chain populated before its child was created.

A one-iteration sanitizer smoke run may validate fixture setup, hit/miss preflight, and cleanup
before final measurements. Such debug, instrumented results are validation data only: retain them
separately and mark them nonpublishable, rather than folding their timings into a performance report.

Before the final report, extend the matrix with additional character/dense-storage configurations,
capacity exhaustion, distribution and access-order
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
