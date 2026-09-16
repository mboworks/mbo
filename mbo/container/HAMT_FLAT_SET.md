# Persistent flat HAMT set

`HamtFlatSet<Key, Hash, Equal, Options, Source>` stores immutable keys directly in
packed nodes. Snapshot copies retain the root and allocation domain, not every key.
The core currently requires nothrow key copy/move construction and nothrow const
hash/equality invocation. Throwing user operations are not caught or silently adapted.

Persistent `insert` and `erase` return `(new_set, changed)`, leaving the original
untouched. Their `try_` counterparts return a variant containing that pair or
`HamtError`; maximum-size and allocation exhaustion are distinct errors. Ordinary
operations terminate on those failures. `TryCreate` returns an optional set and
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

The owned domain adds one nothrow global allocation even for an inline block source.
`try_clone_to<OtherSource>(source_constructor_args...)` returns an optional set of
the destination-source specialization, owning independently copied nodes and its
new source domain. It preserves hash/equality state. The const-lvalue overload
preserves the original; the rvalue overload consumes only on success and leaves
the original empty and reusable. Domain or node allocation failure preserves the
original and returns an empty optional. External storage borrowed by a source still
requires its own lifetime guarantee.
The transient's rvalue clone overload returns a persistent destination set and
also preserves the transient on failure.

Caller-provided allocation-free domain storage is not implemented yet. Structural
transient operations currently use the persistent path-copying primitives; unique
in-place structural mutation, node storage variants, flat maps, benchmarks, and
the final performance-backed result/API decisions remain outstanding.
