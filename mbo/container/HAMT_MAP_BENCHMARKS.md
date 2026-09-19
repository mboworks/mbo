# General HAMT map benchmark harness

`//mbo/container:hamt_map_benchmark` is a manual, test-only comparison of
read-only general maps. It is not the final performance report. Final measurements
follow the complete implementation and each draft PR's own-context CI validation.

The initial 198 cases compare `HamtFlatMap` and `HamtNodeMap` at fragment widths
4, 5, 6, and 7 against `std::unordered_map`, `absl::flat_hash_map`, and
`absl::node_hash_map`. All use the same 64-bit integer mixer, keys, mapped values,
cardinality, and lookup loop. Sizes are 64, 1,024, and 16,384 entries. Each batch
visits every key once in a deterministic permutation, either all hits or all
misses. The power-of-two sizes make the odd-multiplier permutation bijective.

Population and exhaustive hit/miss/value validation happen before timing.
HAMTs are populated through persistent insertion, discarding the previous snapshot
after each insertion, and then exposed as a const snapshot. This avoids requesting
mutable iterators during untimed setup: the current mutable-iterator preparation
walks the tree and would distort setup scalability. The other maps are populated
through ordinary insertion. Population,
destruction, mutation, persistence conversion, and allocation costs are therefore
not measured by these lookup cases. A compiler escape and identical per-batch
memory barriers prevent hoisting the complete read-only batch out of timing.

This harness deliberately measures public `find`, including iterator construction
where a container requires it; it does not substitute an internal pointer lookup
for the HAMTs. Stored keys and query keys are unsigned 64-bit integers, so this
initial matrix does not cover heterogeneous lookup or string-key hashing costs.

Traversal adds one batch over every container's native const iterator order.
An untimed preflight verifies every key appears exactly once with the expected
mapped value; it does not assume hash-container iteration orders agree. Timed
batches sum mapped values, with the same compiler escape and memory barriers as
lookup. These cases measure complete traversal, not random indexed access or
mutable ownership preparation.

`FillEraseFresh` starts with a new editable map for each iteration, inserts every
key once, then erases every key in the same permutation used by lookup. HAMTs
use a transient; comparison maps use their ordinary mutable interface. The timed
cycle includes construction, public iterator/bool insertion results, count erasure,
and destruction. It neither reserves standard containers nor retains capacity
between iterations. An untimed complete cycle validates every insertion result,
mapped value, erasure count, and final emptiness. Timed results escape to the
compiler without adding correctness branches; items processed count insertion
and erasure separately. This measures fresh end-to-end churn, not isolated
insertion latency or shared-snapshot detachment.

`BranchUpdateOne` and `BranchUpdateAll` retain an unchanged populated parent
and create an editable child for every timed iteration. HAMTs create a transient
sharing the parent; standard/Abseil maps copy the parent because they do not
provide persistent snapshots. The child updates one mapped value or every mapped
value through public `operator[]`, then is destroyed. Branch creation, ownership
detachment or full copying, updates, and child destruction are timed together.
Parent population and destruction are not timed. An untimed preflight checks all
parent and child values and both cardinalities; this validates isolation, not just
the updated value. These cases compare equivalent independent-child outcomes,
not equivalent internal algorithms or pure update latency. Items processed count
mapped edits, so compare matching cases rather than one-edit and all-edit rates.

Remaining measurement work includes isolated mutation,
transient conversion, collision-heavy inputs, bounded sources, allocation and
memory accounting, and representative heterogeneous keys. These lookup cases
alone cannot establish an overall winner or justify every storage option.
Use nine randomly interleaved repetitions, warmup, immutable raw JSON artifacts,
and the shared report tooling for final charts. ASAN or one-iteration smoke runs
prove harness execution only and must not become performance claims. Initial
host decisions remain provisional until the later AMD Zen 5 follow-up.
