<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Containers

`mbo::container` provides bounded inline containers, stable segmented sequences, and adapters for
container-independent iteration and conversion. The public experimental circular buffer lives in
`mbo::container::experimental`. All require the repository's C++23 compiler and library baseline:
GCC 15/libstdc++ 15, Clang 22/libc++ 22, or Xcode 16.3 with its matching Apple Clang/libc++.

## Choosing a container

| Type                                                      | Storage and purpose                                             | Supported mutation                                        |
| --------------------------------------------------------- | --------------------------------------------------------------- | --------------------------------------------------------- |
| `LimitedVector<T, N>`                                     | At most `N` elements stored inside the object                   | Append, suffix removal, positional insertion/erasure      |
| `LimitedSet<Key, N, Compare>`                             | Bounded sorted unique keys stored inline                        | Key insertion/erasure; keys are immutable                 |
| `LimitedMap<Key, Value, N, Compare>`                      | Bounded sorted unique keys with mapped values stored inline     | Key insertion/erasure and mapped-value updates            |
| `SegmentedVector<T, Options, Source, DirectoryAllocator>` | Fixed-size segments; append-heavy storage with stable addresses | Append, suffix removal, resize, reserve                   |
| `SegmentedDeque<T, Options, Source, DirectoryAllocator>`  | Fixed-size segments with reusable storage at both ends          | Front/back insertion/removal, resize, directional reserve |
| `experimental::CircularBuffer<T, Allocator>`              | A growable ring in one allocator-managed allocation             | Front/back and positional insertion/removal               |
| `AnyScan<T>`, `ConstScan<T>`, `ConvertingScan<T>`         | Type-erased input iteration over another container              | Access through references or converted values             |
| `ConvertContainer(source[, conversion])`                  | An adapter used to construct a destination container            | Converts and inserts each source element                  |

The owning containers provide random-access iterators, **not contiguous iterators**. None exposes
the complete sequence as a `T*`, `data()`, or `std::span<T>`. Inline storage does not establish a
single array of live `T` objects: the Limited containers manage individual union-slot lifetimes.
Segment views also make no contiguity promise. Use `std::vector` or another contiguous container
when that is a requirement.

## Headers and Bazel dependencies

Headers are included relative to the repository root. In a consuming Bazel module, prefix the
following labels with `@mboworks_mbo`; within this repository use the labels as shown.

| Header                                         | Bazel target                                      |
| ---------------------------------------------- | ------------------------------------------------- |
| `mbo/container/limited_vector.h`               | `//mbo/container:limited_vector_cc`               |
| `mbo/container/limited_set.h`                  | `//mbo/container:limited_set_cc`                  |
| `mbo/container/limited_map.h`                  | `//mbo/container:limited_map_cc`                  |
| `mbo/container/limited_options.h`              | `//mbo/container:limited_options_cc`              |
| `mbo/container/segmented_vector.h`             | `//mbo/container:segmented_vector_cc`             |
| `mbo/container/segmented_deque.h`              | `//mbo/container:segmented_deque_cc`              |
| `mbo/container/segmented_options.h`            | `//mbo/container:segmented_options_cc`            |
| `mbo/container/experimental/circular_buffer.h` | `//mbo/container/experimental:circular_buffer_cc` |
| `mbo/container/any_scan.h`                     | `//mbo/container:any_scan_cc`                     |
| `mbo/container/convert_container.h`            | `//mbo/container:convert_container_cc`            |

For example, a target using both sequences declares:

```starlark
deps = [
    "@mboworks_mbo//mbo/container:segmented_deque_cc",
    "@mboworks_mbo//mbo/container:segmented_vector_cc",
]
```

The examples below are exercised by `//mbo/container:container_readme_test`.

## Limited containers

`LimitedVector`, `LimitedSet`, and `LimitedMap` reserve their entire compile-time bound inside each
container object. They do not allocate container storage, though the elements themselves may
allocate. `size()` counts live elements; `capacity()` and `max_size()` report the fixed bound.
Zero capacity is supported. `reserve(n)` cannot increase the bound. Exceeding it is a requirement
failure, not an automatic transition to heap storage.

```cpp
#include <string>

#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"
#include "mbo/container/limited_vector.h"

mbo::container::LimitedVector<std::string, 4> tasks;
tasks.emplace_back("compile");
tasks.emplace_back("test");
auto task = tasks.pop_back_value();  // "test"; one task remains.

mbo::container::LimitedSet<int, 4> priorities{3, 1, 3};  // {1, 3}
mbo::container::LimitedMap<int, std::string, 4> names{{2, "two"}, {1, "one"}};
names.at(2) = "second";
```

