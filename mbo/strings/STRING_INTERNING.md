# String interning design

This document records the design of an owning string interner for `mbo::strings`. It is a design
document, not yet an API contract. Settled decisions are separated from questions so experiments
can change the implementation without obscuring the required semantics.

## Goals

The interner accepts strings as `std::string_view`, owns one stable copy of each interned string,
and assigns compact dense identifiers. It supports fast lookup in both directions:

- string to identifier, without constructing a `std::string` merely for lookup;
- identifier to `std::string_view`, with the returned view owned by the interner;
- snapshot parent/child interning, allowing related domains to share a stable prefix while adding
  independent local strings;
- configurable hashing, indexing, and character storage, including arena-backed and bounded
  allocation-free configurations;
- a reusable `SegmentedVector` built from fixed-capacity segments for append-oriented stable
  storage, including but not limited to the interner's dense ID table.

The design should make the efficient configuration easy while allowing users to choose different
container guarantees. Standard unordered containers, Abseil hash containers, and an mbo-provided
index should be usable when they satisfy the eventual concepts.

## Core model

An interner consists conceptually of three independent facilities:

1. stable character storage that owns the bytes behind returned `std::string_view` values;
2. an ordered table mapping dense identifiers to those views;
3. a heterogeneous string index mapping content to identifiers.

Separating these concerns is important. An arena can eliminate per-string character allocations,
but it does not make a node-based hash table allocation-free. A fully bounded interner must bound
and supply storage for both the character data and every index/table allocation.

The ordered ID-to-view table naturally wants `SegmentedVector`, an append-oriented container made
from fixed-capacity segments. It must provide stable element addresses and efficient indexing by
dense position. Developing this reusable container is an explicit goal of the project, not merely
a private interner implementation detail. Its API should nevertheless be driven by demonstrated
requirements and measured behavior rather than speculative generality.

The byte arena has different packing and lifetime needs and should not automatically share the
`SegmentedVector` abstraction.

### Relationship between `SegmentedVector` and an arena

`SegmentedVector` and a segmented arena can share nearly the same block-chain substrate, but they
provide different contracts:

| Property             | `SegmentedVector<T>`                         | Segmented arena                    |
| -------------------- | -------------------------------------------- | ---------------------------------- |
| Allocation unit      | A fixed number of `T` element slots          | Requested bytes plus alignment     |
| Type knowledge       | Knows `T`, `sizeof(T)`, and `alignof(T)`     | Treats allocations as untyped      |
| Object lifetime      | Constructs and destroys individual elements  | Usually releases a region at once  |
| Addressing           | Dense element index                          | Pointer or arena-specific handle   |
| Contiguous guarantee | Within one segment only                      | Within one allocation only         |
| Primary operation    | Append/emplace an element and index it later | Allocate an aligned range of bytes |

A common internal segmented-allocation primitive may therefore be worthwhile, provided it does not
force arena semantics onto the typed container or vice versa.

Segment capacities may be described by a compile-time size list. That permits optimized mapping
from a dense index to known prefix ranges, for example through unrolled comparisons, while allowing
small early segments and larger later segments. The design must define what happens after the
listed capacities: stop at a fixed total capacity, repeat the last capacity, or transition to a
runtime growth policy. These choices should be benchmarked against uniform power-of-two segments,
which can map indices particularly cheaply.

### Identity and equality

Strings are equal by byte content and length. Input view address and the source object's lifetime
do not participate in identity. Hash collisions must be resolved by equality; a hash value alone is
not a string identifier.

The empty string is a normal internable value. Returned views remain valid until the owning
interner is destroyed; the design must state explicitly whether move, swap, or reset can invalidate
them.

### Identifiers

Identifiers are dense ordinals and are monotonically assigned within the visible identifier space.
They are not hash values. The selected hash function may therefore produce every value in its
range, including zero, without colliding with an invalid-ID representation.

Two ID layouts are still under consideration:

| Layout                  | First ID | New ID                 | Failure representation             |
| ----------------------- | -------: | ---------------------- | ---------------------------------- |
| Zero-based              |        0 | `size()` before insert | Separate result type               |
| One-based with sentinel |        1 | `size()` after insert  | Zero can represent invalid/failure |

The one-based layout makes a compact sentinel-returning hot-path API possible. The zero-based
layout permits ID zero to identify the empty string, which could be inserted during construction
and optimized specially. Neither benefit should be assumed material until measured.

