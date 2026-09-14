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

## Persistent child insertion

`TryInsertChild` copies a normal node, preserving its entries and adding one child at the
supplied dense rank. The caller preserves all old slots and adds exactly one node slot in the
result bitmap. The child must be non-null and alive throughout the call; it may already be
referenced by the original. Successful construction retains every resulting child reference.

Invalid counts/rank, a null child, or allocation failure returns `std::nullopt` without changing
the original or retaining the proposed child. The result owns one node reference. Releasing
the original cannot invalidate children retained by the result; the source must outlive both.

## Persistent child replacement

`TryReplaceChild` copies a normal node while replacing one dense child position. Its bitmap
and entries remain unchanged. The replacement must be non-null and alive throughout the call;
replacing a child with itself is valid. The result retains all of its children, including the
replacement, and owns one node reference. The original remains untouched.

A collision node, invalid position, null replacement, or allocation failure returns
`std::nullopt` without changing child reference counts. Releasing the old node does not
invalidate the replacement snapshot. Non-throwing entry copies and destruction remain required.

## Persistent entry and child erasure

`TryEraseEntry` and `TryEraseChild` copy a normal node, omitting the entry or child at the
supplied dense position. The caller supplies the resulting bitmap, preserving all other slots
and removing exactly the selected data or node slot. These storage primitives validate payload
counts and positions; they do not validate the caller's hash routing or normalize the full tree.
Collision nodes require separate mutation operations.

Entry erasure retains every original child in the new node. Child erasure retains only the
remaining children; the removed child remains owned by the original node. Neither operation
changes the original entries, topology, or reference counts on failure. Success returns one
owned reference, including when removal produces an empty normal node.

Collision input, an empty relevant payload, inconsistent counts, an invalid position, or
allocation failure returns `std::nullopt`. Inputs remain borrowed throughout the operation.
The source must outlive all resulting nodes; entry copies and destruction must be non-throwing.
These primitives do not mutate existing snapshots and require external synchronization under
the shared-node contract above.

## Persistent collision mutation

`TryInsertCollisionEntry` copies a collision node while inserting an entry at a dense position
in `[0, size]`. The caller establishes that its complete hash matches the bucket and its key
is absent. The entry may alias an original entry; copying does not modify the original node.

`TryEraseCollisionEntry` copies a collision bucket while removing one dense position. A two-entry
bucket may become a singleton collision node. Removing the final entry is a tree-level operation:
this primitive rejects it instead of allocating an empty collision bucket.

Both methods return one owned reference on success. Normal-node input, an invalid position,
singleton erasure, or allocation failure returns `std::nullopt`. Original contents and ownership
remain unchanged. Collision operations reuse the normal-node entry-copy helpers and preserve
the same non-throwing copy, source lifetime, and external synchronization requirements.

## Persistent entry replacement

`TryReplaceEntry` copies a normal or collision node while replacing one dense entry. The
caller must preserve the entry's routing hash and container key uniqueness: this primitive
does not reindex a changed key. Map operations normally use it to replace the mapped value
while keeping the key unchanged.

Successful replacement preserves the bitmap, collision representation, and child topology,
retains the resulting children, and returns one owned node reference. Replacement input may
alias an original entry. Releasing the original does not invalidate the replacement snapshot.
An invalid position or allocation failure returns `std::nullopt` without changing the original
or child references. Non-throwing entry copying and the shared-node lifetime contract apply.

## Branch construction

`TryBuildHamtBranch` joins two entries beneath a supplied hash-path level. The caller establishes
equal preceding fragments and distinct keys; full-hash equality alone does not establish key
equality. Different hashes produce a dense two-entry divergence node ordered by fragment, with
one parent node for every common level. Equal complete hashes produce a terminal collision node.

The starting level may equal the exhausted path boundary only for equal complete hashes. An
invalid level or allocation failure returns `std::nullopt`. Partial branches are released before
failure returns, so every successful temporary allocation is reclaimed. Success returns one
owned node reference. Entries are borrowed and copied without throwing; the source must outlive
the returned branch and satisfy the shared-node ownership contract.

## Persistent tree insertion

`TryInsertHamtEntry` inserts into a borrowed root and returns an owned root plus an `inserted`
flag. A null input root is an empty tree. A duplicate key returns a retained reference to the
unchanged root with `inserted == false`; callers must release that reference too.

New keys copy only the affected path, promote occupied data slots into branches when needed,
and extend equal-full-hash collision buckets. A different hash splits a collision bucket
into a branch rather than mixing hashes inside the bucket. Unaffected descendants are shared.
Allocation failure returns `std::nullopt`, reclaiming temporary nodes without changing the
original snapshots. The supplied hash and key must describe the supplied entry, and equivalent
keys must have equal hashes. Hash/key extraction, equality, and entry copies must be non-throwing.
These are internal primitives; public map/set variants establish the container-level contracts.

## Persistent tree erasure

`TryEraseHamtEntry` removes a key from a borrowed root and returns an owned root plus an `erased`
flag. A successful removal of the final entry returns a null root with `erased == true`, not
an allocation failure. Missing keys return a retained reference to the unchanged root with
`erased == false`; null input is already empty.

Erasure copies the affected path, removes empty children, and propagates singleton entries
back into data slots. Two-entry collision buckets become ordinary singleton entries; larger
buckets remain collision nodes. Existing snapshots remain unchanged. `std::nullopt` reports
allocation failure, with temporary nodes reclaimed and original ownership preserved.

