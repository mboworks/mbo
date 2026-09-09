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

[`SegmentedVector`](../container/SEGMENTED_VECTOR.md) and the
[`Arena`](../memory/ARENA.md) are prerequisite components. Each must be implemented and benchmarked
independently before selecting the interner's default composition.

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
small early segments and larger later segments.

After the listed capacities, all three of the following are supported strategies:

- stop at a fixed total capacity;
- repeat the final segment capacity;
- transition to another growth policy.

These strategies are selected through a constexpr-compatible policy type. The policy controls
compile-time code generation, not merely runtime configuration, so unsupported branches can be
discarded and bounded configurations can remain usable during constant evaluation. Its behavior,
capacity limits, overflow handling, and generated-code consequences must be fully documented.

Policy complexity is justified only by measured use. The public policy surface must contain only
strategies and parameters whose relevance is demonstrated by benchmarks; speculative flexibility
does not become supported API.

Uniform power-of-two segments receive a specialized index-mapping fast path up to a measured size
threshold. Beyond that threshold, excessively large uniform segments may waste too much tail
capacity, so a size list or growth policy can take over. The threshold and transition are selected
from benchmarks rather than fixed by intuition.

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
unsigned integer so applications can trade capacity against memory footprint. These are the
supported POD representations; other underlying types, including signed integers, are not
supported without a demonstrated use. ID exhaustion must be detected before mutating either
storage or the index.

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

- `find` traverses forward from the beginning of the topmost visible parent and ends at the child;
- `rfind` traverses backward from the last visible child ID and ends at the topmost visible parent.

Both directions produce the same result under the uniqueness invariant. A child cannot insert a
duplicate of a string visible at its cutoff, while a parent's later insertion is outside the
child's captured prefix. Divergent branches may independently assign different IDs, but no one
interner can see both branch-local entries. Search direction is therefore a performance and API
choice, not duplicate precedence, unless the design deliberately introduces an operation that can
bypass interning and append duplicates.

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
struct InsertResult {
  StringId id;
  bool inserted;
};

InsertResult intern(std::string_view value);
InsertResult intern(std::string&& value);

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

The iterator-returning `find` and `rfind` signatures above are the STL-like candidates, not yet a
settled contract. An ID-returning lookup could instead return `optional<StringId>`, but iteration
itself exposes only `string_view` and callers can count dense ordinal IDs when needed.

Search direction and result orientation are independent choices:

| Operation model              | Result                  | Consequence                                      |
| ---------------------------- | ----------------------- | ------------------------------------------------ |
| Associative-container `find` | Forward `iterator`      | Familiar `end()` miss; direction stays internal  |
| Reverse-range search         | `reverse_iterator`      | Miss compares with `rend()`                      |
| String-like position lookup  | Optional or sentinel ID | Direct dense ID; not associative-container style |
| Counted forward iteration    | Forward `iterator`      | Natural string-view range; caller counts IDs     |

Because visible strings are unique, a reverse search need not force a reverse-oriented result.
Returning a forward iterator from both search directions would make found values interchangeable,
while returning `reverse_iterator` from `rfind` exposes the traversal orientation. The current
recommendation is a normal forward `find` returning `iterator`, an explicitly named reverse search
returning `reverse_iterator`, and dereference yielding `string_view`. This remains a recommendation
until the public role of reverse search is confirmed.

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
- ID-table chunk sizes, lookup cost, wasted tail capacity, and traversal/indexing strategies;
- compile-time segment-size lists versus uniform and runtime growth policies;
- the power-of-two fast-path threshold and transition to later growth;
- separate `(size, pointer/offset)` descriptors versus inline `(size, content)` arena records;
- standard, Abseil, and mbo-provided index implementations;
- zero-based and one-based ID layouts, including any optimized pre-interned empty string;
- 8-, 16-, 32-, and 64-bit ID representations where practical;
- sentinel, optional, status, expected, and throwing adapters where supported;
- `std::string_view` insertion, moved-string adoption, and moved-string-to-arena copying;
- lookup and insertion latency distributions, not only throughput averages.

## Open questions

1. Do we use zero-based IDs, possibly with a pre-interned empty string, or one-based IDs with zero
   invalid? Which result should the primary API use if measurement finds no meaningful difference?
2. Which search direction does `intern` use for performance: forward from the topmost ancestor or
   backward from the child?
3. Which detailed error distinctions are useful: ID exhaustion, character capacity, entry/index
   capacity, allocator failure, and invalid configuration?
4. What is the default unsigned ID width?
5. Is moved-`std::string` adoption important enough to ship an owning-string backend in version one,
   or is accepting the overload and copying into the default arena sufficient initially?
6. Which operations invalidate views: move construction, move assignment, swap, clear, reset, and
   destruction? Can some operations be deleted to preserve a simpler guarantee?
7. Are embedded NUL bytes fully supported? The content-and-length equality model suggests yes.
8. Is thread safety entirely external, or should there be a read-only frozen form supporting
   concurrent lookup?
9. Do we need serialization or deterministic reconstruction of the dense ID table?
10. Should the advanced heterogeneous parent facility be part of version one, experimental, or
    postponed until a concrete optimized organization demonstrates its constraints?
11. Do `SegmentedVector` and arena storage share a public block-chain abstraction, share only a
    private implementation primitive, or remain independent until measurement exposes useful
    commonality?
12. Can arena descriptors use segment-relative offsets rather than native pointers, and which
    offset width provides the best useful capacity/footprint tradeoff?
13. Do `find` and `rfind` return iterators or optional IDs?

## Final language-baseline decision

After the container, arena, and interner contracts and prototypes are understood, the project must
make an explicit C++20-versus-C++23 baseline decision. The decision is based on implementation
simplicity, generated code, compiler support, and consumer cost. The following WG21 papers provide
the concrete C++23 case:

| Paper                                | Facility                                       | Potential relevance                                      |
| ------------------------------------ | ---------------------------------------------- | -------------------------------------------------------- |
| [P2647R1](https://wg21.link/P2647R1) | Static `constexpr` variables in constexpr code | Compile-time policy tables and segment boundaries        |
| [P2589R1](https://wg21.link/P2589R1) | Static `operator[]`                            | Stateless indexed policy/function objects                |
| [P1169R4](https://wg21.link/P1169R4) | Static `operator()`                            | Stateless hash, growth, and mapping policy objects       |
| [P2448R2](https://wg21.link/P2448R2) | Relaxed constexpr restrictions                 | Fewer artificial splits between runtime/constexpr paths  |
| [P2173R1](https://wg21.link/P2173R1) | Attributes on lambda expressions               | Better attributes on generated/local policy callables    |
| [P0847R7](https://wg21.link/P0847R7) | Explicit object parameter (`deducing this`)    | Fewer duplicated cv/ref accessors and CRTP-style helpers |
| [P2797R0](https://wg21.link/P2797R0) | Static/explicit-object wording resolution      | Clearer interaction of static and explicit-object APIs   |
| [P2201R1](https://wg21.link/P2201R1) | Mixed string-literal concatenation             | Cleaner compile-time string diagnostics and metadata     |
| [P1938R3](https://wg21.link/P1938R3) | `if consteval`                                 | Direct runtime/constant-evaluation path selection        |

No paper is sufficient by itself. Before raising the baseline, prototypes must show which features
remove real complexity or improve results, and the supported GCC/Clang/Bazel matrix must compile
and test those exact uses.
