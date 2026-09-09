# Hash-assisted trie design study

This document records the decision between two distinct hash/trie families: HART and HAMT. They
must not be conflated merely because their acronyms and use of hashes are similar.

HAMT is the implementation target. HART remains documented as a separate concurrency-oriented
research option, but is not planned for implementation. Neither is a prerequisite for
`SegmentedSequence` or the arena. HAMT becomes a string-interner dependency only if benchmarks show
that it is the best index for the representative workloads.

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

One block-source concept drives the core implementation. Adapters provide standard allocator, PMR,
arena, and fixed-buffer integration without multiplying HAMT implementations.

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

The allocation-exhaustion behavior of ordinary, non-`try_*` modifiers remains a separate API
decision. Users requiring recoverable bounded operation use `try_*`; the non-throwing contract does
not by itself decide whether an ordinary operation throws, terminates, or invokes a configured
failure handler when its block source is exhausted.

## Measurements required

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
- comparison with `std::unordered_map`, Abseil hash containers, adaptive radix trees, and a simple
  sorted/dense index where appropriate;
- single-threaded operation without concurrency overhead;
- arena, allocator, and caller-supplied bounded node storage.

## Open questions

1. Which ownership strategy wins within an allocation domain for persistent structural sharing:
   atomic intrusive counts,
   constrained non-atomic ownership, an ownership domain, arena/generation retention, or a useful
   combination?
2. Does a repeated non-consuming transient snapshot operation justify its cost in any measured
   workload, or is consuming `persistent() &&` sufficient?
3. Which of the benchmarked fragment widths and bitmap/node representations should remain public
   policy rather than internal tuning?
4. What exact lightweight result types carry the value/result, mutation flag, and bounded-operation
   error without imposing work on the successful path?
5. Can transient structural mutation preserve node-layout iterators without measurable linked-list
   or root-search overhead, or should it preserve references while invalidating iterators?

## Layout guarantees

- Node layout preserves references and iterators across insertions and unrelated erasures; erasing
  an element invalidates only references and iterators to that element.
- Flat layout prioritizes locality and compactness; mutation may invalidate all iterators and
  references according to its documented rules.
- Both layouts are implemented for comparison, but both become public only if each demonstrates a
  material advantage for a relevant workload.
