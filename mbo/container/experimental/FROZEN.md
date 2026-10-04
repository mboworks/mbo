<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# FrozenMap and FrozenSet

`mbo::container::experimental::FrozenMap` and `FrozenSet` are immutable, inline perfect
hash containers. They support both constexpr and runtime construction on MBO's C++23
baseline, and compile in C++26 mode. Their experimental API may change between releases.

```cpp
#include <array>
#include <string_view>
#include <utility>

#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_set.h"

using namespace std::string_view_literals;
using mbo::container::experimental::FrozenMap;
using mbo::container::experimental::FrozenSet;

constexpr auto kEntries = std::to_array<std::pair<std::string_view, int>>({
    {"-name", 1}, {"-n", 1}, {"-type", 2},
});
constexpr FrozenMap kOptions(kEntries);
constexpr FrozenSet kNames(std::to_array<std::string_view>({"-name", "-n", "-type"}));
static_assert(kOptions.at("-n") == 1);
static_assert(!kOptions.lookup("unknown"));
static_assert(kNames.contains("-type"));
```

The examples and interface are exercised by `:frozen_test`. Use Bazel dependencies
`//mbo/container/experimental:frozen_map_cc` and `:frozen_set_cc` (with
`@mboworks_mbo` before the labels in another module). `:frozen_options_cc` exports options
and default hashes independently.

## Construction and ownership

The template signatures follow the Limited containers:

```cpp
FrozenMap<Key, Value, CapacityOrOptions, Hash = FrozenHash<Key>, KeyEqual = std::equal_to<>>
FrozenSet<Key, CapacityOrOptions, Hash = FrozenHash<Key>, KeyEqual = std::equal_to<>>
```

An integer capacity selects default options. Alternatively pass a structural `FrozenOptions`
value. Array deduction guides and `MakeFrozenMap`, `MakeFrozenSet`, `ToFrozenMap`, and
`ToFrozenSet` infer capacity and element types from `std::array`. Explicitly typed containers
also accept iterator/sentinel pairs, initializer lists, and `std::from_range` ranges. The
empty constructor supports every capacity. Hash and equality objects can carry constexpr
state and can be passed after the input. Input elements are copied into inline storage;
move iterators can transfer move-only elements. Keys and mapped values must be object types,
not reference types; a reference member could remain writable through a const iterator.
Elements need not be default constructible.

Both keys and mapped values are read-only after construction. Copy construction owns an
independent copy of all elements and the precomputed index. Move construction of a frozen
container copies its const storage and therefore requires copyable elements. Assignment is
deleted. Input iteration order is retained, with duplicate elements removed; this order is
not sorted. References and iterators remain valid for the owning container's lifetime.

A string-view key borrows its backing bytes, which must outlive the table. Use literals or
other static constexpr backing storage for static tables. Embedded NUL requires an explicit
length or the `sv` suffix; conversion from a C string stops at its first NUL. Hashing treats
characters as unsigned bytes and compares the full string view on lookup. There is no
case folding, normalization, or mode inference. Build separate tables from each mode's
actual spellings. Different alias spellings may map to the same value; the same spelling
may map to different values in independently constructed tables.

Duplicate set keys collapse. Duplicate map keys collapse when their mapped values compare
equal; conflicting values fail construction. If the mapped type lacks equality, any duplicate
key fails with a dedicated diagnostic. Equivalent keys retain the first spelling and value.
As with standard hash containers, key equality must be an equivalence relation and equal
keys must hash equally, including heterogeneous lookup keys.

`FrozenHash` supports integral and enum keys and `std::string_view`. Other key types use a
custom hash object or a specialization. Hash results are converted to `std::uint64_t`.
Custom operations must produce identical results at runtime and during constant evaluation;
portable reproducibility also requires compiler-independent custom operations.

## Read-only C++26 interface

