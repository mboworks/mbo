# Node-value iterator adapter

`HamtNodeIterator<Iterator>` projects a forward iterator over payload handles into
a forward iterator over their values. Dereferencing and arrow access use the
handle's `get` pointer; the underlying HAMT iterator retains responsibility for
traversal and range identity. No extra traversal, allocation, or ownership is added.

Copies traverse independently. End comparison delegates to the underlying iterator.
Value-initialized adapters initialize their underlying iterator rather than leaving
raw pointer iterator state uninitialized. Dereferencing end or an empty payload is
invalid. The underlying range and payloads must remain alive throughout use.

The adapter requires nonthrowing iterator construction, copying, movement,
destruction, increment, and equality. The current payload handle exposes only const
values; mutable node-map iteration requires a separate ownership preparation step
and remains outstanding. This helper does not change pointer-invalidation contracts.
