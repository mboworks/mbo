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
  allocation-free configurations.

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

### Identity and equality

Strings are equal by byte content and length. Input view address and the source object's lifetime
do not participate in identity. Hash collisions must be resolved by equality; a hash value alone is
not a string identifier.

The empty string is a normal internable value. Returned views remain valid until the owning
interner is destroyed; the design must state explicitly whether move, swap, or reset can invalidate
them.

### Identifiers

Identifiers are dense and monotonically assigned within the visible identifier space. Two layouts
are still under consideration:

| Layout                  | First ID | New ID                 | Failure representation             |
| ----------------------- | -------: | ---------------------- | ---------------------------------- |
| Zero-based              |        0 | `size()` before insert | Separate result type               |
| One-based with sentinel |        1 | `size()` after insert  | Zero can represent invalid/failure |

The one-based layout makes a compact sentinel-returning hot-path API possible. A strong `StringId`
type should prevent accidental arithmetic and avoid confusing an invalid value with a valid
integer. ID exhaustion must be detected before mutating either storage or the index.

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

Whether lookup checks local entries before the captured parent prefix remains to be settled. Local
first is the natural rule because it preserves a child's own ID if duplicates can arise after the
snapshot, but the insertion algorithm should normally avoid a local duplicate of a string already
visible at child creation.

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
std::optional<StringId> Find(std::string_view value) const;
std::optional<std::string_view> Lookup(StringId id) const;

std::size_t size() const;
std::size_t local_size() const;
bool empty() const;
```

We must decide whether insertion reports whether it created a local entry, for example with an
`InsertResult { StringId id; bool inserted; }`, and whether the fast sentinel API and diagnostic
API should have distinct names such as `Intern` and `TryIntern`.

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

- repeated hits and unique inserts across short, medium, and long strings;
- high and low duplication ratios;
- root, shallow-child, and deep-chain lookups, separating local and ancestor hits;
- adversarial and ordinary hash collisions;
- allocation count, allocated bytes, resident memory, and fragmentation;
- arena segment sizes and bounded-capacity exhaustion;
- standard, Abseil, and mbo-provided index implementations;
- sentinel, optional, status, expected, and throwing adapters where supported;
- lookup and insertion latency distributions, not only throughput averages.

## Open questions

1. Do we adopt one-based IDs with zero invalid, replacing the earlier zero-based dense-ID rule?
2. Does `Find` search local entries first and then each captured ancestor prefix?
3. Does the main insertion API return only `StringId`, or an `{id, inserted}` result?
4. What is the default ID width: `std::uint32_t`, `std::uint64_t`, or `std::size_t`?
5. Which detailed error distinctions are useful: ID exhaustion, character capacity, entry/index
   capacity, allocator failure, and invalid configuration?
6. Should both the character store and index receive the same `std::pmr::memory_resource`, or
   should storage be a more general template concept with PMR supplied as one adapter?
7. Which operations invalidate views: move construction, move assignment, swap, clear, reset, and
   destruction? Can some operations be deleted to preserve a simpler guarantee?
8. Are embedded NUL bytes fully supported? The content-and-length equality model suggests yes.
9. Is thread safety entirely external, or should there be a read-only frozen form supporting
   concurrent lookup?
10. Do we need serialization or deterministic reconstruction of the dense ID table?
11. Should the advanced heterogeneous parent facility be part of version one, experimental, or
    postponed until a concrete optimized organization demonstrates its constraints?