A strong `StringId` type should prevent accidental arithmetic and avoid confusing an invalid value
with a valid integer. Its underlying representation must be a selectable 8-, 16-, 32-, or 64-bit
integer so applications can trade capacity against memory footprint. Whether signed underlying
types are useful remains open. Unsigned types expose their full range naturally; signed types only
provide value if negative sentinels justify sacrificing half the non-negative ID space. ID
exhaustion must be detected before mutating either storage or the index.

`size()` is the number of identifiers visible from an interner, including its captured ancestors.
`local_size()` is the number added directly to that interner.

Looking up an invalid or non-visible identifier returns `std::optional<std::string_view>` in the
ordinary API. An unchecked lookup may be considered only if profiling establishes a need and its
precondition is explicit.

## Snapshot parent/child semantics

A child captures a non-owning parent reference and the parent's visible size at the exact time the
child is created. The parent does not track its children. The child can see only the prefix that
existed at that captured cutoff, plus its own local entries.

Consequently:

- a string visible through the captured parent prefix retains and returns its parent ID;
- additions to the parent after child creation are invisible to the child;
- if the same new string is later added independently to parent and child, it may have different
  IDs in the two interners;
- subsequent child lookup must return its local ID and must not discover the parent's post-cutoff
  ID;
- a child cannot outlive its parent, and no descendant can outlive any ancestor on which it
  depends.

The ordinary parent constructor should require the exact same interner specialization. Mixing
hashers, indexes, or storage policies casually would make invariants and performance surprising.
An explicitly named advanced facility may later allow compatible but different specializations,
supporting organizations whose parent and child hold strings of substantially different natures.
Such compatibility must be expressed as a deliberate concept, not inferred from coincidentally
similar member functions.

Lookup supports both traversal directions, analogous to `find` and `rfind`:

- `find` starts at the beginning of the visible ID space, searching the topmost visible ancestor
  first and ending with the child;
- `rfind` starts at the end of the visible ID space, searching the child first and then its visible
  ancestors in reverse order.

This distinction is observable when the same string acquired different IDs in independently
mutated ancestors and descendants after a snapshot. Interning an already visible string still
needs one defined lookup direction; this remains to be selected explicitly.

## Customization

The public template should permit independent policies or compatible containers for:

- the hash function and transparent equality;
- the string-to-ID index;
- stable character storage;
- the ID-to-view table;
- the identifier representation and possibly the failure policy.

The eventual constraints must describe behavior rather than require a particular STL spelling.
In particular, heterogeneous lookup by `std::string_view` is essential: using a container that
constructs an owning `std::string` for every lookup defeats a central goal.

An mbo default should provide excellent performance and stable views. Compatibility adapters can
make `std::unordered_map` and Abseil flat/node hash containers usable where their invalidation and
allocation guarantees fit the selected storage arrangement.

### Owning-string insertion

For compatibility with standard-container conventions, insertion should also accept an rvalue
`std::string`. This must not force the default representation to store one `std::string` object per
entry or weaken arena support.

The overload is therefore a capability of the selected storage backend:

- an owning-string backend may adopt the moved string's allocation where it can preserve the
  character address;
- an arena backend copies the bytes into arena storage, even when its input is an rvalue;
- a bounded backend succeeds only when its supplied capacity can hold the bytes and index entry.

Small-string optimization means moving a `std::string` does not universally transfer a stable heap
allocation. An owning-string backend must also choose a representation whose later growth, moves,
or relocation cannot invalidate views into short strings. This profile is an additional option,
not a cost paid by the arena-oriented default.

## Allocation models

At least three configurations should be investigated and benchmarked:

| Model                 | Character storage          | Index/table storage       | Exhaustion                |
| --------------------- | -------------------------- | ------------------------- | ------------------------- |
| General-purpose       | Growing segmented arena    | Growing containers        | Allocation failure policy |
| Caller-supplied arena | Provided memory resource   | Same or separate resource | Resource/failure policy   |
| Fully bounded         | Fixed caller-owned storage | Fixed-capacity structures | Explicit, non-destructive |

A segmented arena is attractive because it drastically reduces allocation count while keeping
previously returned views stable as the interner grows. A single growing contiguous byte vector is
not acceptable unless relocation is impossible, because it would invalidate every stored view.

The fully bounded mode must perform no hidden allocation. Exhaustion can arise independently from
character capacity, entry capacity, index capacity, or identifier range. A failed insertion must
be transactional: no bytes, ID, or index entry become observably committed unless the complete
operation succeeds.

### Failure APIs

The following interfaces are candidates and may coexist as adapters over one implementation:

