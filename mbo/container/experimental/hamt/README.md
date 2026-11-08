# Experimental HAMT containers

The HAMT implementation lives in `mbo/container/experimental/hamt` and namespace
`mbo::container::experimental::hamt`. Headers, target names, types, and behavior in this
experimental package may change between releases without compatibility aliases or deprecation.
Internal machinery lives in its `hamt_internal` namespace and is not a supported interface.

HAMTs share unchanged hash-trie nodes between persistent snapshots. Use them when structural
sharing or explicitly provisioned storage matters. This package does not claim a general lookup
advantage over ordinary hash tables.

| Header                     | Public type           | Storage and access                                        |
| -------------------------- | --------------------- | --------------------------------------------------------- |
| `hamt_flat_map.h`          | `HamtFlatMap`         | Packed key/value entries; persistent and transient maps   |
| `hamt_flat_set.h`          | `HamtFlatSet`         | Packed keys; persistent and transient sets                |
| `hamt_node_map.h`          | `HamtNodeMap`         | Separately owned payloads for stable surviving addresses  |
| `hamt_node_set.h`          | `HamtNodeSet`         | Separately owned keys for stable surviving addresses      |
| `hamt_options.h`           | `HamtOptions`         | Fragment width, maximum size, and full-hash collision cap |
| `hamt_string_index.h`      | `HamtStringIndex`     | Transient flat-map index for the generic StringInterner   |
| `hamt_node_string_index.h` | `HamtNodeStringIndex` | Transient node-map index for the generic StringInterner   |

Bazel targets use `//mbo/container/experimental/hamt:<header_basename>_cc`.
For example, depend on `//mbo/container/experimental/hamt:hamt_flat_map_cc` and include
`mbo/container/experimental/hamt/hamt_flat_map.h`.

Persistent insertion and erasure return a new container and a change flag. Existing snapshots
remain unchanged. Each container has a move-only `transient_type` for editing and a consuming
`persistent()` conversion. Shared paths are copied before mutation; unique paths can be edited
in place. See the [flat map](HAMT_FLAT_MAP.md),
[flat set](HAMT_FLAT_SET.md), [node map](HAMT_NODE_MAP.md), and
[node set](HAMT_NODE_SET.md) contracts for reference and iterator invalidation.

`HamtOptions` accepts fragment widths from four through seven bits, positive maximum size, and
positive maximum full-hash collision size. The latter two default to unrestricted size bounds.
Routing depth is bounded by hash width; collision scans are bounded only when a finite collision
cap is configured. Hashing and equality still depend on the key's representation and length.

Supported hash, equality, construction, and mutation callbacks must satisfy the operation's
nothrow constraints. Recoverable `try_*` operations report allocation, size, or collision
exhaustion without publishing a partial update. Convenience operations fail hard when their
requirements cannot be met. See [storage and ownership](HAMT_STORAGE.md) for the
allocation-domain and lifetime rules.

Node storage uses `BlockSource`. Factories accepting caller-owned control storage avoid allocating
the ownership domain separately. A source must support the simultaneously live blocks required
by a trie and its snapshots; one fixed single-live-block source is not a general HAMT allocator.
`ArenaBlockSource` supports recyclable blocks from a caller-owned arena. Borrowed backing storage
must outlive every snapshot using it. External synchronization is required for copying and
mutating shared ownership; atomic node reference counts do not provide a publication protocol.

The standalone [flat collision bucket](HAMT_COLLISIONS.md) uses the released
`SegmentedVector` for append, indexed access, and back removal. No legacy segmented-container
implementation is carried by this package.

The [benchmark guide](HAMT_MAP_BENCHMARKS.md) describes the current harness and the review needed
before publishing new performance results. Previous charts and measurements were removed because
they predate this integrated implementation; their commits remain available in Git history.
String ownership and cascading interning use the existing
[generic StringInterner](../../../strings/STRING_INTERNING.md). The HAMT string-index adapters
hold transient flat or node maps and expose const container iterators. Pair them with
`HamtStringInternerIndexPolicy` to preserve recoverable index exhaustion through the generic
interner's policy interface. Character storage uses the existing append-only `ArenaStringStorage`;
Arena checkpoints are not required.

## Generic interner composition

Use an ID-bearing map index: a key-only set would force the generic interner to scan its dense
entries to recover an ID. `StringIdU32` keeps the index payload compact, while `SegmentedVector`
preserves descriptor and mapped-value addresses. The ordinary exception-free publication policy
avoids a rollback guard for mapped values that cannot throw. Select the generic exception-safe
policy when a mapped-value constructor can throw.

```cpp
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_interner_policy.h"
#include "mbo/hash/hash.h"
#include "mbo/strings/string_interner.h"

using Id = mbo::strings::StringIdU32<>;
using Hash = mbo::hash::DefaultHasher;
using Index = mbo::container::experimental::hamt::HamtStringIndex<Id, Hash>;
using Policy = mbo::strings::StringInternerPolicyHolder<
    mbo::strings::StringInternerErrorStlPolicy,
    mbo::strings::StringInternerLookupMboPolicy,
    mbo::container::experimental::hamt::HamtStringInternerIndexPolicy>;
using Interner = mbo::strings::StringInternerSet<
    Id, Hash, Index, mbo::strings::ArenaStringStorage<>,
    mbo::container::SegmentedVector<std::string_view>, Policy>;

Interner strings;
auto inserted = strings.insert("example");
```

`HamtStringIndex` and `HamtNodeStringIndex` are move-only containers with const iterators,
`find`, `emplace`, `erase(iterator)`, and fallible `try_emplace`. An interner has one evolving
local index, so insertion uses transient ownership rather than copying a persistent root first.
Parent visibility continues to use the generic interner's captured ID prefix. Keys borrow the
interner's character storage; insertion results never expose mutable keys or IDs.

The flat adapter avoids separate payload allocations and is the initial compact composition.
The node adapter is available for comparison. Fragment widths, hash algorithms, character-block
sizes, and expected populations remain measurement axes rather than universal speed defaults.
The [generic interner benchmark](HAMT_MAP_BENCHMARKS.md#generic-stringinterner-compositions)
compares these choices using the same interner implementation.

Failed index insertion leaves no published ID or lookup entry. As in the generic interner on
`main`, an uncommitted character copy can remain reserved until storage destruction. Checkpoint
rollback is not part of this composition. With exception-safe mapped publication, `erase(iterator)`
removes the unpublished index entry if the caller's value constructor throws.
