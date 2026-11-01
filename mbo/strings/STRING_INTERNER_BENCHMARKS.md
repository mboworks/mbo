# StringInterner performance

[`string_interner_benchmark.cc`](string_interner_benchmark.cc) measures the current interner,
its supported backend configurations, and index and character-storage costs separately. The same
source builds with and without compiler exception support, allowing comparison of the interner's
exception policy independently of the surrounding build.

## Build and run

On the Apple M5 Pro reference machine:

```sh
bazel build -c opt --config=clang --config=opt_apple_m5 \
  //mbo/strings:string_interner_benchmark \
  //mbo/strings:string_interner_exceptions_benchmark

python3 tools/benchmark_artifact.py run \
  --component StringInterner \
  --target //mbo/strings:string_interner_benchmark \
  --config opt --config clang --config opt_apple_m5 \
  --output /private/tmp/string-interner.json \
  -- bazel-out/darwin_arm64-opt/bin/mbo/strings/string_interner_benchmark \
  --benchmark_display_aggregates_only=true

python3 tools/benchmark_artifact.py validate /private/tmp/string-interner.json
```

Use `--config=opt_zen5` instead of `--config=opt_apple_m5` on Zen 5, and use the executable path
printed by Bazel for that platform. The artifact runner captures the source revision and dirty
state, host, compiler, standard library, flags, timing controls, and every raw repetition. Its
defaults are nine randomly interleaved repetitions, one second minimum measurement, and one second
warmup. Use `--benchmark_filter` to select related families for a shorter run.

The exception-enabled executable is `string_interner_exceptions_benchmark`. Filter it with
`--benchmark_filter='^Map/(Build|SafeBuild|Duplicate|SafeDuplicate)/'` to compare publication rollback
policies in one translation unit. The normal executable makes the corresponding comparison with
compiler exceptions disabled. Dependencies retain their normal repository build flags.

Run the executable smoke test with:

```sh
bazel test //mbo/strings:string_interner_benchmark_test
```

It runs representative families once, checks each registered configuration's insertion,
duplicate, lookup, and ID-access semantics before timing, and compiles iteration of non-copyable
mapped values. It verifies execution rather than asserting machine-dependent latency.

## Workloads and interpretation

| Family          | Measures                                                                       |
| --------------- | ------------------------------------------------------------------------------ |
| `Build`         | Construction, shuffled unique insertion, and destruction                       |
| `BuildReserved` | Same lifecycle, including up-front index reservation                           |
| `Hit` / `Miss`  | Shuffled string lookup after population                                        |
| `Duplicate`     | Insertion of existing strings after population                                 |
| `Unique`        | New strings inserted into an existing population; setup/destruction excluded   |
| `Get`           | Shuffled ID-to-element access                                                  |
| `GetString`     | Shuffled ID-to-string access                                                   |
| `Iterate`       | Sequential reference traversal; observes element addresses without copying     |
| `Map`           | Mapped values, non-copyable reference traversal, and publication rollback      |
| `Parent`        | Root hits, local hits, and misses across parent depth and per-layer population |
| `Storage`       | Character-storage construction, copies, and destruction without an index       |
| `Hash`          | Hashing input views without storage or indexing                                |
| `Error`         | Recoverable character-storage exhaustion and dense-ID exhaustion               |
| `Counts`        | Hash/equality invocations per operation; instrumented, not a latency baseline  |
| `Layout`        | ABI sizes of result types, dense entries, index slots, and the interner        |
| `Key`           | Empty and embedded-NUL duplicate insertion                                     |

Backend comparisons use 4,096 keys of exactly 16, 64, or 1,024 bytes. The default population sweep
also covers 0, 1, 16, 256, and 65,536 existing strings. `Unique` adds 1,024 distinct keys to each
starting population; it pauses timing during population setup and destruction. This captures
growth boundaries during the insertion batch rather than claiming a fixed load factor.
Inputs use deterministic `mt19937_64` generation and shuffled query order. Ordinals are encoded
in the first eight bytes, so the queries also
exercise embedded NULs. A miss differs in its last byte. Input ownership and generation are outside
timed loops. Lookup setup and destruction are excluded; lifecycle benchmarks include both.
Divide a batched benchmark's CPU time by its key count, or use `items_per_second`, to compare
per-operation costs. Single-operation `Error` and `Key` rows already report per-operation timing.
These are amortized throughput measurements over independent operations, not a dependency chain
measuring serialized lookup latency. They do not by themselves measure cold-cache behavior or
worst-case growth latency. Short diagnostic runs are marked suspect in artifact metadata; selecting
defaults still requires the longer protocol and both reference machines.