Vector indexing and endpoint operations take constant container work. Middle insertion/erasure
and ordered-container insertion/erasure can move a linear number of elements. Ordered lookup uses
the configured comparator and optimized search routines; it should be measured for the actual
capacity and key distribution. `find`, `contains`, `index_of`, `lower_bound`, `upper_bound`, and
`at_index` support lookup without treating the storage as a node-based map or set.

Vector append preserves references to existing elements. Insertion and erasure can invalidate
references and iterators at or after the changed position. Ordered insertion/erasure can shift the
sorted suffix. Set keys and map keys cannot be modified through their iterators. Copy, move, and
swap operate on inline elements rather than transferring an allocation. Operations participate
according to the construction/assignment capabilities of the element; an immovable value can still
be emplaced directly into unused vector storage and removed from the back.

The capacity argument can instead be a `LimitedOptions<N, Flags...>{}` value. The options header
also provides `MakeLimitedOptions` and the corresponding option concepts.

| Flag                         | Effect                                                                                                                  |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `kDefault`                   | No additional behavior                                                                                                  |
| `kRequireSortedInput`        | Ordered-container range/list constructors require input already sorted by the comparator; checked only without `NDEBUG` |
| `kNoOptimizeIndexOf`         | Disable optimized ordered lookup                                                                                        |
| `kCustomIndexOfBeyondUnroll` | Select the custom ordered lookup beyond the unrolled capacity                                                           |
| `kEmptyDestructor`           | Suppress the normal destruction pass; only use when element cleanup is deliberately unnecessary                         |

Prefer the normal destructor. `kEmptyDestructor` is a specialized constant-evaluation/ASan escape
hatch, not a general performance option for owning values. The ordered lookup unroll bound is
configured by `--//mbo/config:limited_ordered_max_unroll_capacity`.

## Segmented sequences

Both segmented containers acquire fixed-size segments and keep an independently allocated pointer
directory. Growing the directory never relocates elements. Use `SegmentedVector` for append and
suffix removal; use `SegmentedDeque` when insertion/removal at the front is required. Neither
implements arbitrary middle insertion or erasure.

```cpp
#include "mbo/container/segmented_options.h"
#include "mbo/container/segmented_vector.h"

constexpr mbo::container::SegmentedOptions kOptions{
    .segment_size = 64, .segment_capacity = 8, .segment_reservation = 8};
mbo::container::SegmentedVector<int, kOptions> values;
values.emplace_back(7);
auto first = values.begin();
values.resize(130, 9);  // Acquire more segments without moving the first element.
const int original = *first;  // Still 7.
const int last = values.pop_back_value();  // 9.
```

`SegmentedOptions` is shared by both classes. Invalid options are rejected by template constraints.

| Field                 | Default    | Valid values and meaning                                                                      |
| --------------------- | ---------- | --------------------------------------------------------------------------------------------- |
| `segment_size`        | `256`      | Nonzero power-of-two elements per segment                                                     |
| `segment_capacity`    | `SIZE_MAX` | Nonzero power-of-two maximum segment count, or `SIZE_MAX` for the representation bound        |
| `segment_reservation` | `1`        | Zero or power-of-two initial directory reservation, no greater than a finite segment capacity |

Finite products must fit element-count and iterator-difference representations. Reservation alone
allocates directory slots, not elements or segments. Set `segment_reservation` to zero for
allocation-free empty construction. Setting it to a finite `segment_capacity` avoids subsequent
directory growth within that bound. Elements must be complete, cv-unqualified, non-array objects
with nonthrowing destructors; `SegmentedElement`, `ValidSegmentedOptions`, and
`RepresentableSegmentedOptions` expose these checks.

`reserve(n)` acquires segments for at least `n` total element slots. `clear()` destroys elements
while retaining storage; `trim_capacity()` returns empty segments; `release()` destroys elements
and returns all segments, retaining the directory allocation until destruction. `segments()`
exposes views of the live portions of segments, with separate invalidation rules from element
iterators. `bytes_reserved()` counts source-reported segment storage and excludes the directory.

For a deque, unused slots can be split across partially occupied endpoint segments. Use
`reserve_front(n)` or `reserve_back(n)` to reserve **additional** room at a specific end and
`front_capacity()` or `back_capacity()` to query it. Those capacities overlap through shared spare
segments and must not be added. A sliding window may need one extra segment. At a hard segment
bound, one end can be full even when the other end has unused slots.

