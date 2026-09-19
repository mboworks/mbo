# Node-value iterator adapter

`HamtNodeIterator<Iterator, Mutable>` projects a forward iterator over payload
handles into a forward iterator over their values. Const dereference uses the
handle's `get` pointer; mutable dereference uses nonallocating unique access after
the owning transient has prepared all node and payload ownership. The underlying
HAMT iterator retains responsibility for traversal and range identity. No extra
traversal, allocation, or ownership is added during iteration.

Copies traverse independently. End comparison delegates to the underlying iterator.
Value-initialized adapters initialize their underlying iterator rather than leaving
raw pointer iterator state uninitialized. Dereferencing end or an empty payload is
invalid. The underlying range and payloads must remain alive throughout use.

The adapter requires nonthrowing iterator construction, copying, movement,
destruction, increment, and equality. Mutable adapters convert to const adapters,
never conversely. `HamtNodeMap::transient_type` performs the required ownership
preparation before returning mutable iterators; dereference cannot fail or detach
lazily. This helper does not change pointer-invalidation contracts.
