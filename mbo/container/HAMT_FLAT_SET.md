# Persistent flat HAMT set

`HamtFlatSet<Key, Hash, Equal, Options, Source>` stores immutable keys directly in
packed nodes. Snapshot copies retain the root and allocation domain, not every key.
The core currently requires nothrow key copy/move construction and nothrow const
hash/equality invocation. Throwing user operations are not caught or silently adapted.

Persistent `insert` and `erase` return `(new_set, changed)`, leaving the original
untouched. Their `try_` counterparts return a variant containing that pair or
`HamtError`; maximum-size and allocation exhaustion are distinct errors. Ordinary
operations terminate on those failures. `try_create` returns an optional set and
accepts constructor arguments for the owned source; ordinary construction requires
a nothrow default-constructible source and terminates if domain allocation fails.

```cpp
mbo::container::HamtFlatSet<int> empty;
auto [one, inserted] = empty.insert(1);
auto edit = one.transient();
auto [position, added] = edit.insert(2);
auto two = std::move(edit).persistent();
// empty is empty; one contains 1; two contains 1 and 2.
// edit is now empty and remains reusable.
```

The nested `transient_type` is move-only. Insertion returns `(iterator, inserted)`;
erasure returns a count. Its `try_` operations return those values or `HamtError`.
Only a consuming rvalue `persistent()` conversion is offered. All iterator values
are const keys. Iteration is structural rather than insertion order; active iterator
equality includes container identity even when snapshots share their entire root.
Mutation invalidates the mutated container's iterators; unchanged snapshots retain
their values and iterators. External synchronization is required.

The ordinary owned-domain factory adds one nothrow global allocation even for an inline block
source. `TryCreateIn(control_source, hash, equal, source_args...)` instead constructs that domain in
caller-provided block storage. The control source and every resource borrowed by the node source
must outlive the set and all snapshots sharing its domain. This makes a fully caller-provisioned
configuration possible when both sources report recoverable exhaustion; control-domain and node
budgets remain independent.
`try_clone_to<OtherSource>(source_constructor_args...)` returns an optional set of
the destination-source specialization, owning independently copied nodes and its
new source domain. It preserves hash/equality state. The const-lvalue overload
preserves the original; the rvalue overload consumes only on success and leaves
the original empty and reusable. Domain or node allocation failure preserves the
original and returns an empty optional. External storage borrowed by a source still
requires its own lifetime guarantee.
The transient's rvalue clone overload returns a persistent destination set and
also preserves the transient on failure.

Transient structural operations reuse uniquely owned nodes where the representation permits and
fall back to persistent path copying for shared paths. Separate node set/map variants provide
stable payload addresses, while flat variants keep payloads inside packed topology blocks. An
`ArenaBlockSource` can supply multiple simultaneously live node blocks from a caller-owned arena
and recycle blocks released during failed mutations, so repeated bounded failure does not silently
consume the provisioned budget. The container still requires external synchronization; immutable
snapshots and atomic node reference counts do not by themselves publish roots or reclaim snapshots
for concurrent readers. Benchmarks and final performance-backed defaults remain outstanding.
