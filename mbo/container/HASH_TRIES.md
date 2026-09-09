# Hash-assisted trie design study

This document scopes two distinct hash/trie families for possible mbo containers: HART and HAMT.
They must not be conflated merely because their acronyms and use of hashes are similar.

Neither structure is currently a prerequisite for `SegmentedSequence`, the arena, or the string
interner. Each must demonstrate a concrete advantage in representative benchmarks before becoming
an implementation dependency or supported public API.

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

## Common container contract to investigate

Any mbo hash-trie container should be expressed through behavioral concepts for hash, equality,
key access, value storage, and node allocation. It must specify:

- both map and set forms over common internal machinery, plus heterogeneous lookup;
- flat versus node storage as a measured policy or implementation decision rather than an API
  omission;
- full-hash collisions and unequal-key resolution;
- ownership and lifetime of keys and values;
- mutation, deletion, persistence, and snapshot semantics;
- iteration order and whether it is deterministic;
- iterator/reference invalidation;
- copy, move, swap, and allocator/block-source propagation;
- bounded and allocation-failure behavior;
- exception-enabled and exception-disabled operation;
- lock-free HART operation wherever meaningful, with precisely documented progress, reclamation,
  and memory-ordering guarantees.

## Measurements required

- successful and unsuccessful lookup across realistic and adversarial hashes;
- insertion, replacement, and deletion where supported;
- persistent snapshot creation and branch-local updates;
- transient-to-persistent conversion if both modes exist;
- short and long strings, shared prefixes, and varying duplication ratios;
- node count, pointer count, bitmap density, padding, allocated bytes, and fragmentation;
- cache misses, branch behavior, code size, and latency distributions;
- comparison with `std::unordered_map`, Abseil hash containers, adaptive radix trees, and a simple
  sorted/dense index where appropriate;
- single-threaded configurations before adding concurrency overhead;
- concurrent HART read, insert, update, and deletion scalability, including contention and
  reclamation costs;
- arena, allocator, and caller-supplied bounded node storage.

## Open questions

1. Is mbo's HART a concurrent in-memory hash-assisted adaptive radix tree, or must it also provide
   the paper's persistent-memory placement, ordering, logging, and crash-recovery guarantees?
2. What progress guarantee does concurrent HART require: thread safety, lock-free reads, lock-free
   all-operation progress, or a stronger guarantee?
3. Should HAMT expose a flat/node policy publicly, select the representation internally, or offer
   distinct optimized container aliases after benchmarking both?
4. Is HAMT deletion required even though the string interner itself is append-only?
5. Must HAMT iteration be deterministic across processes and hash seeds?
6. Which HAMT hash fragment width and bitmap/node representations should be benchmarked?
7. Are HART prefix/range operations required, or only exact heterogeneous lookup?
8. Is HAMT concurrency also required, or is concurrency initially specific to HART?
9. Are these general public containers or initially experimental string-index implementations?