These containers provide the read-only unordered associative operations in the
[C++ working draft](https://eel.is/c++draft/unord.map.overview), with the following explicit
boundary. They are not full mutable standard containers. In particular, constexpr
construction does not imply immutability: freezing is a separate choice made by these types.

| Area             | Members                                                                                                                                                                   |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Types            | `key_type`, `value_type`, `hasher`, `key_equal`, `size_type`, `difference_type`, `reference`, `const_reference`, `pointer`, `const_pointer`; maps also have `mapped_type` |
| Iterators        | `iterator`, `const_iterator`, `local_iterator`, `const_local_iterator`; all are const forward iterators                                                                   |
| Iteration        | `begin`, `end`, `cbegin`, `cend`, including bucket overloads                                                                                                              |
| Size             | `empty`, `size`, `max_size`, `capacity`                                                                                                                                   |
| Lookup           | `find`, `contains`, `count`, `equal_range`, `index_of`                                                                                                                    |
| Optional access  | `lookup` returns `mbo::types::OptionalRef<const mapped_type>` for maps and `OptionalRef<const key_type>` for sets                                                         |
| Checked access   | Map `at`, and `at_index` on both containers                                                                                                                               |
| Hash observers   | `hash_function`, `key_eq`, `bucket`, `bucket_count`, `max_bucket_count`, `bucket_size`, `load_factor`, `max_load_factor`                                                  |
| MBO conveniences | `contains_all(range)`, `contains_any(range)`, `construction_work()`                                                                                                       |
| Comparison       | Content-based `==` and `!=`, independent of insertion order and capacity                                                                                                  |

Map `value_type` is `std::pair<const Key, Value>`; every exposed reference is const, including
the mapped value. Iterator aliases are identical to their const counterparts. There is no
allocator API, node handle, insertion, erasure, merge, inserting subscript, mutable load-factor
setter, reserve, rehash, or swap. There are no ordering observers or lower/upper bounds.
`lookup` uses MBO's optional-reference convention on the C++23 baseline rather than requiring
standard optional references. `find` returns `end()` on absence; `index_of` returns `npos`.
`at` and invalid index/bucket access use MBO's configured requirement policy, as Limited
containers do, rather than promising `std::out_of_range`.

Both `Hash::is_transparent` and `KeyEqual::is_transparent` enable heterogeneous overloads
when the operations accept that key. This includes `at`, `lookup`, `bucket`, and `index_of`.
The ordinary key overloads still allow normal implicit conversions. Forward iteration and
range algorithms work on const and non-const objects; modifying algorithms cannot write
through the iterators.

Each exposed bucket is one physical perfect-hash slot, containing zero or one element.
`max_load_factor()` is fixed at 1. An empty zero-capacity table has zero buckets and a zero
load factor; `bucket(key)` returns zero in that case, but no bucket index is valid for local
iteration. The sparse slot count is distinct from element capacity.

## Shared implementation and construction limits

`FrozenMap` and `FrozenSet` share `frozen_internal::FrozenTable`, analogous to the
Limited containers' shared implementation. A separate `FrozenIndex` knows only hashes and
element positions. It can be reused by a future mutable sibling without inheriting from a
frozen public type. A possible mutable family is `StaticHashMap` / `StaticHashSet`; those
types are not implemented here. Mapped-value updates would preserve the index, while key
insertion could require rebuilding it.

Construction hashes keys once, detects duplicates and full-hash collisions, groups keys by
a first-level bucket, and places the largest groups first using bounded displacement search.
Singletons fill the remaining slots directly. It then verifies the actual occupied slot of
every registered key. Distinct full hashes are not sufficient proof of perfect placement.
An unknown key can reach an occupied slot, so lookup always checks candidate equality.
Lookup requires hashing the query and at most one full key comparison; string lookup is
linear in the query length, not constant in byte length.

| Option          | Default | Meaning                                                           |
| --------------- | ------: | ----------------------------------------------------------------- |
| `capacity`      |       0 | Maximum number of distinct elements; at most 4,096                |
| `slots`         |       0 | Zero selects `2 * capacity`; explicit counts are at most 65,536   |
| `max_seed`      |     256 | Attempts per multi-key bucket; at most 65,536                     |
| `max_work`      |  65,536 | Shared entry/probe/placement-comparison budget; at most 1,048,576 |
| `max_key_bytes` |  65,536 | Total input string-view bytes, including duplicate inputs         |

Use `FrozenOptions{.capacity = N, .slots = N}` for a minimal table when all `N` entries
are unique. Smaller slot counts can hold fewer than capacity elements. Search failure has
no silent fallback: use more slots, more search budget, or a different hash. Distinct keys
with identical full hashes cannot be separated by this algorithm and are rejected.
Limits bound library work, not arbitrary user hash/equality execution or the compiler's
implementation-specific step accounting. Large inputs or enlarged limits can still need a
compiler with a larger constexpr budget. No compiler limits are raised by the tests.

Storage consists of optional inline element slots (so unused entries need no default
constructor), two arrays of 32-bit indices, hash/equality objects, size, and a work counter.
Construction uses bounded automatic scratch arrays. Static constexpr tables need no dynamic
initialization; lookup allocates no container memory. User-owned element types and custom
hash/equality functions retain responsibility for their own allocations and lifetimes.

## Validation and measurements

```sh
bazel test //...
bazel test --//mbo/config:require_throws=true \
  //mbo/container/experimental:frozen_require_exceptions_test
bazel build --config=clang-tidy //...
./compile_commands-update.sh
python3 mbo/container/experimental/frozen_compile_test.py
bazel run -c opt --config=clang //mbo/container/experimental:frozen_benchmark -- \
  --benchmark_min_time=0.1s --benchmark_repetitions=3 \
  --benchmark_out=/tmp/frozen-lookup.json --benchmark_out_format=json
python3 mbo/container/experimental/frozen_measure.py --output /tmp/frozen-measure
```

The compiler test first compiles a valid baseline, then verifies negative diagnostics,
sparse tables at maximum capacity within default constexpr limits, and C++26 interface
compilation. CI runs it after generating the compilation database; the
exception-policy coverage pass includes the construction and checked-lookup failures.
Ordinary tests cover byte strings, non-default-constructible types, all lookup operations,
transparent overload participation, minimal/sparse tables, aliases, independent vocabularies,
slot uniqueness, runtime/constexpr equivalence, and copies.

The lookup benchmark compares maps and sets, integer and string-view keys, 8/64/256 elements,
hits/misses/mixed queries, linear scans, Limited containers, and minimal/sparse perfect
layouts. All layouts use the same alternating hit/miss corpus and consume mapped values for
map lookups. It reports object bytes and frozen construction work alongside latency.

A local Clang 22.1.8 / macOS arm64 run of the 64-key string-map fixture required 437 work
units for the minimal index and 170 for the sparse index. Container sizes were 2,576 and
2,832 bytes respectively. This measured construction/storage tradeoff supports the sparse
default; it does not establish a universal winner. Compare with Limited containers too:
their inline storage can be considerably smaller. Startup measurements on this host were
noisy at millisecond scale and do not establish a startup improvement.

The measurement script uses the compilation database's actual Clang command and retains
raw time traces, section sizes, symbol listings, commands, and a JSON report. It compares
four standalone commands with identical query behavior. Compilation runs without an object
cache and includes trace instrumentation; `Total Evaluate*` events report constexpr work
and may overlap, so do not sum them. Object bytes include metadata; read-only section bytes
include relocated const data where reported by the object format. A symbol check records
dynamic initialization. Startup timings include process creation and warmed operating-system
caches, not cold disk startup. The script accepts `--size`, repetition counts, `--llvm-size`,
and `--llvm-nm` paths. These synthetic commands measure container cost, not XFF parser or
whole-application speedups. Select a layout using these costs together with lookup latency.
