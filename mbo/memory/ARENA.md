# Arena design

This document specifies the general arena component in `mbo::memory`, where aligned storage
acquisition and region lifetime belong because they are memory-management facilities rather than
container or string semantics.

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
failure leaves the arena-owned cursors, counters, block chain, and growth position unchanged. A
consumer-provided source may update its own observable diagnostics before returning null or throwing.

`Arena` returns raw storage and does not track objects that callers construct in it. The caller must
destroy every live object placed in that storage before `Reset`, `Release`, or arena destruction.

The current byte arena exposes a hard-failing allocation operation, a recoverable operation when
the selected block source can report failure, lifetime operations, and constant-time counters:

```cpp
Arena();
explicit Arena(const Source& source);
explicit Arena(Source&& source);

std::byte* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
std::byte* TryAllocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

std::size_t bytes_used() const noexcept;
std::size_t bytes_reserved() const noexcept;
void Reset();
void Release();
```

The source constructors copy an lvalue source and move an rvalue source. Their availability and
`noexcept` specification follow the corresponding `Source` construction operation. Separate
reference overloads avoid passing an over-aligned source through a by-value parameter.

`TryAllocate` returns null for arithmetic overflow, source exhaustion, unsupported alignment, or a
backing-source failure. A zero size or an alignment that is zero or not a power of two violates an
API precondition and uses mbo's configured requirement failure. `Allocate` applies that same hard
failure policy when an otherwise valid request cannot be served. Neither operation returns an
undersized or insufficiently aligned allocation.

### Existing mbo failure-policy precedent

Existing bounded containers use `MBO_CONFIG_REQUIRE` when an operation cannot satisfy a capacity
precondition. It throws `std::runtime_error` only when mbo's exception policy is explicitly enabled
and otherwise terminates through fatal logging. `Allocate` uses the same mechanism, keeping the
arena consistent with `LimitedVector`, `LimitedMap`, and `LimitedSet`.

Resource exhaustion can also be ordinary control flow, particularly for a caller-supplied bounded
arena. `TryAllocate` therefore reports ordinary source exhaustion directly.
A nullable pointer carries the same success/failure information as `optional<std::byte*>` in a
smaller conventional representation; a separate diagnostic operation can return a typed reason if
real callers need it.

`TryAllocate` is not a universal no-throw boundary: programmer-precondition failures still use the
configured requirement policy, and an exception from a consumer-provided block source propagates.
The arena commits no cursor, counter, block-chain, or growth-state change before a new block is
successfully acquired and validated.

`Allocate` uses the configurable hard requirement. `TryAllocate` returns null for ordinary
exhaustion without logging; the precondition and source exceptions above remain exceptions to that
ordinary-failure path.

Alignment must be a nonzero power of two. Each block source advertises its supported maximum
alignment. An otherwise valid alignment above that maximum returns null from `TryAllocate` and
triggers the configured hard failure from `Allocate`.

Allocation is transactional at the byte-tail level. Failed block acquisition, alignment, size
arithmetic, or capacity checks leave the cursor, padding, counters, retained blocks, and normal
growth-sequence position unchanged.

## Block sources and growth

A block-source concept expresses acquisition, ownership, alignment, release behavior, and whether
resource failure is genuinely recoverable. A successful source result must truthfully describe
suitably aligned raw or byte-array storage of the reported size, with no live non-trivial object
occupying it. It must be possible to provide:

- fixed caller-owned storage that never allocates;
- an allocator-backed growing source;
- a PMR-backed adapter;
- a source using a constexpr-compatible fixed representation;
- test and benchmark sources that count every acquired byte and block.

`NewDeleteBlockSource`, `FixedBlockSource`, and `InlineBlockSource` use inherently non-throwing
acquisition and therefore always support `TryAllocate`. `AllocatorBlockSource` and
`PmrBlockSource` must call standard APIs whose failure contract is `std::bad_alloc`; their
`TryAllocate` is consequently available only in exception-enabled builds, where that exception can
be caught. In the repository's normal exception-disabled mode they support hard `Allocate` only.
This is a compile-time API distinction: mbo does not label an operation “try” when the selected
upstream API can terminate before returning failure.

