# Hash-assisted trie design study

This document records the decision between two distinct hash/trie families: HART and HAMT. They
must not be conflated merely because their acronyms and use of hashes are similar.

HAMT is the implemented target. HART remains documented as a separate concurrency-oriented
research option, but is not planned for implementation. Neither is a prerequisite for
`SegmentedSequence` or the arena. StringInterner can use HAMT through its replaceable index
contract; benchmarks still decide which index configuration should be recommended for each
representative workload.

## Structures under consideration

### HART

[HART](https://doi.org/10.1109/IPDPS.2019.00100) is the concurrent Hash-Assisted Radix Tree described
by Pan, Xie, and Song for DRAM/persistent-memory hybrid systems. The published
[reference implementation](https://github.com/CASL-SDSU/HART) hashes a key prefix to select a
sub-radix tree and uses an adaptive radix tree for the remaining key. It exposes exact insertion,
search, and deletion, plus radix-tree minimum/maximum operations.

Its original design includes concerns that may not transfer to mbo's ordinary in-memory use:
placement across DRAM and persistent memory, persistence ordering, crash recovery, logging, and
concurrency. An mbo experiment must isolate the hash-assisted radix organization from those
platform-specific mechanisms and must not claim HART behavior while silently omitting guarantees
central to the paper.

HART's principal attraction is concurrent access. That specialization does not currently match an
established mbo requirement closely enough to justify its substantial publication, synchronization,
memory-reclamation, and verification complexity. mbo therefore does not plan to implement HART.
This decision can be revisited only for a concrete multithreaded workload where HART has a measured
advantage over simpler structures.

### HAMT

A [hash array mapped trie](https://en.wikipedia.org/wiki/Hash_array_mapped_trie) consumes fixed-size
fragments of a key's hash at successive trie levels. Bitmap-compressed nodes store only occupied
slots. Phil Bagwell's
[Ideal Hash Trees](https://infoscience.epfl.ch/record/64398/files/idealhashtrees.pdf) is the
foundational design.

HAMTs are particularly relevant in persistent form: updates path-copy changed nodes while sharing
the unchanged structure. This may align naturally with string-interner parent/child snapshots,
where a child retains the parent's index root and adds branch-local strings without observing later
parent mutations. The planned container supports both persistent and transient modes, with an
explicit transition between them so bulk construction need not pay unnecessary path-copying cost.

The implementation must be complete, correct, production-quality, and benchmarked. Incomplete
prototype implementations are not design targets. They may suggest representation ideas, but do
not reduce mbo's requirements for correctness, completeness, robustness, or measured performance.

HAMT is a general `mbo/container` facility regardless of whether it ultimately wins the
StringInterner index benchmarks. StringInterner may consume it through a compatible-index contract;
HAMT is not a restricted or string-specific implementation detail.

StringInterner does not expose HAMT iterators. Its public iteration follows dense IDs through its
separate stable ID-to-view sequence, while HAMT is only a replaceable content-to-ID lookup index.
Consequently, StringInterner requires stable character storage and stable dense-sequence iteration,
not stable transient HAMT iterators. HAMT iterator stability is a general-container design choice
and must justify its own speed and memory cost.

## Potential roles

| Role                            | HART fit                                      | HAMT fit                                       |
| ------------------------------- | --------------------------------------------- | ---------------------------------------------- |
| Mutable string-to-ID index      | Exact key lookup; long-key radix locality     | Hash-fragment lookup; collision nodes          |
| Persistent child snapshot       | Not inherent in the published organization    | Structural sharing is a central strength       |
| Concurrent mutable index        | Central to the published HART                 | Requires a separate concurrency design         |
| Ordered/prefix string operation | Radix subtrees may help, but hashing obscures | Hash order discards lexical prefix structure   |
| Segment directory               | No published predecessor/range lookup         | No natural cumulative-boundary lookup          |
| Deterministic ID iteration      | Requires a defined traversal                  | Requires a defined traversal or separate table |

Neither known structure directly solves `SegmentedSequence` index-to-segment location. That
operation asks for the segment containing a cumulative index, effectively a range or predecessor
query. The published HART implementation provides exact lookup rather than this operation, and a
HAMT indexes hash fragments rather than ordered boundaries.

## HAMT container contract

The mbo HAMT should be expressed through behavioral concepts for hash, equality, key access, value
storage, and node allocation. It must specify:

- both map and set forms over common internal machinery, plus heterogeneous lookup;
- both flat and node storage implementations for benchmarks, exposing both publicly only if each
  provides a material advantage for a relevant workload;
- full-hash collisions and unequal-key resolution;
- non-throwing hash and equality operations as part of the public concepts;
- immutable keys after insertion;
- persistent iterators exposing only const elements; transient map iterators may mutate mapped
  values but never keys;
- stateful hash and equality objects, including their seeds, stored in the container and preserved
  exactly across persistent copies and transient conversions;
- mutation and deletion in both transient and persistent modes, including path-copying persistent
  deletion and in-place mutation of uniquely owned transient nodes;
- value-semantic persistent mutation that returns a new container and leaves the source unchanged;
- persistent insertion returning `{new_container, inserted}` and persistent erasure returning
  `{new_container, erased}`;
- STL-like in-place transient mutation returning conventional iterator/bool results or counts;
- persistent maps exposing only const mapped access and producing a new persistent map for mapped
  updates; transient maps provide mutable mapped access and `operator[]` when the mapped type is
  default constructible under the non-throwing construction contract;
- a consuming transient-to-persistent conversion as the baseline fast path: `persistent() &&`
  invalidates the transient structurally and preserves in-place edit ownership until conversion;
- no repeated snapshot API on the hot path unless benchmarks show that its edit-token rollover and
  subsequent copy-on-write costs are justified;
- forward iterators that retain container identity for valid comparison but do not own or extend
  the lifetime of a persistent snapshot;
- iteration order without an unmeasured deterministic-order guarantee; relevant research and
  benchmarks determine whether a stronger guarantee has enough value to expose;
- iterator/reference invalidation;
- copy, move, swap, and allocator/block-source propagation;
- bounded and allocation-failure behavior;
- allocator/block-source selection as part of the template contract from the beginning, including
  bounded and no-additional-allocation arena-backed configurations;
- an explicit deep `clone_to(source)` operation for changing allocation domains; ordinary copies,
  persistent mutations, and transient conversions stay inside their existing ownership domain and
  do not add per-node source metadata;
- explicit lightweight `try_insert`, `try_emplace`, `try_erase`, and persistent equivalents for
  bounded operation, with an error enum rather than exceptions or a heavyweight status type;
- `try_*` errors initially limited to `allocation_exhausted` and `max_size_exceeded`, with
  source-specific exhaustion mapped to the former;
- the strong mutation guarantee: failed allocation leaves the original persistent value or
  transient container unchanged;
- supported mutation paths require non-throwing hash, equality, key construction, and value
  construction; the implementation does not carry rollback machinery for throwing user code;
- transient-to-persistent rvalue conversion leaving the moved-from transient valid and empty, in
  keeping with ordinary moved-from container semantics;
- exception-enabled and exception-disabled operation.
- external synchronization for all mutation and transient conversion; persistent values and other
  const access may be read concurrently only while no thread mutates the same underlying transient
  state. The implementation adds no locks solely for container access. Ownership metadata used for
  persistent structural sharing may still use atomics if benchmarks select that ownership strategy.

One block-source concept drives the core implementation. Adapters provide standard allocator, PMR,
arena, and fixed-buffer integration without multiplying HAMT implementations.

The arena adapter is a required implementation component, not documentation shorthand. It must
provide multiple simultaneously live node blocks from provisioned storage. Since a monotonic arena
cannot reclaim an arbitrary node merely because `BlockSource::Release` is called, failed path-copy
operations require either checkpointed transactional rewind or a bounded reusable node-block pool.
Tests must prove that repeated failed mutations do not reduce the remaining usable capacity.

Hard-real-time use is a distinct compile-time profile. Fixed hash width bounds trie depth, while an
explicit collision bound limits full-hash equality scans. Exceeding that bound is a recoverable
insertion failure and leaves the tree unchanged. Immutable lookup performs no reference-count
mutation. Atomic snapshot retention, root publication, and reclamation are specified separately;
the presence of atomics does not by itself establish wait-free behavior.

`HamtOptions::maximum_collision_size` supplies that bound. Its default is unrestricted and compiles
out the additional lookup and collision-size check. A finite value rejects only insertion of a new,
unequal key once the terminal full-hash bucket has that many entries; reinserting an existing key
still succeeds with `inserted == false`. Rejection reports `HamtError::kCollisionLimitExceeded` and
does not change the tree.

## Implemented acceptance surfaces

The implementation deliberately separates semantic guarantees that require different public types
from resource guarantees selected through sources and constexpr options:

| Requirement                     | Implemented surface                                              | Guarantee boundary                                                |
| ------------------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------- |
| Set and map semantics           | `HamtFlatSet`, `HamtFlatMap`, `HamtNodeSet`, `HamtNodeMap`       | One shared routing, collision, mutation, and ownership machinery  |
| Persistent updates              | Value-returning `insert`/`erase`/mapped replacement              | Source snapshots remain unchanged and share untouched structure   |
| Efficient construction/editing  | Move-only nested `transient_type`                                | Consuming `persistent() &&`; failed edits preserve current state  |
| Flat locality                   | `HamtFlatSet` and `HamtFlatMap`                                  | Mutation may invalidate flat entry references and iterators       |
| Pointer-stable payloads         | `HamtNodeSet` and `HamtNodeMap`                                  | Surviving payload addresses remain stable; erased payloads expire |
| Unrestricted allocation         | `NewDeleteBlockSource` defaults                                  | Convenience modifiers fail hard if allocation cannot complete     |
| Guarded/recoverable allocation  | Fallible custom `BlockSource` plus `try_*`                       | Exhaustion is reported and mutation is non-destructive            |
| Provisioned allocation-free use | `TryCreateIn` plus bounded control and arena/fixed block sources | No general allocation after provisioning within declared budgets  |
| Bounded routing work            | Fixed hash width and finite collision option                     | String hashing/equality remains proportional to inspected bytes   |
| Diagnostics                     | `structural_diagnostics` and node visitors                       | Cold, allocation-free inspection; no hot-path counters            |
| Threading baseline              | Immutable held snapshots and external synchronization            | No mutable root publication or reclamation protocol is implied    |

The node names expose pointer stability because it changes observable semantics. Allocation mode is
not another container family: the block-source type and factory select unrestricted, recoverable,
or fully provisioned storage without creating parallel HAMT implementations. Arena-backed sources
must support multiple simultaneously live blocks and recycle failed path-copy work, so a fixed
buffer is a real bounded configuration rather than an allocation claim layered over hidden heap
state.

## Public type and policy shape

Abseil's `flat_hash_map`, `flat_hash_set`, `node_hash_map`, and `node_hash_set` establish a useful
precedent: materially different flat and node guarantees are visible in the type name, not hidden
behind a runtime switch. mbo follows that shape with `HamtFlatMap`, `HamtFlatSet`, `HamtNodeMap`, and
`HamtNodeSet` over one shared implementation. Flat and node layouts have fundamentally incompatible
reference, pointer, and invalidation semantics and cannot be selected through `HamtOptions`.

The names above denote persistent containers. Each exposes its transient form as a nested
`transient_type` returned by `.transient()`, avoiding a second set of four top-level names.

The existing `LimitedMap` and `LimitedSet` provide the blueprint for richer compile-time options.
Their `LimitedOptions` structural constexpr value combines capacity and feature flags in a value
suitable for a non-type template argument, validates the contract with a concept, and allows
compile-time branching to remove unused behavior. HAMT follows the same principles:

- a constexpr-compatible structural options value rather than runtime configuration;
- concepts that validate the options and behavioral customization types;
- compile-time selection of fragment width, internal bitmap/node representation, ownership,
  persistence, bounded behavior, and any iterator-stability strategy, but never the public
  flat-versus-node layout;
- no runtime branch or stored policy state for choices known at compile time;
- named public flat/node container families so consequential guarantees remain obvious at use
  sites;
- only benchmark-proven choices retained as supported public options.

Map and set forms are views over the same machinery, not separate implementations. Likewise,
option variation must specialize shared primitives rather than multiply core implementations.

The names distinguish configuration from behavior. `HamtOptions` is the candidate structural
constexpr value for compile-time configuration. "Policy" is reserved for behavioral customization
types where useful, such as ownership or block-allocation strategies; hash and equality retain their
familiar dedicated template roles. This matches the `LimitedOptions` precedent without forcing all
extension points into one vocabulary.

## Precise non-throwing contract

"Non-throwing" is a compile-time API constraint, not advice to users and not a promise that the
container catches exceptions:

- Every supported call to `Hash` and `KeyEqual` must be statically `noexcept` for the lookup key
  types used by that call.
- A modifier overload participates only when constructing the new key and mapped value from that
  overload's actual arguments is statically `noexcept`.
- Operations used to place or reorganize existing elements must also be statically `noexcept`.
  Node layout can normally preserve and share element nodes instead of moving their values. Flat
  layout additionally requires any moves its representation performs to be non-throwing.
- Destruction must be non-throwing. A throwing destructor is unsupported.
- These requirements apply to the expressions an operation actually evaluates, not merely to the
  declared key and mapped types. A type may therefore support one insertion overload and not
  another.
- Unsupported potentially throwing overloads are rejected during constraint checking with a
  diagnostic; the container does not surround user operations with hidden `try`/`catch`, maintain
  exception-only rollback state, or silently call `std::terminate` for them.
- `try_*` reports only block-source exhaustion and maximum-size exhaustion. It never translates a
  user exception into an error result because supported hash, equality, construction, movement,
  and destruction cannot throw.
- Allocation failure is distinct from object construction. A bounded block source reports failure
  before construction begins, allowing `try_*` to preserve the original container without
  exception machinery.

This contract has important consequences. For example, constructing a new `std::string` from
characters can allocate and is generally not `noexcept`, so an insertion overload that performs
that construction is not supported by the non-throwing HAMT API. Moving an already constructed
value may be supported when that exact move construction is `noexcept`. Allocator-aware element
types must satisfy the constraint under the allocator and operation actually selected.

Ordinary non-`try_*` modifiers treat allocation exhaustion as a hard failure through mbo's
configured requirement mechanism. Users requiring recoverable bounded operation must use `try_*`.
This keeps recovery state and result handling out of the ordinary successful path.

## Measurements required

The initial Apple M5 Pro map comparison, retained raw evidence, charts, and provisional decision
matrix are in [`measurements/HAMT.md`](measurements/HAMT.md). It covers ordinary lookup, traversal,
fresh transient fill/erase, and the two persistent-branch endpoints: editing one mapped value and
editing every mapped value. The list below remains the complete evidence envelope; entries not
covered by that initial report are still required before making broader default or
cross-architecture claims.

- successful and unsuccessful lookup across realistic and adversarial hashes;
- insertion, replacement, and deletion where supported;
- persistent snapshot creation and branch-local updates;
- transient-to-persistent conversion if both modes exist;
- transient bulk construction with consuming conversion, compared with any repeated-snapshot
  candidate;
- short and long strings, shared prefixes, and varying duplication ratios;
- full-hash collision storage using flat inline arrays and separately allocated node lists;
- storing the full hash with each entry versus recomputing it during collision handling and
  structural changes;
- node count, pointer count, bitmap density, padding, allocated bytes, and fragmentation;
- 4-, 5-, 6-, and 7-bit hash fragments; 5 bits gives a 32-way bitmap in one 32-bit word, 6 bits
  gives a 64-way bitmap in one 64-bit word, while 4 trades smaller nodes for greater depth and 7
  requires multiword occupancy metadata or a wider representation;
- CHAMP-style combined data/node bitmaps and credible alternative node representations;
- atomic intrusive reference counts, non-atomic ownership where permitted, and arena/generation
  lifetime strategies; atomics are not assumed free without measurements under sharing;
- per-node source/deleter metadata versus a shared ownership domain with explicit deep cloning when
  changing domains; cross-source flexibility must justify its node-footprint and hot-path cost;
- cache misses, branch behavior, code size, and latency distributions;
- transient node-layout iterator designs: compact traversal-stack iterators invalidated by
  structural mutation, per-element iteration links, a separate iteration index, and root-searching
  increment where credible; measure iterator size, per-element memory, mutation cost, and traversal
  cost rather than assuming stability is free;
- comparison with `std::unordered_map`, Abseil hash containers, adaptive radix trees, and a simple
  sorted/dense index where appropriate;
- single-threaded operation without concurrency overhead;
- arena, allocator, and caller-supplied bounded node storage.

## Benchmark selections

The remaining choices do not leave container semantics unspecified. Benchmarks select:

- ownership within an allocation domain from atomic intrusive counts, constrained non-atomic
  ownership, ownership-domain or arena/generation retention, or a useful combination;
- the fragment widths and bitmap/node representations retained as public options rather than
  internal tuning;
- exact layouts for lightweight result structs carrying the value or new container, mutation flag,
  and `HamtError` from `try_*` operations;
- whether stable transient node-layout iterators justify an `HamtOptions` specialization. The base
  contract preserves references across unrelated node mutations but permits iterator invalidation.

The initial API provides only consuming `persistent() &&`. A repeated non-consuming transient
snapshot API is deferred unless a measured workload justifies its edit-token rollover and
copy-on-write cost.

## Layout guarantees

- Node layout preserves references across insertions and unrelated erasures; erasing an element
  invalidates references to that element. Transient iterator stability remains benchmark- or
  option-selected because preserving it may require extra per-element memory or slower traversal.
- Flat layout prioritizes locality and compactness; mutation may invalidate all iterators and
  references according to its documented rules.
- Both layouts are implemented for comparison, but both become public only if each demonstrates a
  material advantage for a relevant workload.
