# SegmentedVector benchmark harnesses

The public contract is [SegmentedVector](SEGMENTED_VECTOR.md). Retained segmented measurement
results have been removed; rerun the harnesses for the source revision under consideration.

## Production benchmark

`//mbo/container:segmented_vector_benchmark` measures the public implementation using 16,384
64-bit elements. Its matrix separates the costs that a single aggregate result would hide:

| Family                        | What it measures                                                                |
| ----------------------------- | ------------------------------------------------------------------------------- |
| `FreshConstructAppendDestroy` | Construction, append/growth, element destruction, and storage release           |
| `RetainedAppendClear`         | Append into acquired segments followed by timed element destruction via `clear` |
| `GrowthBoundary`              | One append that adds a segment, with and without directory reallocation         |
| `Indexed`                     | Sequential shift/mask dense-position lookup                                     |
| `IndexedPermuted`             | Cache-resistant dense-position lookup in deterministic permutation              |
| `IteratorForward`             | Element-wise forward iterator traversal                                         |
| `IteratorArithmetic`          | Random-access iterator addition and dereference                                 |
| `IteratorReverse`             | Reverse-iterator traversal                                                      |
| `Segments`                    | Traversal through constructed prefixes using non-contiguous segment views       |
| `Vector` / `Deque`            | Matching fresh construct/append/destroy standard-library baselines              |

The production target exercises fixed power-of-two segment sizes of 64, 256, and 1,024 elements.
For 256-element segments, three otherwise identical finite 64-segment configurations use directory
reservations 0, 1, and 64. Their full fresh cycles expose the construction-versus-growth tradeoff
without comparing finite capacity against an unbounded configuration as though reservation were
the only difference.

Growth-boundary cases prepare two full segments and isolate the append that adds the third. The
reservation-0 and reservation-1 cases cross the directory's `2 -> 4` threshold; reservation 64
preallocates the complete directory. Names encode segment size, finite capacity, exact reservation,
and whether the boundary reallocates. Each case reports source allocations/bytes and directory
allocations, deallocations, allocated bytes, and deallocated bytes for the timed event. This is an
isolated growth-event mean, not a tail percentile.

Every measured `SegmentedVector` reports current element capacity, segment count, and
source-reported reserved bytes. Counter publication occurs outside timed loops, or while timing is
paused for the isolated growth event. Growth and lifecycle cases add event allocation/deallocation
calls and bytes. Vector reports capacity bytes; deque is timing-only because it exposes no portable
retained-allocation metadata. Unused tail slots are derivable where the final size is fixed. The
harness does not claim to capture every allocator or metadata consequence.

## Element-shape benchmark

`//mbo/container:segmented_vector_element_shape_benchmark` measures the actual fixed-segment
container rather than the superseded synthetic listed-capacity candidates. It compares sequential
and deterministic permuted indexed lookup with 64-, 256-, and 1,024-element segments across:

- 1-, 2-, 4-, and 8-byte integral values;
- a 16-byte POD and the pointer-plus-size record needed by StringInterner;
- 64- and 256-byte PODs;
- a 64-byte, 64-byte-aligned POD; and
- `std::string` as a non-trivial movable value.

The StringInterner-shaped record receives a valid non-null backing address and length; timed reads
consume both pointer bits and size without dereferencing payload bytes, preserving lookup
isolation. Every case reports element size/alignment, acquired element capacity, source-reported
reserved bytes, and segment count. Construction occurs outside the timed loop so the experiment
isolates the element-shape effect on the current shift/mask plus flat-pointer-directory lookup.

## Lifecycle benchmark

`//mbo/container:segmented_vector_lifecycle_benchmark` measures lifecycle behavior supported by
the current public contract. For 64- and 256-element segments it compares:

- pop/regrow while retaining emptied tail segments;
- pop, `trim_capacity()`, and regrow at shallow, medium, and full depths;
- `clear()` followed by reuse; and
- `release()` followed by rebuilding.

