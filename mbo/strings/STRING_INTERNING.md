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
interner's default composition. Their public contracts remain separate from this design, as do
concrete character-storage and string-container adapters.

## Core model

[`strings_internal::StringInternerBase`](internal/string_interner_base.h) implements shared
insertion, lookup, parent snapshots, and iteration for both key-only and key/value indexes.
[`StringInternerSet` and `StringInternerMap`](string_interner.h) are thin public wrappers that
provide string-set and string-to-value semantics. Shared public error types, conversions, and
container concepts live in [`string_interner_types.h`](string_interner_types.h). The base has a
private Bazel target, following the `LimitedOrdered` layout. The ID type comes first. The map
requires its mapped value type second; the remaining parameters are hash, string container,
character storage, and dense entry sequence:

```cpp
StringInternerSet<> strings;  // StringIdU32, flat string-to-ID index, arena bytes
StringInternerMap<StringIdU32<>, int> counts;
auto inserted = counts.insert("apple", 42);  // ID zero, value 42
if (inserted) {
  auto entry = counts.get(inserted->first);  // OptionalRef to ("apple", 42)
  counts.at(inserted->first) += 1;
}
StringInternerMap<std::uint64_t, std::string> descriptions;
StringInternerSet<StringIdU32<>, std::hash<std::string_view>> custom_hash;

using OwnedMap = std::map<std::string, StringIdU32<>, std::less<>>;
StringInternerMap<StringIdU32<>, int, void, OwnedMap> owned_counts;
using OwnedSet = std::set<std::string, std::less<>>;
StringInternerSet<StringIdU32<>, void, OwnedSet> owned_set;
```

Both wrappers expose string keys: `key_type` is the index's normalized key type, either
`std::string_view` or `std::string`. IDs are separate handles. Ordinary set iteration returns
`const key_type&`; ordinary map iteration returns a reference to a string/value pair. Neither
iteration nor ID access hashes a string or searches for its ID.

The base is parameterized by `element_type`, analogous to `LimitedOrdered`. Its shared lookup,
parent traversal, and iterators operate on elements. For sets, `element_type` is `const key_type`.
For maps, `mapped_type` is the caller's `Value`, and `element_type` is
`std::pair<const std::string_view, Value>` for view keys or
`std::pair<const std::string&, Value>` for owning keys. A view is stored directly without another
indirection; an owning key is referenced without copying its string. Keys stay immutable, including
the view descriptor: reassigning it would make the element disagree with the index. `value_type`
is `element_type` with any top-level const removed.

Non-const map iteration yields `element_type&`, while const iteration yields
`const element_type&`. `get(id)` returns `mbo::types::OptionalRef<element_type>` from a mutable map
and `mbo::types::OptionalRef<const element_type>` from a const map or a set. Missing IDs return
an empty reference. `get_string(id)` provides an optional string view in either wrapper.
`at(id)` and `operator[](id)` return mapped-value references with the same constness. Both require
an existing ID, never insert, and use `MBO_CONFIG_REQUIRE` for an invalid ID. There are no string
overloads of these accessors; `find(key)` and `rfind(key)` explicitly obtain an ID from a string.

Both wrappers default to `absl::flat_hash_map<std::string_view, Id, Hash>` as their index. Map
values live in the dense element sequence, independently of this string-to-ID index.
`insert(key, value)` adds a map element; `try_emplace(key, args...)` constructs its value in place,
including immovable types. Duplicate insertion keeps the existing value and does not consume
the supplied value or construct a replacement, including for inherited entries.
Public callers choose `StringInternerSet` or `StringInternerMap` directly; there is no
`StringInterner` alias.

Custom indexes use ordinary associative-container operations: `find`, `end`, and `emplace`.
They also support iterator-based `erase` to discard an unpublished index entry if a mapped-value
constructor throws; successful entries are never erased. Standard `std::map`,
`std::unordered_map`, `std::set`, `std::unordered_set`, and Abseil maps or sets can serve as either
wrapper's index. The `Hash` parameter selects the default index's hasher; an explicitly supplied
container selects its own hash or comparison type.

`StringInternerContainerTraits<Container>` distinguishes key/value maps from key-only sets by the
presence of `mapped_type`. Its key accessor selects a map entry's `first` member or a set entry
itself. `StringInternerKey<Container>` extracts the key type and removes its top-level `const`
qualification. `StringInternerStringContainer` validates string keys and lookup independently of
any mapped value. Stored keys must remain const through mutable iterators, as they do in standard
maps and sets; const-qualified key-type aliases are also accepted.

