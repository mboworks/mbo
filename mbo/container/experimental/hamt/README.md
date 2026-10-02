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
| `hamt_string_index.h`      | `HamtStringIndex`     | Flat-map adapter for experimental string interning        |
| `hamt_node_string_index.h` | `HamtNodeStringIndex` | Node-map adapter for experimental string interning        |

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
String ownership and cascading interning are documented in
[the experimental interner guide](../../../strings/experimental/README.md).