| Form                         | Information                  | Intended use                         |
| ---------------------------- | ---------------------------- | ------------------------------------ |
| Invalid `StringId` sentinel  | Success/failure only         | Smallest and fastest hot-path result |
| `std::optional<StringId>`    | Success/failure only         | Conventional non-throwing API        |
| `absl::StatusOr<StringId>`   | Detailed failure             | Existing mbo/Abseil callers          |
| `std::expected<StringId, E>` | Typed detailed failure       | C++23 configuration                  |
| Exception                    | Detailed out-of-band failure | Explicit throwing adapter only       |

The baseline remains C++20, so `std::expected` cannot be the only public mechanism. Exceptions
should not be the primary interface for a latency-sensitive container and must remain optional for
builds with exceptions disabled. Successful-hit and successful-insert performance, result size,
generated code, and failure behavior should be measured for each serious candidate.

## Candidate operations

Names and exact return types are deliberately provisional:

```cpp
StringId Intern(std::string_view value);
StringId Intern(std::string&& value);
std::optional<StringId> Find(std::string_view value) const;
std::optional<StringId> RFind(std::string_view value) const;
std::optional<std::string_view> Lookup(StringId id) const;

std::size_t size() const;
std::size_t local_size() const;
bool empty() const;
```

Insertion follows the standard associative-container convention and reports both the ID and
whether it created a local entry, for example with
`InsertResult { StringId id; bool inserted; }`. This result should have no measurable cost over
returning only the ID. If measurement finds a material cost, or ID-only use is sufficiently common,
an additional convenience method may return only `StringId`. The fast sentinel API and diagnostic
API may still need distinct names such as `Intern` and `TryIntern`.

## Correctness invariants

- Every visible valid ID maps to exactly one byte string.
- Re-interning a visible string returns its existing visible ID and does not allocate.
- New local IDs form a contiguous suffix after the captured parent cutoff.
- Parent mutation after child creation cannot change any lookup result in the child.
- Hash collisions never merge unequal strings.
- Successful insertion leaves all previously returned views valid.
- Failed insertion leaves observable state unchanged.
- Destruction order is parent after all children; the API should make misuse difficult and document
  that this remains a lifetime precondition if static enforcement is impractical.

## Measurements required

Benchmarks should cover:

- repeated hits and unique inserts across short, medium, and long strings, including the empty
  string and small-string-optimized inputs;
- high and low duplication ratios;
- root, shallow-child, and deep-chain lookups, separating local and ancestor hits;
- adversarial and ordinary hash collisions;
- allocation count, allocated bytes, resident memory, and fragmentation;
- arena segment sizes and bounded-capacity exhaustion;
- ID-table chunk sizes, lookup cost, wasted tail capacity, and traversal/indexing strategies;
- compile-time segment-size lists versus uniform and runtime growth policies;
- standard, Abseil, and mbo-provided index implementations;
- zero-based and one-based ID layouts, including any optimized pre-interned empty string;
- 8-, 16-, 32-, and 64-bit ID representations where practical;
- sentinel, optional, status, expected, and throwing adapters where supported;
- `std::string_view` insertion, moved-string adoption, and moved-string-to-arena copying;
- lookup and insertion latency distributions, not only throughput averages.

## Open questions

1. Do we use zero-based IDs, possibly with a pre-interned empty string, or one-based IDs with zero
   invalid? Which result should the primary API use if measurement finds no meaningful difference?
2. Which direction does `Intern` use to find an already visible string: `find` from the topmost
   ancestor or `rfind` from the child?
3. What is the default ID width, and do we support only unsigned 8-, 16-, 32-, and 64-bit types or
   signed types as well?
4. Which detailed error distinctions are useful: ID exhaustion, character capacity, entry/index
   capacity, allocator failure, and invalid configuration?
5. Should both the character store and index receive the same `std::pmr::memory_resource`, or
   should storage be a more general template concept with PMR supplied as one adapter?
6. Is moved-`std::string` adoption important enough to ship an owning-string backend in version one,
   or is accepting the overload and copying into the default arena sufficient initially?
7. Which operations invalidate views: move construction, move assignment, swap, clear, reset, and
   destruction? Can some operations be deleted to preserve a simpler guarantee?
8. Are embedded NUL bytes fully supported? The content-and-length equality model suggests yes.
9. Is thread safety entirely external, or should there be a read-only frozen form supporting
   concurrent lookup?
10. Do we need serialization or deterministic reconstruction of the dense ID table?
11. Should the advanced heterogeneous parent facility be part of version one, experimental, or
    postponed until a concrete optimized organization demonstrates its constraints?
12. Do `SegmentedVector` and arena storage share a public block-chain abstraction, share only a
    private implementation primitive, or remain independent until measurement exposes useful
    commonality?
13. What follows a compile-time segment-size list: fixed exhaustion, repetition of the last size,
    or a separate growth policy?