Singleton propagation requires both non-throwing entry copying and moving. Hash/key extraction
and equality must also be non-throwing. Equivalent keys require equal hashes. The source and
all borrowed inputs follow the shared-node lifetime and external synchronization contracts.

## Borrowed forward iteration

`HamtIterator` is a multipass forward iterator exposing only `const Entry&` and `const Entry*`.
It visits dense entries before descendants, including every entry in collision buckets. This
is structural traversal order, not insertion order or key order. Its fixed frame stack covers
the complete hash width for fragment sizes 4 through 7 without traversal allocations.

Iterators borrow the entire snapshot: keep its root and block source alive throughout iteration.
Copying an iterator does not retain its root. Changing another snapshot does not change the
borrowed tree, but releasing the last owner invalidates its iterators and entry references.
Synchronization remains external.

Active iterator equality includes root identity, so two snapshots sharing an entry do not
accidentally compare equal. Exhausted, null-root, empty-root, and default-constructed iterators
all represent the common end value. Dereference and increment require a non-end iterator.

`HamtIterator::At(root, hash, target)` positions an iterator at the exact borrowed entry address
using the hash path, rather than scanning the complete range. Only a terminal collision bucket
requires a linear scan. Advancing this iterator visits precisely the same suffix as ordinary
iteration from that entry. A missing route, null input, or address mismatch returns end.
The caller supplies the target's correct hash; `At` performs neither key lookup nor ownership
acquisition. It has the same snapshot lifetime and external synchronization requirements.

## Explicit allocation-domain cloning

`TryCloneHamtTree(destination, original)` copies entries, collision buckets, routing bitmaps,
and descendants into the destination block source. No original node is retained. The returned
root owns one reference and must be released exclusively through the destination source, which
must outlive it. The original tree may be released independently after cloning succeeds.

Null input returns an engaged optional containing a null root without allocating. Exhaustion
returns `std::nullopt` after releasing every partial destination copy; original ownership remains
unchanged. Entry copies and destruction must be non-throwing, and synchronization is external.
Child staging uses bounded stack storage rather than another allocator. Repeated source child
references are copied independently; this primitive does not preserve graph aliasing or perform
cross-domain structural sharing. Public `clone_to(source)` wrappers also preserve hash/equality
state and container-level metadata; those responsibilities are not handled by this node primitive.

## Persistent value replacement

`TryReplaceHamtEntry` finds an existing key and copies only its affected path, replacing the
stored entry without changing the original snapshot. The replacement must preserve the key
and full hash; public map wrappers enforce immutable keys while replacing mapped values.
Collision siblings and unaffected branches remain unchanged.

Success returns an owned root and `replaced == true`. A missing key returns a retained original
root and `replaced == false`, without allocating; a null root is a successful missing-key result.
`std::nullopt` means allocation failure, with temporary nodes reclaimed and original ownership
unchanged. Release successful roots through their original block source. Copies, destruction,
hash/key extraction, and equality must be non-throwing; access remains externally synchronized.

## Snapshot root ownership

`HamtRootOwner` owns one root reference and borrows the block source responsible for that
entire tree. The source must outlive every owner and copy. Construction and `reset` adopt an
already-owned reference; passing a borrowed `get()` result without retaining it is invalid.
Copies retain the root without copying entries or allocating. Moves transfer the reference and
leave a valid empty owner that still knows its source. Copy/move assignment and swapping carry
the root and its source together, releasing previous roots through their original source.

Destruction and `reset()` release ownership. `release()` transfers the owned reference to the
caller without destroying it; the caller must subsequently release it through the matching
source. `get()` borrows, never retains. The helper neither owns the source nor adds source
metadata to individual nodes; public allocation-domain wrappers must supply source lifetime.
All ownership operations are non-throwing and require external synchronization for shared owner
objects. This internal helper does not grant mutable access to a public persistent container.

## Shared container core

`HamtTree` combines root ownership with constant-time visible size, compile-time maximum size,
stateful hash/key-extraction/equality objects, heterogeneous lookup, and borrowed const iteration.
Map-shaped entries and set-shaped entries use the same implementation through key extraction.
Copies retain snapshots without allocating. Moves leave valid empty trees with the same callable
state; swaps and assignments preserve the pairing of roots, callables, and allocation sources.
The core supplies its container address as iterator range identity, distinguishing even containers
that share an entire root. This reuses the iterator's existing identity slot without increasing its
size. Standalone primitive iterators retain root identity when no container identity is supplied.

`try_insert`, `try_replace`, and `try_erase` return a mutation flag plus an optional `HamtError`.
Duplicate insertion is successful even at maximum size and does not replace an existing value.
Maximum-size and allocation exhaustion leave the value unchanged. Missing replacement/erasure
is successful without mutation. The initial core uses path copying for every structural update;
uniquely owned transient editing is a separate optimization, not provided by this primitive.

`try_clone_to` deep-copies into another source while preserving size and callable state. Empty
clones succeed without allocation. The source is borrowed; public wrappers provide allocation-domain
lifetime and immutable-key enforcement. Entries require non-throwing copying, movement, and
destruction. Callable invocation is constrained for the actual lookup type; callable copies,
moves, swaps, and destruction are non-throwing. These internal requirements are not a claim that
all public storage variants have identical element constraints or invalidation guarantees.
