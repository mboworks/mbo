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

`SegmentedVector` and `Arena` are independent production components and prerequisites for the
interner's default composition. Their public contracts remain separate from this design.

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

`SegmentedVector` and a segmented arena both acquire backing blocks through `BlockSource`, but
they do not share a chain representation and provide different contracts:

| Property             | `SegmentedVector<T>`                         | Segmented arena                    |
| -------------------- | -------------------------------------------- | ---------------------------------- |
| Allocation unit      | A fixed number of `T` element slots          | Requested bytes plus alignment     |
| Type knowledge       | Knows `T`, `sizeof(T)`, and `alignof(T)`     | Treats allocations as untyped      |
| Object lifetime      | Constructs and destroys individual elements  | Usually releases a region at once  |
| Addressing           | Dense element index                          | Pointer or arena-specific handle   |
| Contiguous guarantee | None; segment views are random-access        | Within one allocation only         |
| Primary operation    | Append/emplace an element and index it later | Allocate an aligned range of bytes |

The current `SegmentedVector` uses one compile-time power-of-two `segment_size`, shift/mask index
mapping, a hard `segment_capacity`, and an initial `segment_reservation`. Those options keep every
segment uniform while making bounded capacity and directory-allocation behavior explicit. Segment
size and reservation remain measurement choices because they trade directory size and allocation
frequency against tail waste and cache behavior. Additional growth-policy surface is added only if
a measured workload requires it.

### Identity and equality

Strings are equal by byte content and length. Input view address and the source object's lifetime
do not participate in identity. Hash collisions must be resolved by equality; a hash value alone is
not a string identifier.

The empty string is a normal internable value. Returned views remain valid until the owning
interner is destroyed; the design must state explicitly whether move, swap, or reset can invalidate
them.

### Identifiers

Identifiers are dense and monotonically assigned within the visible identifier space.
They are not hash values. The selected hash function may therefore produce every value in its
range, including zero, independently of the ID representation's invalid sentinel.

IDs are zero-based. The first inserted string receives ID zero and every new ID is `size()` before
insertion. Zero is a valid ID, including for an empty string when that is the first value inserted.
The largest underlying representation value is reserved as `StringId::invalid_value`, making a
default-constructed ID explicitly invalid without sacrificing zero-based dense assignment.

A strong `StringId` type should prevent accidental arithmetic and avoid confusing an invalid value
with a valid integer. Its underlying representation must be a selectable 8-, 16-, 32-, or 64-bit
unsigned integer so applications can trade capacity against memory footprint. These are the
supported POD representations; other underlying types, including signed integers, are not
supported without a demonstrated use. ID exhaustion must be detected before mutating either
storage or the index. The default underlying representation is `std::uint32_t`.

ID width and hash width are independent. A 64-bit hash selects an index path whose stored payload
may be a 32-bit ID; no hash-to-ID conversion occurs. The index consumes the hasher's useful output
bits for routing and collision detection, while the ID remains only the value associated with the
string. A 64-to-32 reduction such as `mbo::hash::Hash64To32` is used only when a selected index
explicitly requires a 32-bit hash. Native 32-bit hashing and reduction from a 64-bit hash must be
benchmarked for such an index rather than coupled to `StringId`.

Possible representation-specific reasons to use 32 bits include packing a 32-bit hash or
fingerprint with a 32-bit ID in one 64-bit word, satisfying an index backend whose hash interface is
32-bit, or targeting a 32-bit platform. These are measured backend optimizations, not semantic
coupling. They must not truncate the default hash merely because the default ID is `std::uint32_t`.

`size()` is the number of identifiers visible from an interner, including its captured ancestors.

The initial [`StringId`](string_id.h) helper constrains
[`mbo::types::ConstStrongId`](../types/strong_id.h) to the four fixed-width unsigned
representations. It provides explicit underlying-value access, ordering, constexpr checked ordinal
conversion via `try_from_ordinal`, an `invalid_value` sentinel, and `is_valid()`, but deliberately
does not expose arithmetic or mutation operations. The largest underlying value is
reserved, so an 8-bit ID supports 255 entries with valid IDs `0..254`; exhaustion returns an empty
optional. Hash width remains independent. Direct construction assumes an already representable
underlying value; constructing `invalid_value` deliberately produces an invalid ID. Use checked
conversion when assigning from a wider count. This helper does not establish membership in any
particular interner or chain.
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
Heterogeneous parent specializations are deferred because no concrete workload has demonstrated
their value. Same-specialization chains may be arbitrarily deep subject to ordinary resource and
recursion/iteration limits; cascading chains are a first-class, useful facility and require only a
parent reference plus the captured cutoff at each level.

