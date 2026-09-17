# Persistent flat HAMT map

`HamtFlatMap<Key, Mapped, Hash, Equal, Options, Source>` uses the same packed-node,
allocation-domain, collision, and path-copying core as `HamtFlatSet`. Its stored
`value_type` is `std::pair<const Key, Mapped>`; persistent iterators are const views.
Keys cannot be edited through the mapped-value update API.

Persistent insertion and erasure return `(new_map, changed)` without modifying the
original. Duplicates preserve the existing mapped value. The move-only transient
offers STL-shaped iterator/bool insertion and count erasure. Converting with
`std::move(edit).persistent()` leaves `edit` empty and reusable.

`try_update(key, editor)` accepts only a const-invocable, nothrow editor returning
exactly `void`, and passes it a `Mapped&`, never a key. A persistent update returns
a variant containing `(new_map, changed)` or `HamtError`. A transient update returns
a variant containing `changed` or `HamtError`; a fully unique path is edited without
allocation, while shared paths are copied. Missing keys do not invoke the editor.
On failed path copying, container values remain unchanged, but external editor side
effects are not rolled back. Editors must not reenter the container.
Isolation applies to stored mapped objects: copying a pointer or shared handle does
not deep-copy its pointee. Mutating externally shared resources remains the user's
responsibility, just as for copies of ordinary standard containers.

`at(key)` provides a const mapped reference and terminates for a missing key.
Ordinary insertion/erasure terminate on bounded failure; `try_` operations report
allocation exhaustion separately from maximum size. Neither throwing user callbacks
nor throwing entry copy/move construction are supported. `try_create` separately
reports allocation-domain creation failure with `std::nullopt`.

Transient nonconst `at` detaches a shared path before returning a mutable mapped
reference. `try_at` returns a variant containing a mapped pointer (null for a missing
key) or an allocation error. `operator[]` value-initializes missing mapped values and
requires nothrow default construction; `try_get_or_insert` is its recoverable form.
Const access never detaches. Mutable references are invalidated by structural
mutation, assignment, move, and consuming conversion to persistence. Do not retain
such a reference and mutate through it after publishing a persistent snapshot.
Uniqueness checks use acquire loads to observe prior ownership releases. This does
not make concurrent retain or mutation safe; the existing external-synchronization
contract still applies.

`try_clone_to<OtherSource>(source_constructor_args...)` returns an optional map of
the destination-source specialization, preserving keys, mapped values, and callable
state in independently owned nodes. The rvalue overload consumes only on success;
failure preserves the original. The new domain owns its source object, not any
external storage or resource that source borrows. Mapped pointer/handle copies
retain their usual shallow-copy semantics.
The transient's rvalue clone overload returns a persistent destination map and
likewise consumes only on success.

Transient nonconst `begin` and `find` return mutable mapped iterators with immutable
keys. Their recoverable forms, `try_begin` and `try_find`, report allocation errors;
missing lookup returns end without allocation. Const access and `cbegin` remain
read-only and never detach. Before exposing mutable iteration, all shared nodes
are detached, preserving multipass iteration and unchanged snapshots. Preparation
may invalidate earlier references and iterators even without inserting or erasing.
Insertion prepares ownership before committing so reported failure never leaves
a new key inserted; successful preparation may invalidate references even if the
subsequent insertion fails. Mutable iterators convert to const iterators.

Node storage variants and unique in-place structural mutation are still outstanding.
Result representations and performance decisions remain provisional until the
complete implementation is benchmarked. This is C++20-compatible work; no C++26
language or library features are required.