Indexing, iterator arithmetic, and endpoint removal are constant time. Endpoint insertion is
amortized constant time for directory management, plus element construction and source allocation.
References and iterators to live elements survive endpoint growth, reservation, and trimming.
Removal invalidates the removed elements; size changes invalidate the old end iterator. Deque
front insertion also preserves existing element iterators. Move and swap invalidate container-owned
iterators. See the detailed [vector](SEGMENTED_VECTOR.md) and [deque](SEGMENTED_DEQUE.md) contracts
for allocation propagation, rollback, segment-view invalidation, and constant evaluation.

### Block sources and arenas

The third template parameter is a `mbo::memory::BlockSource`; the fourth is the directory allocator.
The default source is `NewDeleteBlockSource`. Other supplied sources are `AllocatorBlockSource`,
`PmrBlockSource`, `FixedBlockSource`, and `InlineBlockSource`. The last two permit only one outstanding
block and therefore support one-segment configurations. Source storage must also accommodate the
segment header, alignment, and all slots, not just `segment_size * sizeof(T)`.

```cpp
#include <memory_resource>

#include "mbo/container/segmented_deque.h"
#include "mbo/memory/block_source.h"

std::pmr::monotonic_buffer_resource resource;
using Queue = mbo::container::SegmentedDeque<
    int, mbo::container::SegmentedOptions{.segment_size = 64}, mbo::memory::PmrBlockSource>;
Queue queue{mbo::memory::PmrBlockSource(&resource)};
queue.reserve_back(128);
queue.push_back(1);
queue.push_front(0);
const int next = queue.pop_front_value();  // 0.
```

Here only segment storage uses the resource; a PMR directory allocator can direct the directory
there as well. The resource must outlive the container and must not be reset while it owns blocks.
Monotonic resources do not reclaim individual releases, so retain and recycle segments for steady
queue traffic. The separate `mbo::memory::Arena` owns raw bytes and requires an adapter implementing
`BlockSource` before use as a segment source; it is not itself that adapter.

`try_emplace_*` and `try_push_*` participate for recoverable sources and return an optional reference
on allocation success. They return empty for recoverable source exhaustion or the configured
segment bound; they do not suppress element-construction or directory-allocation exceptions.
Ordinary insertion reports exhaustion through the requirement policy. `unchecked_*` methods need
pre-reserved usable capacity at their selected end and never acquire a segment.

## Experimental CircularBuffer

`mbo::container::experimental::CircularBuffer` is public and usable independently of the deque.
Its experimental namespace and directory explicitly permit API changes between releases without
a compatibility alias or deprecation period.

```cpp
#include <memory>

#include "mbo/container/experimental/circular_buffer.h"

mbo::container::experimental::CircularBuffer<std::unique_ptr<int>> pending;
pending.reserve(3);  // Rounds to a power-of-two capacity of 4.
pending.push_back(std::make_unique<int>(2));
pending.push_front(std::make_unique<int>(1));
auto item = pending.pop_front_value();  // Owns 1.
```

This is a **growable** ring: pushing onto a full buffer preserves all existing values and grows
the allocation. It never silently overwrites an endpoint. Zero capacity is the empty unallocated
state; nonzero capacity is always a power of two, validated by a private capacity value type.
`reserve` creates storage without elements; `clear` retains it; `shrink_to_fit` reduces it to the
smallest fitting power of two or releases it if empty. The allocator parameter supports standard
allocators and PMR. There is no `BlockSource` parameter.

Both ends have push/emplace/pop operations. Positional insertion/erasure, range operations,
`resize`, checked `at`, indexing, all four iterator families, comparisons, copy/move, and swap
complete the STL-style interface. Indexing/removal are constant time; endpoint insertion is
amortized constant time. Growth and middle/bulk insertion take linear time and relocate elements.
Treat successful structural changes as invalidating buffer iterators. References survive
endpoint insertion without growth and removal of other elements. This differs from the stable
element iterators of `SegmentedDeque`. See [the full circular buffer contract](experimental/CIRCULAR_BUFFER.md).

## Value-returning pops

The following APIs follow
[P3182R1: Add container pop methods that return the popped value](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3182r1.html).
They supplement the existing void pops.

| Container                      | `pop_front_value()`                       | `pop_back_value()`                        |
| ------------------------------ | ----------------------------------------- | ----------------------------------------- |
| `LimitedVector`                | No                                        | Yes                                       |
| `SegmentedVector`              | No                                        | Yes                                       |
| `SegmentedDeque`               | Yes                                       | Yes                                       |
| `experimental::CircularBuffer` | Yes                                       | Yes                                       |
| `mbo::json::Json` array        | No                                        | Yes, returning `Json`                     |
| `mbo::types::ContainerProxy`   | When the underlying container supports it | When the underlying container supports it |