Lookup supports both traversal directions, analogous to `find` and `rfind`:

- `find` traverses forward from the beginning of the topmost visible parent and ends at the child;
- `rfind` traverses backward from the last visible child ID and ends at the topmost visible parent.

Both directions produce the same result under the uniqueness invariant. A child cannot insert a
duplicate of a string visible at its cutoff, while a parent's later insertion is outside the
child's captured prefix. Divergent branches may independently assign different IDs, but no one
interner can see both branch-local entries. Search direction is therefore a performance and API
choice, not duplicate precedence, unless the design deliberately introduces an operation that can
bypass interning and append duplicates.

Both directions are required for lookup and interning. Neither parent-first nor child-first is
universally hotter: applications may concentrate reuse in a shared root dictionary or in recent
branch-local additions. Explicit operations expose each direction. A compile-time option selects
the direction of the unsuffixed convenience operation so the default hot path has no runtime branch.
The explicit insertion operations are `intern_parent_first(...)` and `intern_child_first(...)`;
`intern(...)` uses the direction selected by `StringInternerOptions`.

### Immutability and iteration

Interned strings can never be changed or deleted. An interner is append-only, and every interner's
visible IDs form the contiguous interval `[0, size())`, regardless of how many parent snapshots
provide that prefix. Local storage begins at the captured parent cutoff, but `begin()` does not mean
local begin: it denotes the first ID in the topmost visible parent.

Consequently, `begin()` represents ID zero and `end()` represents ID `size()`. Standard reverse
iterators derive from the same half-open interval, making `rbegin()` start with `size() - 1` when
non-empty and making `rbegin() == rend()` when empty. No special reverse-iteration sentinel is
needed beyond the usual iterator representation.

An iterator must retain interner identity as well as position. Numeric IDs alone are insufficient
for iterator equality because different snapshot branches can assign the same local ID to different
strings. Appending must preserve existing element references and iterators; as with other growing
containers, an iterator that previously represented `end()` need not become the new end.

Dereferencing an iterator yields only `std::string_view`. It does not expose an entry pair or an
additional public ID accessor. A caller that needs IDs while traversing can count dense ordinal
positions from `begin()`. IDs remain element ordinals; using arena byte offsets as IDs is not the
default because variable-length storage would make the public representation and capacity less
efficient.

### Object and view lifetime

StringInterner is non-copyable and non-movable. It provides neither move construction nor move
assignment, and it cannot be swapped. This anchors every parent reference and avoids a heap control
block, child registry, relocation tracking, or other overhead solely to make object movement safe.

There is no `clear` or `reset`: the interner is append-only until destruction. Destruction
invalidates every view and iterator it produced and remains forbidden while any child exists. The
child-before-parent destruction requirement is a documented lifetime precondition because the
parent intentionally does not pay to track children.

## Customization

One allocator-shaped abstraction is insufficient because customization covers more than acquiring
raw memory. The public design should permit separate policies or compatible implementations for:

- the hash function and transparent equality;
- the string-to-ID index;
- stable character storage;
- the ID-to-view table;
- the identifier representation and possibly the failure policy.

Each extension point must have a narrow behavioral contract enforced by a C++ concept. Multiple
interfaces are acceptable where ownership, block acquisition, indexing, and typed element storage
have genuinely different requirements. They must compose without requiring inheritance or one
particular memory-resource model.

The constraints must describe behavior rather than require a particular STL spelling. In
particular, heterogeneous lookup by `std::string_view` is essential: using a container that
constructs an owning `std::string` for every lookup defeats a central goal. PMR resources and arenas
are adapters where they meet the relevant concepts, not fundamental requirements imposed on every
configuration.

An mbo default should provide excellent performance and stable views. Compatibility adapters can
make `std::unordered_map` and Abseil flat/node hash containers usable where their invalidation and
allocation guarantees fit the selected storage arrangement.

The independent [HART/HAMT study](../container/HASH_TRIES.md) selects HAMT as a general container
implementation target while retaining HART only as a documented concurrency-oriented research
option. A persistent HAMT may align with parent/child snapshots through structural sharing, but it
is neither the default nor a prerequisite until measurements justify it.