`ArenaOptions` defines the initial and maximum normal block sizes and a rational growth factor.
It deliberately supplies no performance default: `ArenaOptions{}` is invalid, and every `Arena`
specialization names both its block source and a fully specified options value. This keeps the
correctness/API layer usable without selecting an unmeasured growth policy. A future default or
named policy alias requires comparable Apple M5 Pro and AMD Zen 5 evidence.
After each normal block, the implementation multiplies the next size by
`growth_numerator / growth_denominator`, rounds through integer arithmetic, and clamps the result
between the current size and `maximum_block_size`. A request larger than the next normal block
receives a dedicated oversized block and does not advance or otherwise distort that geometric
growth sequence. Listed, repeated-size, and hybrid policies are deferred alternatives, not behavior
of the current arena.

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

Both components can acquire multiple backing blocks through `BlockSource`. Their ownership and
indexing remain distinct:

- the arena suballocates variable-size, variably aligned byte ranges and uses region lifetime;
- `SegmentedSequence<T>` owns uniformly typed element slots, manages each `T`, and provides dense
  indexed iteration.

`BlockSource` is their shared allocation boundary; it does not require a shared chain
representation. Any deeper common primitive should begin as a private implementation detail and
become public only when measurements or implementation experience show material value without
leaking one component's semantics into another.

`Reset()` retains every acquired block, including oversized blocks, resets each cursor, and starts
reuse at the first block. `Release()` returns every backing block to its source and restores the
arena and its geometric growth state to their initial empty values. Selective oversized-block
retention and retention budgets are deferred alternatives; the current `ArenaOptions` has no
retention controls.

Move and swap availability depends on storage and on safe source ownership transfer. They
participate only when the corresponding source construction, assignment, or swap is non-throwing,
so block ownership and the source that must release those blocks cannot become separated by an
exception. Move assignment first releases the destination's old state and therefore invalidates
pointers into that old destination. An arena with an inline fixed buffer is immovable and
unswappable because moving its embedded bytes would invalidate their addresses. Concepts and
constrained special members expose these distinctions at compile time.

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
| Maximum alignment    | Source-specific and exposed by `max_alignment()`                       |
| Move/swap            | Address-preserving when source transfer/swap is non-throwing           |
| Thread safety        | External synchronization; no internal locks or atomics                 |
| Introspection        | `bytes_used`, `bytes_reserved`, and `block_count` in constant time     |
| Constexpr            | Options are constexpr; current raw-storage mutation is runtime-only    |

## Measurements required

- allocation latency distributions for varied sizes and alignments;
- blocks acquired, bytes reserved, bytes used, padding, and fragmentation;
- current geometric and oversized growth behavior, plus any proposed alternative growth policy;
- packed pointer descriptors, relative offsets, and inline string records;
- reset with no destructors, trivial objects, and registered non-trivial destructors;
- branch and code-size cost of each failure-result form;
- PMR, standard allocator, direct allocation, and caller-owned block sources;
- small-string-heavy, mixed, and large-record workloads;
- interaction with the string index and `SegmentedSequence` metadata table;
- exception-enabled and exception-disabled builds;
- single-threaded performance; synchronization remains the caller's responsibility.

The current raw-arena behavior is defined above. Representation choices, including pointer versus
offset descriptors, any future growth or retention defaults, and whether a deeper abstraction above
`BlockSource` merits a public API, remain benchmark decisions rather than missing semantics.
Callers currently select growth explicitly through `ArenaOptions`; the implementation does not
promote any measured candidate to a library default.

The public implementation targets the repository's C++23 baseline. The current raw byte-storage
representation still cannot create its intrusive `Block` object during constant evaluation. The
fixed source is therefore useful for allocation-free runtime operation, but the current arena does
not claim constexpr allocation. A future representation may add it without weakening the runtime
contract; the library must not pretend that merely marking a function `constexpr` proves constant
evaluability.

## Deferred work

- Implement and benchmark `ObjectArena` with typed construction and reverse-order destruction when
  a concrete user requires it.
- Design opt-in full diagnostics covering allocation-size histograms, requested bytes versus
  padding, fragmentation, block reuse, oversized allocations, and peak memory. When diagnostics
  are disabled they must contribute no fields, counter updates, branches, locks, or atomics to the
  production hot path.
