# Arena design

This document specifies the planned general arena component. Its proposed package is `mbo::memory`
because aligned storage acquisition and region lifetime are memory-management facilities rather
than container or string semantics.

The string interner requires an arena-backed character store, and `SegmentedVector` may share a
lower-level block source. The arena remains independently useful and independently benchmarked.

## Goals

- amortize many small allocations across a bounded or growing sequence of blocks;
- return suitably aligned storage whose address remains stable until the owning region is released;
- support caller-owned fixed storage with no hidden allocation;
- support configurable block growth through benchmark-proven constexpr policies;
- make exhaustion explicit and non-destructive;
- permit efficient storage of variable-length string records;
- expose narrow concepts so alternative block sources and arena implementations can be composed;
- remain usable with exceptions disabled.

## Layers

The design separates facilities that are often conflated under the name arena:

1. A block source acquires and releases backing blocks.
2. A byte arena suballocates aligned byte ranges from those blocks.
3. An optional object-lifetime layer constructs objects and, if required, records destruction.
4. Domain storage, such as the interner's character store, defines record layout and indexing.

This separation permits a minimal character arena without forcing destructor-registration overhead
onto every allocation. Whether the typed object-lifetime layer ships in version one remains open.

## Byte-arena contract

The core operation requests a byte count and alignment. On success it returns only a non-null
aligned pointer. The contract guarantees that at least the requested number of bytes are available
at that pointer until the region is released; returning the already-known size would add needless
result width and work. A zero-byte request needs an explicit canonical behavior and must not
accidentally consume unbounded metadata.

Successful allocations remain valid and do not move until `Reset`, destruction, or another
explicitly documented region-lifetime operation. Individual allocations cannot be freed. Allocation
failure leaves the arena's observable state unchanged.

Candidate operations are deliberately provisional. The fast form assumes that configured
exhaustion is a hard failure; the failure-aware form illustrates a nullable result:

```cpp
std::byte* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
std::byte* TryAllocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

std::size_t bytes_used() const noexcept;
std::size_t bytes_reserved() const noexcept;
void Reset();
```

`TryAllocate` returns null on failure in this sketch. The final failure-aware API may instead use an
optional or typed error result. Invalid alignment, arithmetic overflow, configured exhaustion, and
backing-source failure are distinct internal conditions; the public error granularity remains
unsettled. Not expressing failure means a documented hard failure such as termination, not
undefined behavior or an undersized allocation.

### Existing mbo failure-policy precedent

Existing bounded containers use `MBO_CONFIG_REQUIRE` when an operation cannot satisfy a capacity
precondition. It throws `std::runtime_error` only when mbo's exception policy is explicitly enabled
and otherwise terminates through fatal logging. Reusing that mechanism for hard-failing `Allocate`
would keep the arena consistent with `LimitedVector`, `LimitedMap`, and `LimitedSet`.

Resource exhaustion can also be ordinary control flow, particularly for a caller-supplied bounded
arena. `TryAllocate` must therefore avoid the requirement mechanism and report failure directly.
A nullable pointer carries the same success/failure information as `optional<std::byte*>` in a
smaller conventional representation; a separate diagnostic operation can return a typed reason if
real callers need it.

This is the current recommendation, not a settled decision: `Allocate` uses the configurable hard
requirement and `TryAllocate` returns null without logging or throwing.

## Block sources and growth

A block-source concept should express acquisition, ownership, alignment, and release behavior. It
must be possible to provide:

- fixed caller-owned storage that never allocates;
- an allocator-backed growing source;
- a PMR-backed adapter;
- a source using a constexpr-compatible fixed representation;
- test and benchmark sources that count every acquired byte and block.

Block growth uses only policies justified by benchmarks. As with `SegmentedVector`, a constexpr
policy may stop after a size list, repeat its final size, or transition to another policy. Requests
larger than the next normal block require a settled oversized-allocation rule: dedicate a block,
advance the growth sequence, or fail.

The policy must guard addition, multiplication, alignment rounding, and representation conversion
against overflow before acquiring or committing storage.

## Typed object lifetime