The index is not the interner's iteration storage. Public StringInterner iterators traverse the
dense ID-to-view sequence, and lookup results are converted from an index entry to that dense
position. No iterator from `std::unordered_map`, an Abseil container, HAMT, or another selected index
escapes the interner. The index therefore need not provide stable iterators; stable strings and
public interner iterators come from character storage and the dense sequence respectively.

### Owning-string insertion

Owning `std::string` storage is a fundamentally different, viable backend rather than an incidental
optimization of the arena/string-view design. It is expected to be slower for the primary workload
and is not part of version one. Accepting an rvalue `std::string` does not force the default
representation to store one `std::string` object per entry or weaken arena support; the initial
arena backend copies the string's bytes into stable character storage.

The overload is therefore a capability of the selected storage backend:

- an owning-string backend may adopt the moved string's allocation where it can preserve the
  character address;
- an arena backend copies the bytes into arena storage, even when its input is an rvalue;
- a bounded backend succeeds only when its supplied capacity can hold the bytes and index entry.

Small-string optimization means moving a `std::string` does not universally transfer a stable heap
allocation. An owning-string backend must also choose a representation whose later growth, moves,
or relocation cannot invalidate views into short strings. This profile remains deferred and becomes
an additional backend only if benchmarks demonstrate a meaningful winning workload. It is never a
cost paid by the arena-oriented default.

### Embedded NUL bytes

The natural `std::string_view` contract compares and hashes the complete explicit length, so an
embedded NUL byte is ordinary content rather than a terminator. This is the correctness default and
avoids silently changing `string_view` semantics into C-string semantics.

The benchmark suite must nevertheless measure whether full embedded-NUL support causes any relevant
micro-performance cost. A compile-time no-embedded-NUL option is justified only if disabling support
produces a material measured improvement. Such a mode must state an explicit precondition rejecting
inputs containing NUL; it must never silently truncate them.

### Thread safety

Thread safety uses external synchronization, consistent with other mbo containers. Concurrent const
lookup is permitted only while no thread mutates that interner or any relevant ancestor. No frozen
representation is planned unless it later provides a measured optimization or enforces a necessary
lifetime guarantee.

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

The arena is a separately benchmarked project component. At least two representations must be
compared for interned strings:

| Representation       | Arena payload              | Dense ID metadata           | Principal tradeoff                  |
| -------------------- | -------------------------- | --------------------------- | ----------------------------------- |
| Separate descriptors | Packed character bytes     | `(size, pointer)` or offset | Direct lookup; larger metadata      |
| Inline records       | Consecutive `(size, data)` | Offset or record index      | Better locality; variable-size scan |

Inline `(size, data)` records remove a content pointer from the record itself, but variable record
sizes prevent direct O(1) dense-ID lookup unless another offset table is maintained. Relative
offsets may be narrower than pointers and make arena segments more relocatable. Measurements must
include lookup latency, insertion throughput, bytes per string, alignment loss, and cache behavior.

The fully bounded mode must perform no hidden allocation. Exhaustion can arise independently from
character capacity, entry capacity, index capacity, or identifier range. A failed insertion must
be transactional: no bytes, ID, or index entry become observably committed unless the complete
operation succeeds.

The lightweight error enum distinguishes `id_exhausted`, `character_storage_exhausted`,
`entry_storage_exhausted`, and `index_exhausted`. Allocation and block-source failures map to the
storage component whose capacity could not grow.

### Failure APIs

The following interfaces are candidates and may coexist as adapters over one implementation:

| Form                         | Information                  | Intended use                         |
| ---------------------------- | ---------------------------- | ------------------------------------ |
| Invalid `StringId` sentinel  | Success/failure only         | Smallest and fastest hot-path result |
| `std::optional<StringId>`    | Success/failure only         | Conventional non-throwing API        |
| `absl::StatusOr<StringId>`   | Detailed failure             | Existing mbo/Abseil callers          |
| `std::expected<StringId, E>` | Typed detailed failure       | C++23 callers                        |
| Exception                    | Detailed out-of-band failure | Explicit throwing adapter only       |

The C++23 baseline makes `std::expected` available, but it need not be the only public mechanism.
Exceptions should not be the primary interface for a latency-sensitive container and must remain
optional for builds with exceptions disabled. Successful-hit and successful-insert performance,
result size, generated code, and failure behavior should be measured for each serious candidate.

## Candidate operations

Names and exact return types are deliberately provisional:

