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

## Shared nodes

`HamtSharedNode<FragmentBits, Entry>::TryCreate` creates a packed node with one owned reference.
Entry and child counts must match the bitmap index; occupied child slots require non-null nodes.
Each child acquires an additional reference only after allocation succeeds. Failure returns
`std::nullopt`, without consuming borrowed inputs or changing child reference counts.

`Retain` acquires one additional reference; every owned reference must be balanced by one
`Release`. Null retain/release operations are harmless. The final release destroys entries and
releases each owned child reference recursively, returning the original allocation metadata.
The source must outlive every node and must be the same source used for the node and descendants.
Nodes must be created through `TryCreate`, not as standalone stack objects.

Entry copies and destruction must be non-throwing. Unexpected source exceptions terminate under
the allocation method's `noexcept` contract. Atomic reference counting protects reference updates,
not mutation of entries, topology, publication, or source access. Callers must already hold a live
reference before retaining; using a released node or releasing an unowned reference is invalid.
The reference count is 32-bit; callers must not retain when the count is already its maximum.
Higher-level persistent operations retain immutable nodes; direct mutable spans are internal
construction machinery and must not modify nodes shared by snapshots.

## Hash paths

`HamtHashPath<Hash, FragmentBits>` consumes an unsigned hash from its least-significant fragment
upward. Fragment widths 4–7 are supported for later benchmark comparison, not a performance
recommendation. A final partial fragment has zero bits beyond the hash width. `Fragment(level)`
requires `level < kLevels`.

`FindHamtMergePath` identifies a shared suffix of path levels and the first divergent fragments,
or reports a full-hash collision. `common_levels` counts levels from the supplied `start_level`,
not from the root. The caller must have established equal fragments before `start_level`, and
`start_level <= kLevels`; an exhausted path is valid. Both helpers are constexpr and allocate
no storage. Full-hash collisions still require key equality; shared fragments never establish
key equality by themselves.

## Full-hash collision nodes

`HamtSharedNode::TryCreateCollision` creates a non-empty dense entry array with no indexed
children. Collision entry counts are independent of the bitmap slot count. The caller must
establish that every entry has the same complete hash; this storage layer neither extracts
hashes nor deduplicates keys. The collision flag distinguishes this representation even for
a singleton bucket. Ordinary nodes still require matching bitmap counts and non-null children.

Normal and collision construction share allocation validation, object construction, and exact
source-metadata reclamation. Empty collision input, invalid layout, exhaustion, or unusable
source storage returns `std::nullopt`. Owned references and lifetime rules are unchanged.

## Borrowed lookup

`FindHamtEntry` follows one bitmap hash path, scanning only terminal full-hash collision buckets.
It returns a borrowed `const Entry*`, or null for an empty tree or absent key. Complete hashes
are compared before key extraction/equality. Hash extraction, key extraction, and equality
use const callable references without copying state; their invoked operations must be non-throwing.
Heterogeneous keys are accepted when the equality callable supports the corresponding types.
The tree must remain valid and alive throughout lookup and subsequent use of the returned pointer.
Lookup acquires no ownership and performs no allocation or synchronization.

## Persistent entry insertion

`HamtSharedNode::TryInsertEntry` copies a normal node while inserting one dense entry and
retaining its existing children. The caller supplies the new bitmap and insertion rank, preserving
all old slots and adding exactly one data slot. This primitive does not perform key deduplication
or hash routing. Collision nodes use separate operations.

The entry argument may reference an original entry: the old node stays untouched, and a successful
result owns an independent packed entry array with one node reference. Invalid counts/rank or
allocation failure returns `std::nullopt`, preserving the original and its child reference counts.
The caller must balance the returned reference and keep borrowed inputs alive through the call.
Non-throwing copies/destruction are required; no exception rollback mechanism is introduced.
