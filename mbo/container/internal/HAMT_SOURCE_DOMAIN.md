# HAMT allocation-domain ownership

`HamtSourceDomain<Source>` owns a stable, possibly nonmovable block source. Copies
share the source; moves leave an empty handle. Containers must declare their
domain before their root owner so that root destruction precedes source destruction.
Do not use a borrowed source pointer after the last domain handle is destroyed.

`TryCreate` requires nothrow source construction and reports control-block allocation
failure with `std::nullopt`. It uses one global nothrow allocation, independently of
the source's allocation budget. Consequently this candidate is not an allocation-free
domain for caller-provided fixed storage; that adapter still needs implementation.
No ownership metadata is added to individual HAMT nodes.

The atomic count protects handle ownership, not concurrent container mutation or
source operations. Those still require external synchronization. Counter overflow
terminates rather than wrapping and permitting premature destruction.

Public map/set integration and performance comparisons remain outstanding. This
internal candidate does not establish that shared domain ownership is the best
representation for every storage configuration.