```cpp
struct InsertResult {
  StringId id;
  bool inserted;
};

InsertResult intern(std::string_view value);
InsertResult intern(std::string&& value);
InsertResult intern_parent_first(std::string_view value);
InsertResult intern_child_first(std::string_view value);

iterator find(std::string_view value) const;
reverse_iterator rfind(std::string_view value) const;
std::optional<std::string_view> lookup(StringId id) const;

std::size_t size() const;
std::size_t local_size() const;
bool empty() const;
```

Insertion follows the standard associative-container convention and reports both the ID and whether
it created a local entry. This result should have no measurable cost over returning only the ID. If
measurement finds a material cost, or ID-only use is sufficiently common, an additional convenience
method may return only `StringId`. The fast hard-failing API and failure-aware API may still need
distinct names such as `intern` and `try_intern`.

`find` returns a forward iterator and uses `end()` for a miss. `rfind` returns a reverse iterator and
uses `rend()` for a miss. Iteration itself exposes only `string_view`; callers can count dense
ordinal IDs when needed.

Search direction and result orientation are independent choices:

| Operation model              | Result                  | Consequence                                      |
| ---------------------------- | ----------------------- | ------------------------------------------------ |
| Associative-container `find` | Forward `iterator`      | Familiar `end()` miss; direction stays internal  |
| Reverse-range search         | `reverse_iterator`      | Miss compares with `rend()`                      |
| String-like position lookup  | Optional or sentinel ID | Direct dense ID; not associative-container style |
| Counted forward iteration    | Forward `iterator`      | Natural string-view range; caller counts IDs     |

Because visible strings are unique, a reverse search need not force a reverse-oriented result.
Returning `reverse_iterator` from `rfind` intentionally exposes its traversal orientation, while
dereference still yields only `string_view`.

## Serialization

No dedicated serialization format or API is planned initially. Dense iteration already lets a
caller serialize strings in ID order; reinserting that sequence reconstructs the same dense IDs
without coupling the public contract to an index or arena representation.

## Correctness invariants

- Every visible valid ID maps to exactly one byte string.
- Every visible byte string maps to exactly one ID within one interner view.
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
- fixed power-of-two ID-table segment sizes, lookup cost, and wasted tail capacity;
- zero, partial, and full directory reservation under bounded and growing workloads;
- indexed, forward, reverse, segment-view, and iterator-arithmetic traversal;
- separate `(size, pointer/offset)` descriptors versus inline `(size, content)` arena records;
- standard, Abseil, and mbo-provided index implementations;
- 8-, 16-, 32-, and 64-bit ID representations where practical;
- 32- and 64-bit index hash outputs where supported, including native 32-bit hashing versus
  `Hash64To32` reduction when an index requires 32 bits;
- sentinel, optional, status, expected, and throwing adapters where supported;
- `std::string_view` insertion, moved-string adoption, and moved-string-to-arena copying;
- embedded-NUL support versus a checked or preconditioned no-NUL specialization;
- lookup and insertion latency distributions, not only throughput averages.

### Diagnostic support

The finished library should provide a deliberately opt-in diagnostic configuration suitable for
understanding production string populations and tuning the selected representation. It should be
able to report string-size histograms, hit depth and search direction, parent-versus-local hits,
hash collisions, table occupancy and probe or trie depth, payload/descriptor/index memory,
alignment padding, unused capacity, and peak memory.

This is a quality requirement, not permission to tax the ordinary container. Disabled diagnostics
must add no object fields, counter updates, branches, locks, or atomics. Instrumentation may use a
separate specialization, observer, or benchmark wrapper after measurements determine the least
intrusive design. Clearly valuable diagnostics should be implemented even if they are not needed
by the first internal caller.

## Benchmark selections

The version-one semantic contract has no remaining open questions. Measurements still select:

- whether implementation experience demonstrates a useful common abstraction above the existing
  `BlockSource` boundary without coupling arena and `SegmentedVector` ownership models;
- native pointers versus segment-relative offsets for arena descriptors, including the best useful
  offset width;
- whether a later owning-`std::string` backend has a meaningful winning workload.

## Language baseline

The repository targets C++23 with its supported GCC and Clang toolchains. The implementation may
use C++23 facilities such as `std::expected`, static call/index operators, `if consteval`, and the
relaxed constexpr rules where they materially simplify the code or API. Their availability is not
by itself a reason to expose a facility or add an abstraction: selected uses still require focused
tests on the supported compiler matrix and a concrete readability, correctness, or generated-code
benefit.
