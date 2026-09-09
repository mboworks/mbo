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
- ownership and lifetime of keys and values;
- mutation and deletion in both transient and persistent modes, including path-copying persistent
  deletion and in-place mutation of uniquely owned transient nodes;
- a consuming transient-to-persistent conversion as the baseline fast path: `persistent() &&`
  invalidates the transient structurally and preserves in-place edit ownership until conversion;
- no repeated snapshot API on the hot path unless benchmarks show that its edit-token rollover and
  subsequent copy-on-write costs are justified;
- iteration order without an unmeasured deterministic-order guarantee; relevant research and
  benchmarks determine whether a stronger guarantee has enough value to expose;
- iterator/reference invalidation;
- copy, move, swap, and allocator/block-source propagation;
- bounded and allocation-failure behavior;
- exception-enabled and exception-disabled operation.

## Measurements required

- successful and unsuccessful lookup across realistic and adversarial hashes;
- insertion, replacement, and deletion where supported;
- persistent snapshot creation and branch-local updates;
- transient-to-persistent conversion if both modes exist;
- transient bulk construction with consuming conversion, compared with any repeated-snapshot
  candidate;
- short and long strings, shared prefixes, and varying duplication ratios;
- node count, pointer count, bitmap density, padding, allocated bytes, and fragmentation;
- 4-, 5-, 6-, and 7-bit hash fragments; 5 bits gives a 32-way bitmap in one 32-bit word, 6 bits
  gives a 64-way bitmap in one 64-bit word, while 4 trades smaller nodes for greater depth and 7
  requires multiword occupancy metadata or a wider representation;
- CHAMP-style combined data/node bitmaps and credible alternative node representations;
- atomic intrusive reference counts, non-atomic ownership where permitted, and arena/generation
  lifetime strategies; atomics are not assumed free without measurements under sharing;
- cache misses, branch behavior, code size, and latency distributions;
- comparison with `std::unordered_map`, Abseil hash containers, adaptive radix trees, and a simple
  sorted/dense index where appropriate;
- single-threaded operation without concurrency overhead;
- arena, allocator, and caller-supplied bounded node storage.

## Open questions

1. Which ownership strategy wins for persistent structural sharing: atomic intrusive counts,
   constrained non-atomic ownership, arena/generation retention, or a useful combination?
2. Does a repeated non-consuming transient snapshot operation justify its cost in any measured
   workload, or is consuming `persistent() &&` sufficient?
3. Which of the benchmarked fragment widths and bitmap/node representations should remain public
   policy rather than internal tuning?

## Layout guarantees

- Node layout preserves references to unaffected elements across unrelated mutation.
- Flat layout prioritizes locality and compactness; mutation may invalidate references according to
  its documented rules.
- Both layouts are implemented for comparison, but both become public only if each demonstrates a
  material advantage for a relevant workload.
