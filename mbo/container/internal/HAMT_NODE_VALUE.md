# HAMT node payload ownership

`HamtNodeValue<Value, Source>` owns a separately allocated, shared payload. Copying
the handle preserves the value's address rather than copying the stored value.
The allocation control block retains its source domain, so a payload can safely
outlive the original container and its domain handle. Externally borrowed storage
must still outlive that domain. Moves leave valid empty handles.
Payload construction occurs in place: a value need not be copyable or movable
for handles to share it. Mutable copy-on-write access is available only when the
value itself supports nothrow copying; handle copying does not grant that ability.

`TryCreate` requires nothrow value construction and returns `std::nullopt` for an
empty domain, exhaustion, or invalid returned storage. Rejected storage is returned
to its source with unchanged allocation metadata. The last owner destroys the value
and returns its allocation before releasing the source domain. Unexpected source
exceptions terminate through the `noexcept` interface; they are not caught.

`get` exposes a const value pointer. `try_get_mutable` requires nothrow copying and
detaches a shared payload before returning a mutable pointer. Allocation failure
returns an empty optional without changing ownership or values. An empty handle
successfully returns a null pointer; unique access neither allocates nor relocates.
Node-map mutable access must perform this payload-level copy-on-write independently
of packed-node ownership; tree uniqueness alone does not establish payload uniqueness.
`get_unique_mutable` provides nonallocating mutable access to an already unique
payload, returning null for shared or empty handles. It does not copy or detach.
Mutable traversal must prepare both tree and payload ownership before using it;
publishing a new snapshot invalidates permission to edit a previously returned pointer.
`HamtNodeIterator<Traversal, true>` projects this access into mutable references.
Its caller must prepare unique ownership of every traversed payload before exposing
the range; iterator dereference never performs lazy detachment. Default node
iteration continues to expose const references even over a mutable traversal.
Do not mutate an earlier reference after publishing a snapshot.
`is_unique` observes ownership releases
with an acquire load but does not permit concurrent mutation or unsynchronized retain.
All container and source operations require external synchronization.

Each allocation has its own reference count and domain handle. Their memory and
execution cost must be measured after the complete implementation; this is not yet
a benchmark-selected layout. Public node map/set integration remains outstanding.