The first pop depth is exactly one segment for each configuration: 64 elements for S64 and 256 for
S256. The other depths are 4,096 and 16,384 elements. Trim therefore releases at least one segment
in every trimmed case rather than using a nominally shallow depth that leaves S256 unchanged.

Cases report low-water and restored capacity, source bytes, and segment counts together with the
per-cycle source acquisitions/releases and bytes acquired/released, plus directory
allocations/deallocations and bytes allocated/deallocated. Counter increments are minimal
instrumentation inside the timed source/allocator operations; counters accumulate across all timed
cycles without per-iteration reset, then are divided and published after the loop. Final object
teardown occurs after reporting and is not charged to a measured cycle. This couples latency to its
observed storage events without reviving removed retention budgets or spare-block pools.

## Reference commands

Warm dependencies and build outputs before timing. Then run from a clean checkout of the exact
implementation commit. The filename is descriptive; metadata inside the artifact is authoritative.

Apple M5 Pro:

```sh
MBO_SEGVEC_SOURCE_SHA="$(git rev-parse HEAD)"
MBO_SEGVEC_BASELINE_SHA="$(git merge-base origin/main HEAD)"
python3 tools/benchmark_artifact.py run \
  --component SegmentedVector \
  --target //mbo/container:segmented_vector_benchmark \
  --output "/private/tmp/macos-arm64-apple-m5-pro_clang-22_${MBO_SEGVEC_SOURCE_SHA}_segmented-vector.json" \
  --config clang \
  --config opt_apple_m5 \
  --baseline-commit "${MBO_SEGVEC_BASELINE_SHA}" \
  -- bazel run //mbo/container:segmented_vector_benchmark --config=clang --config=opt_apple_m5 -c opt --
```

AMD Zen 5:

```sh
MBO_SEGVEC_SOURCE_SHA="$(git rev-parse HEAD)"
MBO_SEGVEC_BASELINE_SHA="$(git merge-base origin/main HEAD)"
python3 tools/benchmark_artifact.py run \
  --component SegmentedVector \
  --target //mbo/container:segmented_vector_benchmark \
  --output "/private/tmp/linux-x86-64-amd-ryzen-9-9950x_clang-22_${MBO_SEGVEC_SOURCE_SHA}_segmented-vector.json" \
  --config clang \
  --config opt_zen5 \
  --baseline-commit "${MBO_SEGVEC_BASELINE_SHA}" \
  -- bazel run //mbo/container:segmented_vector_benchmark --config=clang --config=opt_zen5 -c opt --
```

Run the same protocol separately for
`//mbo/container:segmented_vector_element_shape_benchmark` and
`//mbo/container:segmented_vector_lifecycle_benchmark`, using distinct output filenames and the
matching Bazel target in both `--target` and the command after `--`.

Write every output outside the checkout. Run all three targets from the same clean source SHA, then
validate every output envelope before using it in a comparison.
Writing the first result into the worktree would make later runs dirty and therefore ineligible for
`valid` status. The runner rejects a valid dirty-checkout envelope, errored or incomplete benchmark
families, duplicate or missing repetition indices, and invalid iteration or timing fields. Failed
runs remain preservable as `invalid` only when accompanied by a validity note.

The runner's defaults are required: nine repetitions, randomized interleaving, one-second warmup,
and one-second minimum time. Short executions are smoke diagnostics only and cannot select an
implementation. Validate generated evidence with:

```sh
python3 tools/benchmark_artifact.py validate /private/tmp/*segmented-vector*.json
```

## Additional comparison workloads

The main harness also includes `std::list` lifecycle, vector retained append/clear, forward
iteration against vector/deque/list, sequential/permuted indexing against vector/deque, and
push/pop cycles for all four containers. Push/pop cycles perform 32,768 operations per iteration
and validate removal order before timing. Other full traversals process 16,384 elements. Read
loops contain memory barriers against hoisting. The main context is `segmented-vector-v4`;
shape and lifecycle contexts remain v3. Smoke runs validate execution, not performance.