Each sequence method requires `std::move_constructible<T>` and a nonempty container. It directly
initializes the returned object from `std::move` of the endpoint and removes the element only
after that construction succeeds. There is one move even with optional copy elision disabled.
There is no `move_if_noexcept` copy fallback. If moving throws, size, capacity, element identity,
and ownership remain intact, but the throwing move may have modified the source value.
A subsequent assignment failure in caller code cannot undo a completed pop.

The operations retain container capacity and take constant container work plus element
construction/destruction. Sequence methods are `constexpr` where storage and element operations
permit, and `noexcept` only when moving and the configured requirement policy are nonthrowing.
Json requires an actual nonempty array: null, scalar, and object values are rejected without
conversion. A proxy forwards the available operation and accounts for the accessor and underlying
call in its exception specification. Ordered maps/sets and scanning/conversion adapters do not
gain endpoint pops.

Checked preconditions use `MBO_CONFIG_REQUIRE`, active in optimized builds. The default policy
logs a fatal error. With exceptions enabled and `--//mbo/config:require_throws=true`, it throws
`std::runtime_error`. This does not make unchecked indexing, iterator misuse, or ordinary STL-style
preconditions valid. No container adds internal synchronization; callers synchronize mutation.

## Scanning and converting containers

The scan types allow an API to accept different container representations without becoming a
function template. `AnyScan<T>` exposes compatible references, `ConstScan<T>` exposes const
references, and `ConvertingScan<T>` returns converted values without `operator->`.

```cpp
#include <string>
#include <string_view>
#include <vector>

#include "mbo/container/any_scan.h"
#include "mbo/container/convert_container.h"

const std::vector<std::string> words{"one", "two"};
mbo::container::ConstScan<std::string> scan = mbo::container::MakeConstScan(words);
std::size_t characters = 0;
for (const auto& word : scan) {
  characters += word.size();
}
const std::vector<std::string_view> views{"three", "four"};
std::vector<std::string> owned = mbo::container::ConvertContainer(views);
```

Use the matching `MakeAnyScan`, `MakeConstScan`, or `MakeConvertingScan` factory. Lvalue scans borrow
their source, which must outlive the scan and active iterators. Movable rvalue containers are kept
in shared ownership. Initializer lists remain borrowed; a temporary list is safe for an immediate
function call, not for a retained scan. Conversions to views also depend on their backing storage.
`size()` and `empty()` delegate to the underlying container.

Scanning uses type-erased input iterators, with allocation and indirect-call overhead. It does not
provide random access or independent multipass iterator copies. A fresh `begin()` can start a new
traversal of a reusable source. Exhausted iterators compare equal; live reference iterators compare
element addresses; live converting iterators compare unequal unless they are the same iterator
object. Do not use cross-range equality to identify a container.

`ConvertContainer` builds the selected output type through its available emplacement/insertion
interface. It borrows lvalues and owns moved-in rvalue containers. It forwards source elements as
rvalues, so a mutable source can be moved from; pass a const source to preserve owning elements.
An optional conversion callable transforms each element. Materialize the output while borrowed
containers, initializer lists, and any borrowed callable are still alive. Destination semantics
apply, including ordering, duplicate removal, allocation, and capacity limits.

## Validation and measurements

`bazel test //...` exercises the repository. The shared value-pop suite runs over every supported
sequence endpoint, including constexpr evaluation, move-only values, capacity retention, failed
moves, and live-object accounting. The exception target disables optional return-value elision:

```sh
bazel test --//mbo/config:require_throws=true \
  //mbo/container:value_pop_require_exceptions_test \
  //mbo/container/experimental:circular_buffer_require_exceptions_test \
  //mbo/json:json_require_exceptions_test
```

CI additionally runs the supported compiler/Bazel matrix, sanitizer builds, and coverage gates.
Performance evidence is collected separately for [ordered lookup](measurements/README.md),
[SegmentedVector](measurements/SEGMENTED_VECTOR.md), and [SegmentedDeque](measurements/SEGMENTED_DEQUE.md).
Results apply to the recorded source SHA, compiler, element type, allocator, and workload. Historical
`SegmentedSequence` measurements retain that name; the current API is `SegmentedVector` with no
compatibility alias. The former vector-specific options have become the shared `SegmentedOptions`.

## Experimental HAMT maps and sets

[The HAMT package](hamt/README.md) provides persistent flat/node maps and sets in
`mbo::container::hamt::experimental`, with shared snapshots, transient mutation, and configurable
block sources. Its headers and targets are under `mbo/container/hamt/experimental`; the API may
change between releases. HAMT-specific string-index adapters live in the same package.
