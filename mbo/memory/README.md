# `mbo/memory`

`mbo::memory` provides aligned raw memory blocks and an arena for allocating byte ranges from them.
It supports heap, allocator, memory resource, caller-owned, and inline storage, with configurable
arena growth, reuse, and allocation accounting. Typed object construction and destruction remain
the caller's responsibility.

- Namespace: `mbo::memory`.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                           | Bazel target                   | API                                                               |
| -------------------------------- | ------------------------------ | ----------------------------------------------------------------- |
| [block_source.h](block_source.h) | `//mbo/memory:block_source_cc` | `MemoryBlock`, source concepts, five block-source implementations |
| [arena.h](arena.h)               | `//mbo/memory:arena_cc`        | `Arena<Source, Options>`, `ArenaOptions`, `ValidArenaOptions`     |

## Blocks and source concepts

| API                           | Contract                                                                                                                                                               |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MemoryBlock`                 | `std::byte* data`, byte `size`, and `alignment`; default is an empty descriptor                                                                                        |
| `BlockSource<Source>`         | `TryAcquire(size, alignment)` returns `std::optional<MemoryBlock>`; `Release(block)` and `max_alignment()` are nonthrowing; static `supports_recoverable_failure` flag |
| `CopyableBlockSource<Source>` | `BlockSource` plus `CopyForContainer() const` returning a new `Source`                                                                                                 |

- Successful acquisition: suitable raw storage of at least the requested size and alignment.
- Release: return the descriptor to the source responsible for its acquisition.
- Block descriptors: do not release storage on destruction.
- Caller-owned storage and memory resources: must outlive their users and outstanding allocations.

## Block-source implementations

| Source                               | Storage                                                   | Maximum alignment                                         | Recoverable allocation failure | `CopyForContainer` |
| ------------------------------------ | --------------------------------------------------------- | --------------------------------------------------------- | ------------------------------ | ------------------ |
| `NewDeleteBlockSource`               | Aligned nothrow `operator new/delete`                     | Largest representable power of two                        | Always                         | Yes                |
| `FixedBlockSource`                   | Caller-owned `std::span<std::byte>`                       | Constructor argument; default `alignof(std::max_align_t)` | Always                         | No                 |
| `InlineBlockSource<Size, Alignment>` | Embedded byte array                                       | `Alignment`; default `alignof(std::max_align_t)`          | Always                         | No                 |
| `AllocatorBlockSource<Allocator>`    | Allocator traits; default allocator of `std::max_align_t` | Alignment of allocator `value_type`                       | With C++ exceptions enabled    | Yes                |
| `PmrBlockSource`                     | Borrowed `std::pmr::memory_resource`                      | Largest representable power of two                        | With C++ exceptions enabled    | Yes                |

- Fixed and inline sources: one outstanding block; successful acquisition returns the whole buffer.
- Fixed source: buffer address must satisfy the requested alignment; no automatic pointer adjustment.
- Inline source: positive size and power-of-two alignment; copying and moving disabled.
- Allocator source: `alignof(value_type) >= alignof(std::max_align_t)`; allocation rounded to whole values.
- Allocator/PMR sources: catch `std::bad_alloc` only in exception-enabled builds.
- Allocator copy policy: `select_on_container_copy_construction`.
- PMR copy policy: retain the same resource pointer.
- Source inspection: `allocator()` on allocator sources; `resource()` on PMR sources.

## Arena policy

```cpp
#include "mbo/memory/arena.h"
#include "mbo/memory/block_source.h"

inline constexpr mbo::memory::ArenaOptions kArenaOptions{
    .initial_block_size = 4'096,
    .maximum_block_size = 65'536,
    .growth_numerator = 2,
    .growth_denominator = 1,
};

using ByteArena = mbo::memory::Arena<mbo::memory::NewDeleteBlockSource, kArenaOptions>;
```

- Both source type and options: explicit template arguments.
- Zero-initialized `ArenaOptions{}`: invalid.
- Example values above: a caller-selected policy.

| Option               | Validity constraint           | Meaning                             |
| -------------------- | ----------------------------- | ----------------------------------- |
| `initial_block_size` | Greater than `sizeof(void*)`  | First normal backing-block request  |
| `maximum_block_size` | At least `initial_block_size` | Upper limit for normal block growth |
| `growth_numerator`   | At least `growth_denominator` | Numerator of the growth ratio       |
| `growth_denominator` | Nonzero                       | Denominator of the growth ratio     |

- `ArenaOptions::IsValid()` and `ValidArenaOptions<Options>`: policy validation.
- Normal block growth: integer multiplication/division, clamped between current and maximum size.
- Oversized requests: dedicated larger block; normal growth sequence unchanged.
- Backing-block size: includes arena metadata and alignment overhead.

## Arena operations

| Operation                                       | Behavior                                                                                    |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Default construction                            | Default-construct the source; no blocks acquired                                            |
| Construction from `const Source&` or `Source&&` | Copy or move the source when supported                                                      |
| `Allocate(size, alignment)`                     | Return aligned bytes; requirement failure on unsuccessful allocation                        |
| `TryAllocate(size, alignment)`                  | Return aligned bytes or null; available only when the source advertises recoverable failure |
| `Reset()`                                       | Invalidate allocations, rewind cursors, retain every block for reuse                        |
| `Release()`                                     | Invalidate allocations, release every block, restore initial growth state                   |
| `bytes_used()`                                  | Requested bytes plus consumed allocation padding                                            |
| `bytes_reserved()`                              | Total bytes in acquired blocks, including metadata and unused storage                       |
| `block_count()`                                 | Number of acquired blocks, including blocks retained by reset                               |
| `source()`                                      | Mutable or const reference to the underlying source                                         |

- Default allocation alignment: `alignof(std::max_align_t)`.
- Preconditions: `size > 0`; alignment nonzero and a power of two.
- `TryAllocate`: null for overflow, unsupported alignment, or reported source exhaustion.
- Invalid preconditions: [configured requirement failure](../config/README.md#requirement-failures),
  including for `TryAllocate`.
- Consumer source exceptions: propagate; `TryAllocate` is not universally nonthrowing.
- Failed allocation: arena cursors, counters, block ownership, and growth state unchanged.
- Counters: constant-time queries; `Reset()` sets used bytes to zero and retains reserved bytes/block count.
- Successful allocations: stable addresses until reset, release, destruction, or release of their owning state.

## Ownership and lifetime

- Raw storage only: no typed construction API, destructor registry, or per-allocation deallocation.
- Objects constructed in arena storage: caller destroys them before reset, release, or destruction.
- Arena copying: disabled.
- Moving and swapping: constrained to nonthrowing source transfer or swap; preserve transferred addresses.
- Move assignment: releases the destination's old blocks first.
- Inline-source arena: immovable and unswappable.
- Thread synchronization: caller-owned; no internal locks or atomics.
- Allocation: runtime operation; constexpr options do not imply constexpr byte allocation.

## Tests and benchmarks

```sh
bazel test --config=clang //mbo/memory:all
bazel run -c opt --config=clang //mbo/memory:arena_benchmark
bazel run -c opt --config=clang //mbo/memory:arena_layout_benchmark
```

- [block_source_test.cc](block_source_test.cc): acquisition, alignment, release, source constraints.
- [arena_test.cc](arena_test.cc): growth, exhaustion, reuse, moves, and accounting.
- [arena_exceptions_test.cc](arena_exceptions_test.cc): consumer-source exceptions and requirement failures.
- [Measurements](measurements/README.md): retained benchmark data and commands.