`Default` uses `StringIdU32<>`; `Plain` changes only the ID to `std::uint32_t`. `Optional` and
`Status` change the insertion-result policy; `StlLookup` changes the reference-result policy.
`Vector`, `Deque`, and `List` change the dense sequence. `Owning` uses a node hash map with owning
string keys; `Unordered` uses `std::unordered_map`. `LargeArena` compares 64-KiB initial and 1-MiB
maximum character blocks with the current 4-KiB/64-KiB storage defaults. These are comparison axes;
they do not change production defaults.

`SetIndex` uses a key-only flat hash set, exposing the current linear scan needed to recover an ID.
`Colliding` gives every key the same hash and measures collision stress with full string comparison.
`DirectIndex` measures a flat map referencing original input keys, so it is a component comparison
rather than a perfectly equivalent allocation layout.

Parent names have arguments `depth/key_bytes/strings_per_parent/local_strings`. They sweep 0, 1,
2, and 4 parent links and 0, 1, 32, or 256 strings per layer. Additional cases compare
populated parents with an empty child, empty parents with a populated child, and 4,096 strings per
parent with 32 local strings. Each layer owns distinct keys of the requested
length. Root/local hits are registered only when the corresponding layer is populated; misses
also exercise entirely empty chains. Cases with at least 32 entries per layer avoid tiny-table
behavior that could otherwise hide repeated hashing.

`Counts/Unique` reserves sufficient index capacity before measuring 2,048 additional keys. It uses
one iteration because later passes over those keys would measure duplicate insertion. Neither
instrumented counts nor the single-iteration `Layout` row provide reliable latency samples.

This harness provides diagnostic evidence on its running host. The complete reference-machine
protocol and requirements for selecting defaults remain in
[`INTERNING_IMPLEMENTATION.md`](INTERNING_IMPLEMENTATION.md).

## Performance analysis of the current implementation

### M5 diagnostic observations, 2026-11-01

Raw, validated artifacts are retained as compressed JSON:
[baseline](measurements/2026-11-01-apple-m5-pro/baseline.json.xz),
[population/parent sweep](measurements/2026-11-01-apple-m5-pro/populations.json.xz), and
[exception-enabled comparison](measurements/2026-11-01-apple-m5-pro/exceptions.json.xz).
These were collected from the worktree containing this harness, based on production revision
`78fd976284b38ace895998b655555764b8e3d623`; the artifact records the dirty source state. LLVM 23.1.4,
libc++ 22.1.6, `-c opt`, `-mcpu=apple-m5`, and nine randomly interleaved repetitions were used.
The baseline and exception comparison ran after compiler/lint activity stopped, with 0.1-second
minimum samples and 0.02-second warmup. The wider population sweep used shorter windows while
two lint workers were active; its metadata records that limitation. All artifacts are marked
diagnostic/suspect and do not establish cross-platform defaults.

The following values are median amortized CPU nanoseconds per operation. The 4,096-key cases use
16-byte keys unless another length is shown. Build rows include construction and destruction;
lookup/ID/iteration rows exclude population and destruction. Differences below a few percent are
not evidence for choosing a representation in these short runs.

| Workload                        | Current default | Comparison                          |
| ------------------------------- | --------------- | ----------------------------------- |
| Build, 4,096 keys               | 37.65 ns/key    | 22.62 ns/key, pre-reserved index    |
| Build, 64-byte keys             | 46.19 ns/key    | 29.88 ns/key, pre-reserved index    |
| Build, 1,024-byte keys          | 183.36 ns/key   | 118.43 ns/key, pre-reserved index   |
| Build, one 16-byte key          | 103.85 ns       | 112.45 ns, pre-reserved index       |
| String hit, 4,096 keys          | 6.85 ns         | 4,251 ns, key-only set index        |
| ID access, 4,096 keys           | 0.785 ns        | 2,112 ns, list-backed entries       |
| Reference iteration, 4,096 keys | 0.527 ns/item   | 2,108 ns/item, list-backed entries  |
| Duplicate insertion, plain IDs  | 9.77 ns         | 9.78 ns optional; 10.48 ns StatusOr |

Reservation is the clearest immediately available throughput improvement at the measured larger
population. It does not help the measured one-key case. The set/list results confirm algorithmic
costs rather than small code-generation differences. The measured optional insertion result does
not improve duplicate insertion despite dropping detailed errors; StatusOr was slightly slower
than expected here, so there is no basis for changing the default result policy for speed.

For 32 strings per layer, the baseline parent sweep gives:

| Parent links | Root hit | Local hit |     Miss |
| -----------: | -------: | --------: | -------: |
|            0 |  7.23 ns |   7.23 ns |  6.29 ns |
|            1 | 11.30 ns |   7.20 ns | 10.79 ns |
|            2 | 15.68 ns |   7.23 ns | 15.95 ns |
|            4 | 22.68 ns |   7.86 ns | 22.32 ns |