A typed adapter could provide `Create<T>(args...)`. Construction failure must not publish the
object; whether it may consume otherwise unreachable tail bytes depends on the arena's rollback
contract.

For trivially destructible objects, no destructor metadata is needed. Non-trivial objects require
one of these explicitly selected contracts:

- reject them at compile time;
- construct them but require the caller to destroy them separately;
- register destructors and invoke them in reverse construction order during reset/destruction.

Destructor registration costs space and time per object and changes reset complexity. It must not
be imposed on byte/string-only configurations unless measurement and real use justify it.

## String-record layouts

The interner requires both stable bytes and dense ID lookup. At least these layouts are compared:

| Layout               | Arena contents             | External metadata           | Lookup characteristic             |
| -------------------- | -------------------------- | --------------------------- | --------------------------------- |
| Packed bytes         | Concatenated string bytes  | `(size, pointer)`           | Direct; native-pointer metadata   |
| Packed bytes/offsets | Concatenated string bytes  | `(size, segment, offset)`   | Direct; potentially narrower      |
| Inline records       | Repeated `(size, content)` | Offset table or linear scan | Compact record; variable stepping |

Offsets may reduce metadata and improve relocatability, but introduce decoding and capacity limits.
No pointer-versus-offset choice is made until benchmarks cover realistic string-size distributions,
arena sizes, and lookup ratios.

## Relationship to `SegmentedVector`

Both components can acquire a chain of blocks. Their public contracts remain distinct:

- the arena suballocates variable-size, variably aligned byte ranges and uses region lifetime;
- `SegmentedVector<T>` owns uniformly typed element slots, manages each `T`, and provides dense
  indexed iteration.

A private shared block-chain primitive is plausible. A public common abstraction requires evidence
that it simplifies real customization without leaking one component's semantics into the other.

## Required guarantees to settle

| Area                 | Decision required                                                        |
| -------------------- | ------------------------------------------------------------------------ |
| Typed lifetime       | Raw bytes only or an optional destructor-registering object layer        |
| Failure result       | Sentinel, optional, status, expected-like result, or policy-selected API |
| Reset                | Retain all blocks, retain one block, or release all backing storage      |
| Rollback             | Whether failed construction/transactions reclaim tail bytes              |
| Oversized allocation | Dedicated block, growth-sequence interaction, or failure                 |
| Zero-size allocation | Canonical pointer/range and accounting behavior                          |
| Maximum alignment    | Supported bound and behavior for over-aligned requests                   |
| Move/swap            | Address stability, block-source propagation, and invalidation            |
| Thread safety        | External synchronization or specialized concurrent mode                  |
| Introspection        | Required byte/block counters without affecting hot paths                 |
| Constexpr            | Exact fixed-storage operations supported during constant evaluation      |

## Measurements required

- allocation latency distributions for varied sizes and alignments;
- blocks acquired, bytes reserved, bytes used, padding, and fragmentation;
- fixed, repeated, listed, geometric, and oversized growth behavior;
- packed pointer descriptors, relative offsets, and inline string records;
- reset with no destructors, trivial objects, and registered non-trivial destructors;
- branch and code-size cost of each failure-result form;
- PMR, standard allocator, direct allocation, and caller-owned block sources;
- small-string-heavy, mixed, and large-record workloads;
- interaction with the string index and `SegmentedVector` metadata table;
- exception-enabled and exception-disabled builds;
- single-threaded performance before considering synchronization.

## Open questions

1. Does version one expose raw aligned bytes only, or also typed object construction?
2. If typed construction is included, are non-trivial destructors registered automatically?
3. Which reset/block-retention strategies are required?
4. How do oversized allocations affect the normal growth sequence?
5. What is the primary failure result and which diagnostics are observable?
6. Is allocation transactional at the byte-tail level, or only at the caller-visible record level?
7. What maximum alignment must every block source support?
8. Are move and swap supported while preserving all allocation addresses?
9. Is thread safety entirely external in version one?
10. Which counters are always maintained, optional, or benchmark-only?
11. Does a shared block-chain primitive remain private to `mbo::memory` internals?
