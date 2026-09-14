# Persistent node HAMT set

`HamtNodeSet<Key, Hash, Equal, Options, Source>` shares the general HAMT core with
the flat containers but stores each key in a separately owned payload. Persistent
copies and structural mutation keep unchanged key addresses stable. Iterators
expose `const Key&`, not ownership handles; keys cannot be edited.

Persistent insertion and erasure return `(new_set, changed)`. The move-only transient
provides iterator/bool insertion and count erasure; consuming conversion leaves it
empty and reusable. Duplicate insertion preserves its original payload and does
not allocate. Full-hash collisions still compare keys, independently of hashes.

`try_` mutation reports allocation exhaustion and maximum-size failure. Ordinary
mutation terminates on these failures; it does not throw. Source construction,
called hash/equality operations, and key copies used by const-reference insertion
must be nonthrowing. An unexpected source exception terminates rather than being
caught. Source and container access require external synchronization.

Source-changing cloning reconstructs independent payloads and topology in the new
domain. Successful cloning therefore does not preserve original key addresses.
Consuming clones empty the original only after complete success; failure preserves
its contents. Erasing a key invalidates access through the modified container, while
unchanged snapshots retain their payloads. Iterators are invalidated by structural
mutation even though unchanged key references remain stable.

Persistent and transient rvalue insertion move keys into payload storage and
support nothrow move-only keys. Duplicate checks happen before moving, preserving
duplicate arguments. A later topology-allocation failure preserves container values
but may leave the supplied argument moved from; input objects are not rolled back.
Clone operations require nothrow key copying, even when insertion supports moves.
Comprehensive bounded-source tests remain in progress.
Public node maps and benchmarks follow; this layout is not a performance claim.