`StringInternerIdIndex<Container, Id>` checks insertion for both wrappers. Map-like indexes map
keys to IDs and use `emplace(key, id)`; key-only indexes use `emplace(key)`. Key-only indexes
recover IDs by scanning local dense elements within the captured cutoff. The default string-to-ID
index avoids that scan. ID lookup always selects a dense element directly, without an index lookup.
String lookup accepts `std::string_view` without constructing an owning string. Owning standard
containers therefore need heterogeneous lookup, such as `std::less<>` or a transparent hash and
equality pair.

The key type selects ownership, enforced by template constraints. A `std::string_view` key requires
separate `Storage`, which defaults to `ArenaStringStorage<>`; `Storage = void` is rejected. A
`std::string` key owns its bytes and requires `Storage = void`; supplying separate storage is
rejected. These rules also apply to const-qualified keys and to both maps and sets.

Owning containers must preserve key addresses during insertion, including for short strings.
Flat-hash, C++23 flat-associative, and unknown custom containers are rejected at compile time for
owning keys because growth may move their keys. Flat containers with `std::string_view` keys remain
supported because separate storage owns the characters. Standard node containers and Abseil node
containers are explicitly recognized as stable. Other custom indexes must explicitly provide the
`kStringInternerStableKeyAddresses` trait marker only when they provide the same lifetime guarantee.
`LimitedMap` and `LimitedSet` currently relocate elements during insertion and therefore do not meet
this requirement for owning keys, even when their capacity is fixed.
Dense entries default to `SegmentedVector`, which preserves element
addresses during growth. A view set stores string views; an owning set stores pointers to const
strings. A map stores optional elements so it can reserve an unpublished slot without constructing
a mapped value. Custom entry containers must store that corresponding entry type and support
bidirectional iteration, `size()`, default `emplace_back()`, and mutable `back()`. A fallible
`try_emplace_back()` is used when available.

The parent constructor forwards additional arguments to the string container. They must construct
an empty index. For example, a stateful default-index hasher can be supplied as
`StringInternerSet<Id, MyHash> strings(nullptr, 0, MyHash{seed})`. Both wrappers inherit this
constructor and the base's parent-prefix behavior. The same constructor supports a
container's comparator or allocator arguments. Hashing and allocating operations retain the
selected container's exception behavior; string lookup is not forced through a `noexcept` adapter.
ID lookup is `noexcept`. If a mapped-value constructor throws, only the new index entry is erased;
character bytes and the reserved dense slot are retained for the next attempt.

`insert` returns `std::expected<std::pair<Id, bool>, StringInternerError>`. Success contains the ID
and whether this call inserted a new string; a duplicate returns its existing ID and `false`.
Failure contains an enum identifying the exhausted component. Returning either result does not
format or allocate an error message. Fully bounded configurations must also supply backends that
do not allocate; the result type alone does not make the default, growing containers allocation-free.

Status-based callers explicitly use `ToStatusOr(interner.insert(key))`. `ToStatusCode(error)`
provides the enum mapping used by that conversion:

| StringInternerError          | absl::StatusCode     |
| ---------------------------- | -------------------- |
| `kIdExhausted`               | `kOutOfRange`        |
| `kCharacterStorageExhausted` | `kResourceExhausted` |
| `kEntryStorageExhausted`     | `kResourceExhausted` |
| `kIndexExhausted`            | `kResourceExhausted` |

Converted errors have empty messages and no payloads. Callers that need the exact exhausted
storage component inspect the enum before conversion; all three storage errors share a status
code. Neither result representation is claimed to be faster without measurements.

Character storage only needs `try_store`; it is append-only from the interner's perspective. If
indexing fails after a copy, those bytes may remain allocated until destruction. The interner does
not reclaim them or publish an ID for them. An `emplace` result with `second == false` for a missing
key reports index exhaustion, allowing bounded indexes to reject insertion. The interner stores a
element referring to the canonical key after successful indexing.

Both ownership modes reserve at most one unpublished dense entry and reuse it after a failed
attempt. `size()`, iteration, and ID lookup include only successfully published entries. Entry
containers need append and a writable `back()`; neither entry removal nor character-storage
rollback is required. Reserving the entry before storing bytes also ensures entry exhaustion cannot
consume character capacity. Owning mode has no separate character-storage object or runtime
ownership branch. The entry sequence is destroyed before the string container, and any separate
character storage is destroyed last.

The interner implements captured parent cutoffs, forward/reverse lookup, whole-chain bidirectional
iteration yielding string views for sets or key/value pairs for maps, and append-only insertion
that publishes an ID only after
indexing succeeds. Declared empty parents retain their identity; a zero local starting ID does not
imply a null parent. Internal ordinals use unsigned `size_type`; negative signed IDs are rejected
before conversion or ancestor traversal. Ancestor cutoffs can be passed directly during lookup
because each parent's local starting ID is at most its child's.

