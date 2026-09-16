# Persistent flat HAMT map

`HamtFlatMap<Key, Mapped, Hash, Equal, Options, Source>` uses the same packed-node,
allocation-domain, collision, and path-copying core as `HamtFlatSet`. Its stored
`value_type` is `std::pair<const Key, Mapped>`; public iterators are const views.
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
nor throwing entry copy/move construction are supported. `TryCreate` separately
reports allocation-domain creation failure with `std::nullopt`.

Mutable mapped iterators, `operator[]`, consuming allocation-domain clones, node
storage variants, and unique in-place structural mutation are still outstanding.
Result representations and performance decisions remain provisional until the
complete implementation is benchmarked. This is C++20-compatible work; no C++26
language or library features are required.
