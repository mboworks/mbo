# FrozenMap and FrozenSet design

Implement generic, immutable `mbo::container::experimental::FrozenMap` and `FrozenSet`
in `mbo/container/experimental`, with shared storage/index machinery following the
`LimitedOrdered` / `LimitedMap` / `LimitedSet` organization. This is a standalone MBO
facility; XFF integration and parser changes are outside this change. Names are settled.
Supply public documentation, tests, and benchmarks.

## Types and construction

- Use the familiar `FrozenMap<Key, Value, CapacityOrOptions, Hash, KeyEqual>` and
  `FrozenSet<Key, CapacityOrOptions, Hash, KeyEqual>` shape, with deduction/factory helpers.
- Own fixed inline storage; support constexpr and runtime construction from arrays, iterator
  pairs, initializer lists, and C++23 `from_range`. Remain usable with the repository's C++23
  baseline and in C++26 translation units.
- Support generic literal keys and typed mapped values, including non-default-constructible
  types. Supply deterministic constexpr hashing for integral, enum, and string-view keys;
  accept user-supplied constexpr hash/equality objects for other keys.
- Use MBO's existing fambo hasher with its default seed for string-view keys. Keep
  `FrozenHash<Key>` as the type-based default selector; do not maintain a separate string
  hashing algorithm. Retain the measured former FNV baseline as historical evidence.
- String views are length-aware borrowed keys. Their backing storage must outlive the
  container; static constexpr tables require constant-expression backing storage. Never
  retain an initializer-list or input-array view instead of copying its elements.
- Construction is constexpr, not consteval: the same constructors work at compile time
  and at runtime. A static constexpr table has no dynamic initialization. Copies preserve
  the precomputed index without rebuilding it.
- Reject conflicting duplicate keys during constant evaluation with a clear diagnostic.
  Keep alias spellings as separate keys. Independently constructed tables may assign
  different values to the same key. Document the handling of identical duplicates.

## Frozen associative interface

Provide the complete read-only unordered associative interface compatible with the C++26
container vocabulary: standard member types, const iteration and local bucket iteration,
`empty`, `size`, `max_size`, `find`, `contains`, `count`, `equal_range`, `hash_function`,
`key_eq`, `bucket`, `bucket_count`, `max_bucket_count`, `bucket_size`, `load_factor`,
and the `max_load_factor` observer. Maps additionally provide checked `at` and optional
`lookup`; sets provide optional `lookup` as an MBO extension. Transparent hashing and
equality enable heterogeneous lookup, including map `at`. Use MBO `OptionalRef<const T>`
for optional references on the C++23 baseline. Provide content equality and the applicable
Limited-container conveniences (`capacity`, `at_index`, `index_of`, `contains_all`,
`contains_any`). Iterators and map values remain read-only even on non-const objects.

The first implementation is fully frozen. `constexpr` does not imply immutability; this
is a separate API choice. Keep construction/index machinery separate from the public layer
so a later mutable container can reuse it. Updating mapped values does not require a new
index; inserting keys may require rebuilding the perfect index. Runtime construction is
supported now; post-construction mutation is a possible second layer, not part of this first
step. The names `FrozenMap` and `FrozenSet` designate the fully frozen layer.

This first layer is not a mutable standard-container replacement: insertion,
erasure, extraction, merge, inserting `operator[]`, rehash/reserve, load-factor mutation,
allocator ownership, and assignment into existing elements are intentionally excluded.
Document this compatibility boundary explicitly; do not claim full mutable conformance.

## Index and lookup guarantees

- Map and set share perfect-hash index construction. Support minimal and non-minimal sparse
  slot counts through options, with bounded displacement search and checked storage/work
  limits. Construction failure must be diagnosable without unbounded compiler evaluation.
- Verify actual occupied-slot uniqueness during construction. Unique full hashes alone are
  insufficient. Detect irreconcilable full-hash collisions and unsuccessful bounded search.
- Always check full key equality against the candidate, including misses that land in
  occupied slots. For string keys, safely handle empty strings, embedded NUL, arbitrary
  non-ASCII bytes, and long unknown inputs. Never index using a signed character.
- Default hashes use the same algorithm during constant evaluation and at runtime, with
  deterministic generation across supported compilers. Custom hash/equality objects must
  obey the same contract, including equal hashes for equivalent heterogeneous keys.
- Lookup allocates no container memory. User-provided hash/equality operations must be
  allocation-free to retain that guarantee. Absence is explicit through end iterators,
  optional references, or the documented `npos` index sentinel.

## Verification and performance evidence

- Cover empty/singleton/many-element maps and sets, generic keys/values, non-default-
  constructible values, copies, iterator/range concepts, every public observer and lookup,
  transparent overload participation, and C++26 compilation.
- Test duplicate diagnostics, capacity/budget failures, hash collisions, unsuccessful
  displacement search, adversarial misses, string byte edge cases, aliases, and independent
  mode-like tables. Prove registered keys occupy distinct slots and runtime hashes agree
  with constexpr results. Add compiler-negative checks for construction diagnostics.
- Benchmark maps and sets against existing Limited containers and linear scans, with hit,
  miss, and mixed workloads, multiple sizes, integral and string keys, and minimal/sparse
  perfect layouts. Retain reproducible commands and measurement tooling for build time,
  constexpr evaluation cost, object/read-only-data size, and process startup alongside
  lookup latency. Do not select a layout from asymptotic complexity alone or claim parser
  speedups from container microbenchmarks.
- Run `bazel test //...`. Do not run lint locally for this change; leave lint to CI. Keep
  required warnings, sanitizer, compiler-matrix, and coverage gates intact.
