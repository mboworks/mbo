# HAMT collision storage

These internal helpers handle terminal collision buckets for the general HAMT map/set family.
They are not specialized to strings. A bucket compares the stored full hash before applying key
equality; a hash-path fragment alone is not sufficient to establish equality. Equal keys must
produce equal full hashes.

## Flat collision buckets

`container_internal::HamtFlatCollisionBucket` accepts value, key-extractor, equality, options,
full-hash, entry, and storage types. Its default storage is `SegmentedSequence`.

- `find(hash, key)` supports heterogeneous keys and returns an iterator or `end()`.
- `try_insert(hash, value)` returns an entry pointer, an inserted flag, and an optional `HamtError`.
  Duplicates succeed without insertion, including when the bucket is full.
- Exceeding `HamtOptions::maximum_size` reports `kMaxSizeExceeded`; exhausted storage reports
  `kAllocationExhausted`. Neither failure adds an entry.
- `erase(hash, key)` replaces the erased slot with the final entry and pops that final entry.
  References to the erased entry and the former final entry are invalidated. Other entries are
  untouched; iteration order is not insertion order after erasure.
- Default segmented storage preserves existing entry addresses during append. Custom storage
  determines its own append stability and must support the operations used by the helper.

Key extraction and equality are invoked through const references, without copying callable state.
Their invoked operations must be non-throwing. This is a compile-time contract, not an exception
recovery mechanism: do not label a callable `noexcept` unless it really cannot throw. Moving a
stored value during insertion, and moving an entry during erasure, must also be non-throwing.
Allocation exhaustion is a separate recoverable outcome, not a thrown exception.

Buckets require external synchronization. The helper performs no atomic publication or reclamation.