Extended bounded-allocation configurations and performance tuning remain outstanding. Forward
lookup currently recurses through ancestors, while reverse lookup iterates. These are initial
algorithms, not benchmark-selected strategies.

The initial [`ArenaStringStorage`](arena_string_storage.h) adapter implements byte
ownership independently of the index. `try_store` copies exactly the view length,
including embedded NUL bytes, without adding a terminator. Empty strings need no
allocation. Exhaustion returns an empty optional; the interner uses the adapter only
for append-only storage and keeps successful views until destruction. The adapter
is neither copyable nor movable and requires external synchronization. Its arena
type is configurable. Unexpected arena exceptions terminate through the adapter's
`noexcept` boundary; it does not convert exceptions into allocation-failure results.
This is an initial implementation, not a benchmark-selected storage strategy.

An interner consists conceptually of three facilities:

1. stable character storage that owns the bytes behind returned `std::string_view` values;
2. an ordered table mapping dense identifiers to elements (keys or key/value pairs);
3. a heterogeneous string index mapping content to identifiers.

The character storage and index can be separate or combined in an owning container. An arena can
eliminate per-string character allocations, but it does not make a node-based hash table
allocation-free. A fully bounded interner must bound and supply storage for both the character data
and every index/table allocation.

The ordered ID-to-element table naturally wants `SegmentedVector`, an append-oriented container made
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
For `StringId`, the largest underlying representation value is reserved as `invalid_value`, making
a default-constructed ID explicitly invalid without sacrificing zero-based dense assignment.

The interner accepts either a plain non-boolean integral ID or `StringId` with a signed or unsigned
integral representation. Signed types permit application-defined negative, pre-known values while
the interner continues assigning its dense local range from zero. `StringId` prevents accidental
arithmetic and reserves its maximum representation value as invalid; a plain integral uses every
nonnegative representable value. ID exhaustion is detected before mutating storage or the index.
The default ID type is `StringId<std::uint32_t>`.

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
[`mbo::types::ConstStrongId`](../types/strong_id.h) to non-boolean integral representations. It
provides explicit underlying-value access, ordering, constexpr checked ordinal
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

For maps, captured entries share their mapped values rather than copying them. Changes through
a parent, child, or sibling are visible to every interner that sees that ID. The snapshot fixes
membership and ID ownership, not a historical copy of mapped values. Consequently map parents
must be mutable; a map child cannot be constructed from a pointer to a const parent. Sets still
accept const parents. Const access to either a map or its parent exposes only const references.

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

Insertion has one operation, `insert(...)`. It reuses the ID of any string visible through the
captured parent prefix or local entries; otherwise, it adds the string to the current interner.
It never inserts into a parent. The result reports the ID and whether a new local entry was added.
Duplicate lookup currently checks the current interner before its ancestors. That traversal order
is an implementation detail; callers do not choose an insertion direction.

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
strings. Appending preserves string views and ordinal iterators; as with other growing containers,
an iterator that previously represented `end()` need not become the new end. References to map
pairs and mapped values follow the dense entry container's invalidation rules. The default
`SegmentedVector` preserves them even when the flat string index grows. A custom `std::vector`
entry sequence can invalidate them on reallocation.

Dereferencing a set iterator yields a const string key reference; a map iterator yields a
string/value pair reference. Both support `operator->`. Mutable map iterators allow changes to the
mapped value; const iterators make the entire pair const. Mutable iterators convert to const
iterators, and the two kinds compare directly. `cbegin`, `cend`, `crbegin`, and `crend` always
provide const access.

An iterator's `id()` exposes its current dense position as `id_type` without hashing or searching.
It has the same dereferenceable-iterator precondition as `operator*`. This supports occasional
`(id, string)` or `(id, string, value)` access without larger ordinary iteration results:

```cpp
for (auto position = counts.begin(); position != counts.end(); ++position) {
  const auto id = position.id();
  const auto& [key, value] = *position;
  // Use id, key, and value together only where needed.
}
```

IDs remain element ordinals; arena byte offsets would make the public representation and
capacity depend on variable-length storage.

### Object and view lifetime

StringInterner is non-copyable and non-movable. It provides neither move construction nor move
assignment, and it cannot be swapped. This anchors every parent reference and avoids a heap control
block, child registry, relocation tracking, or other overhead solely to make object movement safe.

There is no `clear` or `reset`: the interner is append-only until destruction. Destruction
invalidates every view, reference, and iterator it produced and remains forbidden while any child exists. The
child-before-parent destruction requirement is a documented lifetime precondition because the
parent intentionally does not pay to track children.

## Customization

One allocator-shaped abstraction is insufficient because customization covers more than acquiring
raw memory. The public design should permit separate policies or compatible implementations for:

