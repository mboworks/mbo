# HAMT node map

`HamtNodeMap<Key, Mapped, Hash, Equal, Options, Source>` stores immutable keys
and editable mapped values in separately allocated payloads. Persistent snapshots
share topology and payload ownership. Structural edits preserve addresses of
surviving payloads; mapped edits detach shared payloads before exposing mutation.
The existing HAMT traversal and mutation algorithms remain the single routing
implementation.

Persistent rvalue insertion accepts nothrow-movable entries, including move-only
mapped values. Duplicate detection happens before moving the entry, so a duplicate
does not consume its argument. Once payload construction succeeds, a later topology
allocation failure can leave the input moved-from while preserving all container
values. Snapshot sharing and erasure do not copy the mapped value. Editing a shared
payload and independent cloning require nothrow entry copying; move-only ownership
does not imply copy-on-write editing support.

Persistent iteration is read-only. Transient `at` detaches only the edited path
and payload. Mutable iteration and mutable `find` prepare unique ownership of the
whole tree and all payloads before exposing a forward iterator. Iterator copies
then traverse independently without lazy allocation during dereference. Keys
remain const. Mutable iterators convert to const iterators, not conversely.

Successful whole-tree payload preparation is remembered by the move-only transient.
Repeated mutable lookup, iteration, and insertion do not repeat that payload walk.
Failed partial preparation never establishes the proof. Moves and swaps keep the
proof with its map; consuming publication leaves an empty reusable transient.
A new transient created from a shared snapshot must establish its own proof.
Structural mutation introduces only newly owned payload records, preserving a
successful proof. The underlying tree separately caches node-ownership uniqueness;
node uniqueness alone cannot prove payload uniqueness.

Preparation may change topology or payload addresses even when a later allocation
fails; logical values and existing snapshots remain unchanged. Do not retain
editable references across publishing snapshots or subsequent ownership preparation.
External synchronization is required for all operations.

`try_clone_to<OtherSource>` reconstructs both topology and payloads in the destination
domain, rather than accidentally retaining payloads allocated by the original source.
Failure destroys the partial destination; consuming clones empty the source only
after success. Consuming `persistent` leaves a valid empty transient.

This implementation is still under validation. First mutable traversal of an
unprepared transient can perform whole-tree preparation; subsequent preparation
uses the retained proof. Performance decisions still require the complete benchmarks.
The copying entry interfaces require nothrow key and mapped-value copies. Tests cover
full-hash collisions, duplicate preservation, erasure, address stability, bounded
insertion, size limits, consuming-clone failure, heterogeneous lookup, source lifetime,
and all fragment widths from four to seven bits. Intermediate payload and topology
allocation failures preserve snapshots and release temporary ownership. Full-width
shared-prefix persistent mapped updates, new-key insertions, and chain-collapsing
erasures sweep every allocation boundary through success for fragment widths 4–7,
checking temporary release balance, snapshot values, and unchanged sibling payload
addresses. Mutable whole-tree preparation tests cover partial failure, retry, swap,
move assignment, snapshot preservation, and proof-cache invalidation. These focused
sweeps do not claim to enumerate every possible user topology. Source-domain control
allocation remains separate from the supplied block-source budget. Benchmark
comparisons follow the complete implementation stack; no fastest-container claim is
made here.
