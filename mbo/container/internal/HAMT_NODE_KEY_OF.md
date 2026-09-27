# Node key projection

`HamtNodeKeyOf<KeyOf>` applies an existing map/set key extractor to a payload
handle's stored value. It preserves the extractor's result type and borrowed
reference identity: map keys remain const, and no mapped value or key is copied.
The same projection works with separately owned node payloads and compatible
borrowed handles, without a second hash-routing or lookup implementation.

Stored handles must be nonempty and alive. Returned references borrow their
payload's lifetime. Handle access and the actual extractor invocation must be
nonthrowing; throwing extractors are rejected rather than silently terminated.
Extraction is constexpr when both handle and extractor support constant evaluation.

The extractor is stored by value, retaining configured state. `HamtNodeMap` and
`HamtNodeSet` use this projection so the flat and node layouts share routing and
lookup code. Benchmark comparisons remain outstanding.
