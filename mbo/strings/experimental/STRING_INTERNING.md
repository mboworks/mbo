# Experimental interner contract

All interner types are in `mbo::strings::experimental`. See the [package guide](README.md) for
headers, targets, and the experimental compatibility policy.

## Identity and ownership

`StringId<Representation>` uses unsigned 8-, 16-, 32-, or 64-bit storage. IDs start at zero;
the maximum representable value is reserved as invalid. An eight-bit interner therefore has
255 valid IDs, numbered zero through 254. IDs identify positions within one captured ancestor
chain, not globally unique strings. Siblings may assign the same ID to different local strings.

Insertion copies string bytes into stable storage and assigns an ID only after the descriptor
and index insertion succeed. Existing strings return their existing IDs without allocating a
second copy. Empty strings are valid and require no character bytes. String views preserve
embedded NUL bytes; equality and hashing use their full lengths.

Returned views and mapped-object addresses remain valid while their owning interner lives.
Interners do not move or copy. Parent pointers borrow; every ancestor must outlive its children.
The API has no erase or reset operation that could invalidate published IDs. External
synchronization is required whenever mutation could overlap lookup or iteration.

## Captured ancestors

A child captures its parent's visible size at construction. It inherits that prefix and starts
assigning local IDs at `first_local_id()`. Later parent or ancestor insertions remain invisible,
even when their IDs overlap the child's local IDs. Ancestor strings cannot be replaced through
the child. `StringInternerMap` also exposes ancestor mapped objects as const references.

`parent()`, `root()`, `first_local_id()`, and `parent_has_grown()` describe this topology.
`parent_has_grown()` checks the direct parent only. `size()` includes the captured prefix;
`local_size()` counts this node's own entries. `empty()` and `max_size()` follow those bounds.
`StringInternerOptions::maximum_parent_depth` can bound ancestor walks. Exceeding it at
construction is a fatal configuration error, not recoverable storage exhaustion.

`find(key)` searches ancestors first; `rfind(key)` searches the local node first. Both honor
captured cutoffs. `intern_parent_first` and `intern_child_first` select those lookup orders
explicitly; `intern` follows `StringInternerOptions::parent_first`, which defaults to true.
`get(id)` returns an optional string view and rejects IDs outside the visible prefix.

Forward, const, and reverse iteration visit visible strings in dense-ID order or its reverse.
Iterators cache their owning ancestor and resolve boundaries when crossing between nodes.
Append preserves existing element iterators and views; an old end iterator is not a new end.
Invalid iterator operations are checked by debug requirements.

## Results and rollback

`intern` and its search-order variants return a variant containing `(id, inserted)` or
`StringInternError`. Errors distinguish ID exhaustion, character storage exhaustion,
descriptor storage exhaustion, and index exhaustion. `try_intern` returns an optional pair;
`try_intern_id` returns an optional ID. These adapters discard the detailed failure reason.

Failed insertion does not publish an ID, descriptor, index entry, or mapped object. Character
storage rewinds to its checkpoint and staged descriptors are popped. Previously published
strings and views remain intact. Reserved capacity may remain for reuse. Duplicate lookup still
succeeds when an insertion budget is exhausted.

`StringInternerMap<Mapped>::try_emplace(key, args...)` adds mapped-value construction to this
transaction and can also report `kMappedStorageExhausted`. Duplicates preserve the existing
mapped value. `key(id)`, `mapped(id)`, and `get(id)` resolve visible entries; invalid IDs return
an empty optional or null pointer. Iteration returns a key view and const mapped reference.
The core and mapped-storage factories allow stateful or immovable bounded backends.

## Backends and allocation

The backend concepts check syntax and nothrow operations; callers must also honor their semantic
contracts:

- `StringInternerStorage`: stable copied views, checkpoints, and rollback of uncommitted bytes.
- `StringInternerEntries`: append exactly one descriptor on success, unchanged contents on failed
  append, indexed lookup, and back removal for rollback.
- `StringInternerIndex`: content lookup and transactional insertion that reports duplicate or
  exhaustion without publishing partial state. Stored key views borrow character storage.

Nothrow factories construct storage, entries, and index directly, preserving noncopyable backend
state. The supplied factory must return the exact backend type. `ArenaStringStorage` accepts an
arena factory and supports immovable caller-owned sources. Its checkpoints are borrowed and must
not be reused after invalidation or used to rewind already published views.

The default descriptor and mapped-value backends use the released `SegmentedVector`. Its element
source and directory allocator are independent. Finite segment capacity does not imply an inline
directory or allocation-free construction. Use `LimitedVector` for fixed inline descriptor/mapped
storage, or configure both vector allocation domains. HAMT factories can use caller-owned control
storage and a recyclable `ArenaBlockSource` for nodes. A complete no-general-allocation guarantee
requires provisioning every layer, including all ancestors.

## Diagnostics

Diagnostics are computed on demand without counters on ordinary lookup or insertion paths.
`trace_find` and `trace_rfind` report the result, number of index queries, and matched ancestor
depth. `local_character_bytes_used` and `local_character_bytes_reserved` return optional counts.
`local_entry_storage_diagnostics` reports descriptor counts, live bytes, and optional backend
reservation details. `local_index_diagnostics` and `visit_local_index_nodes` participate only when
the configured index exposes the matching operations.

`local_storage_diagnostics` combines the local domains. `visit_storage_diagnostics` visits each
captured ancestor and reports visible entries separately from its current local storage.
A late-grown parent's storage can exceed the prefix visible to the child. The map additionally
exposes `local_mapped_storage_diagnostics` and combines mapped counts with interner diagnostics.

Unknown byte counts remain `std::nullopt`; they must not be described as zero. The released
`SegmentedVector` reports segment reservation but does not expose directory reservation through
these optional hooks. Totals do not include allocator bookkeeping or whole-process memory.

## Benchmarking

The [benchmark harness](STRING_INTERNER_BENCHMARKS.md) covers alternate indexes, parent depth,
lookup distributions, ID/hash widths, input shapes, exhaustion, iteration, and mapped values.
Historical reports record their original source commits and toolchains. Rerun affected workloads
before drawing conclusions about the experimental package; do not reinterpret old raw artifacts
as measurements of the new code.