The diagnostic population sweep also shows 16-byte root hits rising from about 3.1 ns with one
existing string to 6.9 ns with 4,096 strings and 11.5 ns with 65,536 strings. This separates the
tiny-table case from ordinary lookup and the larger memory working set. It does not make parent
depth an argument for a more elaborate ancestry structure: local hits remain local.

Under `-fexceptions`, mapped `uint64_t` build cost was about 35.1 ns/key in exception-free mode and
35.0 ns/key with safe publication. That difference is within noise, consistent with eliminating
rollback for trivial publication. It does not measure a value constructor that can actually throw.
Hash counters independently confirm two hashes per ordinary unique insertion without growth,
one per ordinary hit/duplicate, and zero for ID access.

To validate an archived artifact, decompress it and pass the resulting JSON to the runner's
`validate` subcommand; the content hash covers the complete metadata and raw repetitions.

### Source analysis

The highest-value optimization is preserving preparation across lookup and insertion.
[`StringInternerBase::Insert`](internal/string_interner_base.h) first calls `rfind(key)`, which
prepares and searches each visible index. On a miss it reserves a dense slot, copies the characters,
prepares the local index again, and inserts. In a populated, non-tiny root table this hashes a new
key twice even without index growth. Growth can additionally hash existing keys. An ordinary hit
or duplicate insertion hashes once; ID access does not hash. Empty and tiny tables can take special
paths that avoid hashing altogether. The instrumented `Counts` rows measure the non-tiny case.

The existing `Prepare`/`Find`/`Insert` policy interface does not carry the preparation from the
initial search into insertion. A faster interface needs a query token containing a reusable hash
and, where supported, probe/insertion state. Character copying must rebind its key view to stable
storage without recomputing the hash. If commit grows the index, it must invalidate the old bucket
or probe state and retry with the retained hash. An STL iterator hint alone does not establish any
of these guarantees. In the pinned Abseil version, `emplace_hint` ignores its hint and the deprecated
`find(key, hash)` overload ignores its hash argument. Its experimental `lazy_emplace` also requires
the construction callback to construct the entry; simply returning on arena exhaustion violates
that API's contract. A recoverable preparation/commit interface needs deliberate backend support.

### Costs by operation

| Configuration / operation         | Current expected cost, excluding string length  |
| --------------------------------- | ----------------------------------------------- |
| Map-backed root string lookup     | Average constant-time index lookup              |
| Map-backed root unique insertion  | Two searches plus storage and occasional growth |
| Cascading lookup / insertion miss | One search per visited layer                    |
| Segmented/vector/deque ID access  | Constant-time local access plus ancestor walk   |
| List-backed ID access             | Linear traversal from the local sequence start  |
| Key-only set index hit            | Hash lookup followed by linear ID recovery      |
| List-backed whole iteration       | Quadratic local traversal                       |

String hashing, copying, and full equality comparisons add length-dependent work. Hash tables can
also degrade to linear probing under collisions; the `Colliding` cases expose that behavior.

[`ElementAt`](internal/string_interner_base.h) uses `std::ranges::next(begin, ordinal)`.
This is constant time for the default `SegmentedVector`, `vector`, and `deque` because their
iterators are random access. Replacing it with `operator[]` does not fix a default linear scan:
there is no such scan. A `list` needs a separate positional directory for constant-time ID access.
Interner iterators currently call `ElementAt` on every dereference, restarting list traversal each
time. Carrying the underlying sequence iterator across increments would restore linear list
iteration and reduce repeated ancestor walks for all entry types.

A key-only set index cannot return a dense ID. `FindLocal` therefore scans dense entries and
compares strings after a successful set lookup. It also makes duplicate insertion linear. The fast
setup should retain an ID-bearing index, even when the public interner is a set. Changing a
container from a map to a set saves slot bytes but currently loses the central lookup guarantee.

### Priorities for a faster setup

1. Preserve one hash across the missing-key search, stable character copy, and index insertion.
   Split query hashing from container-specific probing. Reusing a hash across parents is valid
   only when their hasher states are compatible; equal hasher types alone are insufficient.
2. Reserve the index for the expected population. The current `options_type` can accept an empty,
   pre-reserved container; reservation itself is included in `BuildReserved`. A supported capacity
   option could also reserve dense storage through its backend, rather than requiring callers to
   construct container objects. Retain the empty-index invariant.
3. Keep an ID-bearing hash index in the fast configuration. For key-only compatibility backends,
   add an explicit ID recovery strategy instead of scanning entries.
4. Keep `SegmentedVector` as the stable-reference comparison baseline. A vector can reduce local
   access cost but relocates entries on growth, including mapped values. A list needs an auxiliary
   directory or a different complexity contract. Tune segment size and directory reservation
   against population and entry size before changing either default.
