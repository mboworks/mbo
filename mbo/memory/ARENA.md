# Arena design

This document specifies the planned general arena component. Its proposed package is `mbo::memory`
because aligned storage acquisition and region lifetime are memory-management facilities rather
than container or string semantics.

The string interner requires an arena-backed character store, and `SegmentedSequence` may share a
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
3. A deferred object-lifetime layer constructs objects and, when required, records destruction.
4. Domain storage, such as the interner's character store, defines record layout and indexing.

This separation permits a minimal character arena without forcing destructor-registration overhead
onto every allocation. The initial implementation is the raw byte `Arena`. A distinct `ObjectArena`
with typed construction and reverse-order destructor registration is deferred until a concrete use
requires it.

## Byte-arena contract

The core operation requests a byte count and alignment. On success it returns only a non-null
aligned pointer. The contract guarantees that at least the requested number of bytes are available
at that pointer until the region is released; returning the already-known size would add needless
result width and work. The requested size must be greater than zero. C++ complete objects that can
have distinct addresses occupy at least one byte, and the arena does not invent address identity
for a zero-byte allocation. StringInterner represents an empty string without allocating character
bytes.

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
void Release();
```

`TryAllocate` returns null on failure in this sketch. The final failure-aware API may instead use an
optional or typed error result only if measurement and a concrete caller justify another adapter.
Invalid alignment, arithmetic overflow, configured exhaustion, and backing-source failure are
distinct internal conditions, but the base API deliberately reports only success or failure.
Not expressing failure means a documented hard failure such as termination, not undefined behavior
or an undersized allocation.

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

`Allocate` uses the configurable hard requirement. `TryAllocate` returns null without logging or
throwing. Invalid alignment is a programmer precondition failure rather than ordinary resource
exhaustion.

Alignment must be a nonzero power of two. Every block source supports at least
`alignof(std::max_align_t)` and advertises any greater supported maximum. An otherwise valid
alignment above that maximum returns null from `TryAllocate` and triggers the configured hard
failure from `Allocate`.

Allocation is transactional at the byte-tail level. Failed block acquisition, alignment, size
arithmetic, or capacity checks leave the cursor, padding, counters, retained blocks, and normal
growth-sequence position unchanged.

## Block sources and growth

A block-source concept should express acquisition, ownership, alignment, and release behavior. It
must be possible to provide:

- fixed caller-owned storage that never allocates;
- an allocator-backed growing source;
- a PMR-backed adapter;
- a source using a constexpr-compatible fixed representation;
- test and benchmark sources that count every acquired byte and block.

Block growth uses only strategies justified by benchmarks. As with `SegmentedSequence`, constexpr
options may stop after a size list, repeat its final size, or transition to another growth strategy.
A request larger than the next normal block receives a dedicated oversized block and does not
advance or otherwise distort the normal growth sequence.

The policy must guard addition, multiplication, alignment rounding, and representation conversion
against overflow before acquiring or committing storage.

## Deferred `ObjectArena`

A future typed adapter may provide `Create<T>(args...)`. Construction failure must not publish the
object; whether it may consume otherwise unreachable tail bytes depends on the arena's rollback
contract.

For trivially destructible objects, no destructor metadata is needed. `ObjectArena` registers
non-trivial destructors and invokes them in reverse construction order during reset, release, or
destruction. Destructor registration costs space and time per object and changes reset complexity;
none of that state or work is present in raw `Arena`.

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

## Relationship to `SegmentedSequence`

Both components can acquire a chain of blocks. Their public contracts remain distinct:

- the arena suballocates variable-size, variably aligned byte ranges and uses region lifetime;
- `SegmentedSequence<T>` owns uniformly typed element slots, manages each `T`, and provides dense
  indexed iteration.

A shared block-chain primitive should begin as a private implementation detail. It becomes public
whenever measurements or implementation experience show that direct reuse or customization makes
the library materially better. Publication is driven by demonstrated value, not by an arbitrary
minimum number of internal users, and must not leak one component's semantics into another.

`Reset()` retains reusable normal and option-selected oversized blocks while resetting allocation
state. `Release()` returns every backing block to its source and restores the arena to its initial
empty state. The exact automatic retention limits are selected through `ArenaOptions` only when
benchmarks demonstrate useful alternatives.

Move and swap availability depends on storage. Arenas owning growing or external blocks may move
and swap while preserving addresses within the transferred blocks. Move assignment first releases
the destination's old state and therefore invalidates pointers into that old destination. An arena
with an inline fixed buffer is immovable and unswappable because moving its embedded bytes would
invalidate their addresses. Concepts and conditional special members expose these distinctions at
compile time.

Arena operations use external synchronization. The implementation adds no locks or atomics;
concurrent access, including allocation concurrent with observation, requires synchronization by
the caller.

Three constant-time counters are part of the basic contract:

- `bytes_used()` includes requested bytes and alignment padding consumed in active blocks;
- `bytes_reserved()` includes all bytes in active and retained reusable blocks;
- `block_count()` counts active and retained backing blocks.

These values are already inherent in block and cursor management and must not add a per-allocation
branch or atomic operation. Allocation count and the sum or distribution of requested sizes would
require additional hot-path updates, so they belong to opt-in diagnostics rather than the base
representation.

## Required guarantees

| Area                 | Decision required                                                      |
| -------------------- | ---------------------------------------------------------------------- |
| Typed lifetime       | Raw `Arena` now; reverse-destruction `ObjectArena` deferred            |
| Failure result       | Null from `TryAllocate`; configured hard failure from `Allocate`       |
| Reset                | `Reset` retains reusable blocks; `Release` returns every backing block |
| Rollback             | Failed allocation leaves byte-tail state and growth position unchanged |
| Oversized allocation | Dedicated block without advancing the normal growth sequence           |
| Zero-size allocation | Programmer precondition requires a size greater than zero              |
| Maximum alignment    | At least `max_align_t`; source advertises any higher supported maximum |
| Move/swap            | Address-preserving for block-backed; forbidden for inline storage      |
| Thread safety        | External synchronization; no internal locks or atomics                 |
| Introspection        | `bytes_used`, `bytes_reserved`, and `block_count` in constant time     |
| Constexpr            | Exact fixed-storage operations supported during constant evaluation    |

## Measurements required

- allocation latency distributions for varied sizes and alignments;
- blocks acquired, bytes reserved, bytes used, padding, and fragmentation;
- fixed, repeated, listed, geometric, and oversized growth behavior;
- packed pointer descriptors, relative offsets, and inline string records;
- reset with no destructors, trivial objects, and registered non-trivial destructors;
- branch and code-size cost of each failure-result form;
- PMR, standard allocator, direct allocation, and caller-owned block sources;
- small-string-heavy, mixed, and large-record workloads;
- interaction with the string index and `SegmentedSequence` metadata table;
- exception-enabled and exception-disabled builds;
- single-threaded performance; synchronization remains the caller's responsibility.

The initial raw-arena semantic contract has no remaining open questions. Representation choices,
including pointer versus offset descriptors, growth and retention defaults, and whether the shared
block-chain merits a public API, remain benchmark decisions rather than missing semantics.

## Deferred work

- Implement and benchmark `ObjectArena` with typed construction and reverse-order destruction when
  a concrete user requires it.
- Design opt-in full diagnostics covering allocation-size histograms, requested bytes versus
  padding, fragmentation, block reuse, oversized allocations, and peak memory. When diagnostics
  are disabled they must contribute no fields, counter updates, branches, locks, or atomics to the
  production hot path.
