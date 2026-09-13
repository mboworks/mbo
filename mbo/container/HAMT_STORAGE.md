# Packed HAMT storage

These internal building blocks support general HAMT containers; they are not public map/set APIs.

`HamtPackedNodeLayout<Header, Entry, Child>::TryMake` computes aligned offsets and allocation
requirements, rejecting invalid counts and arithmetic overflow before allocation.

`HamtPackedNodeBlock<Header, Entry, Child, Source>` borrows a `BlockSource` that must outlive it.
It owns one block containing a header and dense entry/child arrays. Copy and move are disabled.
`TryInitialize` returns false for an already initialized block, an invalid layout, exhaustion,
or unusable source storage. Rejected allocations are returned to the source unchanged.
`clear()` destroys child objects, entries, and the header, then returns the original allocation
metadata to the source. It is idempotent and also runs during destruction.

Header access requires initialization. Empty blocks expose empty spans; initialized spans and
header references remain valid until `clear()` or destruction. Stored child pointers do not
implicitly acquire ownership of pointed-to nodes.

Copies used during initialization and all stored-object destructors must be non-throwing.
Initialization is `noexcept`: a source must report recoverable exhaustion through its result;
an unexpected exception escaping the source terminates rather than providing rollback.
Declaring an operation `noexcept` does not make a throwing implementation recoverable.
The source and block require external synchronization.