- the hash function and transparent equality;
- the string-to-ID index;
- stable character storage;
- the ID-to-element sequence;
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
dense ID-to-element sequence, and lookup results are converted from an index entry to that dense
position. No iterator from `std::unordered_map`, an Abseil container, HAMT, or another selected index
escapes the interner. The index therefore need not provide stable iterators; stable strings and
public interner iterators come from character storage and the dense sequence respectively.

### Owning-string insertion

An owning string container is an alternative to a borrowing index with separate arena storage.
Both use `insert(std::string_view)`; the owning container copies into its own storage and returns
the canonical view, while an arena backend copies into the separate arena. The choice affects
allocation strategy, per-entry storage, and locality; benchmarks must compare complete backend
configurations before selecting a performance default.

An overload accepting an rvalue `std::string` remains a possible capability of a selected backend:

- an owning-string backend may adopt the moved string's allocation where it can preserve the
  character address;
- an arena backend copies the bytes into arena storage, even when its input is an rvalue;
- a bounded backend succeeds only when its supplied capacity can hold the bytes and index entry.

Small-string optimization means moving a `std::string` does not universally transfer a stable heap
allocation. An owning-string backend must choose a representation whose later growth or relocation
cannot invalidate views into short strings. A container that moves its stored string objects during
growth cannot satisfy the owning contract unless it separately guarantees stable character storage.

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
character capacity, entry capacity, index capacity, or identifier range. A failed insertion
publishes no ID or index entry and leaves previously published strings unchanged. Append-only
character storage may retain bytes from a failed attempt; the unpublished dense slot is reused by
the next attempt. Capacity reclamation is a backend choice, not an interner requirement.

The lightweight error enum distinguishes `kIdExhausted`, `kCharacterStorageExhausted`,
`kEntryStorageExhausted`, and `kIndexExhausted`. Allocation and block-source failures reported by a
backend map to the storage component whose capacity could not grow; ordinary container exceptions
retain their existing behavior.

### Failure APIs

`insert` currently provides an expected result, including the inserted flag; `ToStatusOr` is an
explicit adapter. The following broader interface forms remain comparison candidates:

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

## Operations

Both wrappers return `std::expected<std::pair<Id, bool>, StringInternerError>` from insertion.
`find(key)` and `rfind(key)` return `std::optional<Id>` and differ only in ancestor traversal order.
Iteration follows dense ID order in either wrapper.

```cpp
// StringInternerSet
insertion_result insert(std::string_view key);
const_lookup_result get(id_type id) const;

// StringInternerMap: mapped_type is the caller-supplied Value.
insertion_result insert(std::string_view key, auto&& value);
insertion_result try_emplace(std::string_view key, auto&&... args);
lookup_result get(id_type id);
const_lookup_result get(id_type id) const;
mapped_type& at(id_type id);
const mapped_type& at(id_type id) const;
mapped_type& operator[](id_type id);
const mapped_type& operator[](id_type id) const;

// Both wrappers
std::optional<std::string_view> get_string(id_type id) const;
std::optional<id_type> find(std::string_view key) const;
std::optional<id_type> rfind(std::string_view key) const;
size_type size() const;
size_type local_size() const;
bool empty() const;
```

Map lookup returns an optional reference to the complete key/value entry, preserving the map's
constness. No lookup copies or default-constructs the mapped value. Insertion into an existing
entry preserves the prior value. Assignment through `at(id)`, `operator[](id)`, a mutable lookup
result, or a mutable iterator changes the existing mapped value without changing its key or ID.

## Serialization

No dedicated serialization format or API is planned initially. Dense iteration already lets a
caller serialize strings in ID order; reinserting that sequence reconstructs the same dense IDs
without coupling the public contract to an index or arena representation.

## Correctness invariants

- Every visible valid ID maps to exactly one byte string.
- Every visible byte string maps to exactly one ID within one interner view.
- Re-interning a visible string returns its existing visible ID and does not allocate.
- New local IDs form a contiguous suffix after the captured parent cutoff.
- Parent insertion after child creation cannot change the child's string-to-ID mapping; mapped
  values of visible entries remain shared.
- Hash collisions never merge unequal strings.
- Successful insertion leaves all previously returned views valid.
- Failed insertion leaves published IDs, lookups, and iteration unchanged; backing storage may
  retain allocations made during the attempt.
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
- which owning-string and separate-storage backend configurations win for each workload.

## Language baseline

The repository targets C++23 with its supported GCC and Clang toolchains. The implementation may
use C++23 facilities such as `std::expected`, static call/index operators, `if consteval`, and the
relaxed constexpr rules where they materially simplify the code or API. Their availability is not
by itself a reason to expose a facility or add an abstraction: selected uses still require focused
tests on the supported compiler matrix and a concrete readability, correctness, or generated-code
benefit.
