# HAMT allocation-domain ownership

`HamtSourceDomain<Source>` owns a stable, possibly nonmovable block source. Copies
share the source; moves leave an empty handle. Containers must declare their
domain before their root owner so that root destruction precedes source destruction.
Do not use a borrowed source pointer after the last domain handle is destroyed.
Owning a source object does not transfer ownership of external storage or resources
that source itself borrows. For example, a fixed source's supplied byte span must
outlive every clone or snapshot using that span.

`TryCreate` requires nothrow source construction and reports control-block allocation
failure with `std::nullopt`. It uses one global nothrow allocation, independently of
the source's allocation budget. `TryCreateIn` instead places that control block in a
separate caller-provided block source, enabling a fully bounded composition when both
the control and node sources are bounded. The control source must outlive every domain
handle. No ownership metadata is added to individual HAMT nodes.

The atomic count protects handle ownership, not concurrent container mutation or
source operations. Those still require external synchronization. Counter overflow
terminates rather than wrapping and permitting premature destruction.

`HamtOwnedTree<Tree, Source>` pairs the domain with the existing map/set core.
Snapshot copies retain both ownership layers. Moving transfers the root but retains
the domain in the now-empty source container, making that container reusable without
another control-block allocation. Assignment and swap keep roots paired with their
original allocation domains. The declaration order guarantees that nodes are
destroyed before the source, including when the original container has disappeared.
`domain()` exposes a const domain handle for node-payload allocation. Copying that
handle retains the same source independently of the tree's lifetime; it does not
transfer ownership of resources the source borrows. Node payloads must retain this
paired domain rather than creating an unrelated source with a different lifetime.

`try_clone_to<OtherSource>(source_constructor_args...)` copies into a fresh owned
domain, preserving the core's hash/equality state. Destination-domain or node
allocation failure returns an empty optional and destroys partial destination
ownership without changing the original. The rvalue overload consumes only after
successful cloning, leaving the original empty and reusable. Empty trees still
need the destination control-block allocation, but allocate no destination nodes.

The public flat and node map/set interfaces use this domain, including bounded
control-source construction and independent cross-source cloning. Performance
comparisons remain outstanding; this internal representation is not therefore
declared best for every storage configuration.