5. Benchmark a specialized append-only hash backend with recoverable prepare/commit and growth.
   Candidates include cached full hashes, compact ID-only buckets that resolve keys through dense
   storage, and a shared dense-entry/index record to reduce duplicate metadata. They trade hash and
   equality work against memory footprint and pointer indirections. The existing Abseil default is
   already a flat probing table; a scalar modulo table is not automatically faster.
6. Reduce per-layer work where shallow-parent measurements justify it. Empty layers can be skipped;
   hashes may be shared when compatible. Snapshot-prefix checks must remain intact. More elaborate
   ancestry directories, inherited caches, or persistent indexes need evidence even at depth four;
   they add memory and maintenance work that root/local-hit workloads may not recover.

Index preparation can also reject exhausted bounded indexes before character copying. Currently
an index rejection can leave an unused character copy behind; repeated failed attempts consume
arena space. The append-only lifetime contract does not require reclaiming those copies, but a
backend that reports capacity failure during preparation can avoid making them.

The current options constructor does not enforce its empty-container precondition. Importing a
populated index does not populate the dense entries, so it is unsupported even though the type
system accepts it. The benchmark always supplies an empty index, including the reserved case.
Capacity-option work should enforce this invariant rather than expanding the accepted constructor
arguments further.

Small-object construction deserves separate attention: the default segmented sequence initially
reserves one directory pointer, and the first entry and first characters each acquire a block.
Population-sensitive measurements should decide whether inline entries/characters or lazy directory
reservation are useful. Reducing object size or allocations for a one-string interner does not
necessarily improve steady-state throughput for a large one.

### Result policies, exceptions, and memory

Result policies are compile-time types with no virtual dispatch. The fallible index-result branch
and exception rollback branch are selected with `if constexpr`; the default direct-result,
exception-free specialization does not contain an extra runtime policy selection. Lambdas used
for publication do not allocate. Removing policy abstractions or force-inlining everything has
no demonstrated benefit; generated code and complete workloads should decide those changes.

The ABI matters more than a general claim that one result API is faster. `Layout` records actual
sizes, including padding. In the initial M5/libc++ run, the expected insertion result occupied
8 bytes, the optional result 12, and the status result 16. `OptionalRef` occupied one pointer while
the STL optional reference wrapper occupied two pointer-sized words. These figures are specific
to the measured types and ABI. None makes throwing result access necessary: the interner's normal
paths use checked results and non-throwing construction. Error reporting does not eliminate
allocation failure in STL backends that lack a recoverable allocation interface.

The interner exception-free policy suppresses its publication rollback guard even in an
exception-enabled translation unit. It does not change the exception behavior of a container,
hasher, allocator, or value constructor. Such callers must obey that mode's no-throw contract;
otherwise an unpublished index entry can remain after a value-construction exception. Supporting
throwing values requires the safe policy. For trivial non-throwing publication the optimizer may
already eliminate the safe guard, so measure both modes rather than assuming a fixed benefit.

On the measured 64-bit ABI the view-to-ID map slot is 24 bytes and a dense set entry is 16 bytes,
before hash-control bytes, spare capacity, segment headers, arena headers, and characters. A mapped
`uint64_t` dense entry occupies 32 bytes because it contains an optional key/value pair. Compact
records can reduce cache traffic, but removing optional state requires another way to reserve and
reuse an unpublished entry without constructing a user value prematurely.

Arena allocation and copying are a distinct cost. Larger character blocks can reduce allocation
frequency, but do not fix repeated hashing or probing and can retain more unused memory. The
current byte-aligned, length-aware storage already avoids terminator work and allocating empty
strings. Dropping embedded-NUL support does not remove any scan in this implementation.

Cached hashes should normally reject mismatches before full key comparison. Treating equal hashes
as equal strings changes correctness: even a wide hash can collide. Any trusted-hash mode must be
explicit, and cannot be used to justify a faster exact-interning default. Growth should reuse cached
hashes where available and benchmark both average throughput and the rehash latency spikes.

Further candidates need additional workloads: batch preparation/prefetch to overlap independent
lookups, mixed hit/miss and duplicate ratios, mixed string lengths, segment-size sweeps, and
explicit measurements immediately around growth thresholds. The current synthetic queries do not
establish a winner for real mbo/xff/proto distributions. A prepared-hash API must also retain the
tiny-table shortcut: computing a hash eagerly can regress empty or single-entry indexes that
currently avoid hashing. A cached full hash mainly helps reject non-matching candidates and reuse
rehash work; successful exact lookups still need string equality unless a canonical identity is
already known. Callers with an ID should use ID access directly.
