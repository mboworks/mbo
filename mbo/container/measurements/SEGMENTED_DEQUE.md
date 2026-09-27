# SegmentedDeque measurements

The semantic contract is [SegmentedDeque design](../SEGMENTED_DEQUE.md). This experiment compares
the initial experimental circular pointer directory with `SegmentedVector` and the host standard
library's `std::deque`. It measures the additional cost of double-ended operation without changing
the vector representation or selecting a tuned segment-size default.

## Workloads

`//mbo/container:segmented_deque_benchmark` uses identical templated traversal loops for each
container. Segment sizes are 64, 256, and 1,024 elements. The primary value is `uint64_t`; a second
shape is a 64-byte, 64-byte-aligned record with one consumed integer member.

| Family                      | Measured operation                                                          |
| --------------------------- | --------------------------------------------------------------------------- |
| `Indexed`                   | Sequential indexed traversal                                                |
| `Permuted`                  | Indexed traversal through an odd-multiplier permutation                     |
| `Forward` / `Reverse`       | Iterator traversal in the selected direction                                |
| `Arithmetic`                | Permuted iterator addition and dereference                                  |
| `Segments`                  | Traversal over live segment intervals                                       |
| `FreshBack` / `FreshFront`  | Construction, 16,384 insertions, destruction, and release                   |
| `QueueForward`              | Pop front and push back, with a steady 16,384-element window                |
| `QueueReverse`              | Pop back and push front, with the same window                               |
| `ArenaQueueForward/Reverse` | Steady queue in caller-owned monotonic storage, including directory storage |
| `ClearReuse`                | Clear and rebuild 16,384 elements using retained storage                    |
| `ReleaseRebuild`            | Release segments and rebuild 16,384 elements                                |
| `GrowthBack/Front`          | One insertion acquiring the third segment                                   |

Indexed and permuted 64-bit traversal use both 16,384 and 1,048,576 elements. Other traversals use
16,384 elements. The permutation is `(index * 8191) & (count - 1)`; the odd multiplier visits every
position because the counts are powers of two. Offset-31 cases first fill 31 extra values and remove
them from the front outside timing, separating a partial first segment from an aligned start.
All baselines consume the same logical values for matching offsets and element shapes.

Queue cases time batches of 256 pop/push pairs. Their setup makes one complete traversal of the
window before measuring. Arena cases instead explicitly reserve an extra segment before timing;
both approaches cover simultaneous partial endpoints. The arena has 512 KiB of caller-owned bytes,
a null upstream resource, and a counting resource shared by segment and directory allocation.
Any timed allocation or deallocation marks the arena case as an error. The warm-up allocation count
and requested bytes are recorded separately. These bytes include requests for obsolete directory
arrays retained by the monotonic resource, but exclude alignment padding.

Growth cases use 256-element segments and compare initial directory reservation 1 with reservation 128. Setup fills two complete segments at the measured end. The timed insertion acquires the third
segment and, for reservation 1, grows the directory from two slots to four. A PMR counter records
combined segment/directory allocation calls, requested bytes, and deallocation calls for this event.
Setup, teardown, and counter publication are outside the timed interval. This event's repeated mean
does not establish a tail-latency percentile.

Traversal and retained-lifecycle counters include acquired element capacity, segment count, and
source-reported reserved bytes. `std::deque` has no portable equivalent for those values, so its
storage metadata is not inferred. Fresh throughput cases use ordinary allocation without counter
instrumentation; they measure the complete lifecycle. Lifecycle cases use the same public methods
and include destruction costs, not merely resetting a logical count.

## Reproducibility

Build and warm dependencies first. Record the exact clean implementation commit, compiler, standard
library, host, command, duration, and load through `tools/benchmark_artifact.py`. Keep all nine
randomly interleaved repetitions. The JSON envelope is immutable; the Markdown report is derived
from those rows and includes minimum, fastest-three mean, median, all-nine mean, sample standard
deviation, coefficient of variation, and maximum. Flag noisy families rather than ranking small
differences within their dispersion.

Apple M5 Pro:

```sh
bazel build --config=clang --config=opt_apple_m5 -c opt //mbo/container:segmented_deque_benchmark
MBO_SEGDEQUE_SOURCE_SHA="$(git rev-parse HEAD)"
MBO_SEGDEQUE_BASELINE_SHA="$(git merge-base origin/main HEAD)"
python3 tools/benchmark_artifact.py run \
  --component SegmentedDeque \
  --target //mbo/container:segmented_deque_benchmark \
  --output "/private/tmp/macos-arm64-apple-m5-pro_clang-22_${MBO_SEGDEQUE_SOURCE_SHA}_segmented-deque-indexed.json" \
  --config clang --config opt_apple_m5 \
  --baseline-commit "${MBO_SEGDEQUE_BASELINE_SHA}" \
  -- bazel run --config=clang --config=opt_apple_m5 -c opt //mbo/container:segmented_deque_benchmark -- \
  --benchmark_filter='/(Indexed|Permuted)/'
```

Record four envelopes using the following filename suffixes and filters. Splitting the experiment
keeps each immutable JSON file within the repository's normal file-size gate. Matching container
baselines stay in the same group; random interleaving applies within each group.

| Suffix    | Filter                                                                              | Families |
| --------- | ----------------------------------------------------------------------------------- | -------: |
| indexed   | `/(Indexed\|Permuted)/`                                                             |       40 |
| traversal | `/(Forward\|Reverse\|Arithmetic\|Segments)/`                                        |       29 |
| lifecycle | `/(FreshBack\|FreshFront\|QueueForward\|QueueReverse\|ClearReuse\|ReleaseRebuild)$` |       31 |
| events    | `/(ArenaQueue\|Growth)`                                                             |       10 |

Pass the four envelopes followed by an output Markdown path to
`python3 tools/segmented_deque_report.py` to regenerate the combined report. The generator validates
each envelope and requires matching source, CPU, and toolchain metadata. It rejects duplicate or
missing repetitions and inconsistent work-item normalization.

For AMD Zen 5, substitute `--config=opt_zen5` for the machine configuration and record that host's
identity in the output filename. Compare the same clean source commit and workload families.
The first available-host report is diagnostic. Matching Zen 5 data remains necessary before
claiming cross-platform superiority or selecting a performance-tuned default.

## Apple M5 Pro evidence and decisions

The [complete generated report](data/macos-arm64-apple-m5-pro_clang-22_478dd4036dce507eab7f0530cebb06416fd34ae0_segmented-deque-summary.md) contains all 110 families and links the four
immutable JSON envelopes. They were recorded from clean, published source
[`478dd4036d`](https://github.com/mboworks/mbo/commit/478dd4036dce507eab7f0530cebb06416fd34ae0) on Apple M5 Pro with Clang 22.1.8,
C++23, libc++ 220106, Bazel 9.2.0, and `--config=opt_apple_m5`. Each family has nine randomized
repetitions, a one-second minimum, and a one-second warm-up. All benchmark correctness checks pass.
The later evidence commit changes documentation and data only; the measured implementation and
benchmark sources are unchanged.

The results support keeping `SegmentedDeque` separate from `SegmentedVector`. The offset and circular
directory have a measurable indexing cost, while the deque provides both-end mutation, stable
surviving iterators, and allocation-free arena queue reuse after reservation. Keep the directory
internal and experimental: these results establish a usable initial implementation, not a public
circular-array contract. No implementation fix or tuning-default change is required by these data.
The shared 256-element segment size and initial directory reservation of one remain the comparison
baseline. Matching Zen 5 evidence is still needed before selecting different tuning defaults.

### Access cost

These rows use 16,384 `uint64_t` elements, S256, and an aligned origin. Work means one visited
element. Deque sequential indexing costs 0.104 ns/element more than vector indexing at the median
(38.4%); its fastest sample is slower than the vector's slowest. Permuted indexing is 16.9% slower
at the median. Ordinary forward and reverse iterators also carry a cost relative to the vector.
Segment traversal is essentially equal: the median difference is 0.3%, within observed variation.
Reverse iteration and iterator arithmetic are faster than the host standard deque in these cases.

| Workload                     | Deque ns/work | Vector ns/work | Standard deque ns/work | Deque CV |
| ---------------------------- | ------------: | -------------: | ---------------------: | -------: |
| Sequential index             |         0.374 |          0.270 |                  0.279 |    0.71% |
| Permuted index               |         0.478 |          0.408 |                  0.364 |    1.35% |
| Forward iterator             |         0.358 |          0.271 |                  0.398 |    0.41% |
| Reverse iterator             |         0.372 |          0.324 |                  0.730 |    0.25% |
| Permuted iterator arithmetic |         0.470 |          0.407 |                  0.777 |    0.33% |
| Segment traversal            |         0.114 |          0.114 |                    n/a |    1.68% |

The default-source deque object is 56 bytes on this build, versus 40 for the vector and 48 for the
standard deque. The deque's extra origin and spare-list state account for its 16-byte increase over
the vector. PMR-backed deque cases use 72-byte objects. These are measured implementation sizes,
not portable ABI guarantees; allocated storage is accounted for separately.

### Queue and lifecycle cost

Queue work means one pop/push pair in a 256-pair batch with a 16,384-element window. Other rows
normalize the full lifecycle iteration by its 16,384 inserted elements, so their numbers are
throughput measurements, not isolated insertion latencies. Fresh construction includes destruction
and storage release. n/a means that the operation was not included for that baseline.

| Workload                 | Deque ns/work | Vector ns/work | Standard deque ns/work | Deque CV |
| ------------------------ | ------------: | -------------: | ---------------------: | -------: |
| Fresh back construction  |         2.888 |          3.003 |                  1.018 |    1.67% |
| Fresh front construction |         3.376 |            n/a |                  2.504 |    0.76% |
| Forward queue            |         2.827 |            n/a |                  3.060 |    0.50% |
| Reverse queue            |         3.094 |            n/a |                  3.214 |    0.15% |
| Clear and rebuild        |         2.860 |          4.857 |                    n/a |    1.55% |
| Release and rebuild      |         2.983 |          2.944 |                    n/a |    2.02% |

Steady S256 queues take 7.6% less CPU time per pair than the standard deque forward and 3.7% less
in reverse. Fresh back construction costs 2.84 times the standard deque's time; fresh front
construction costs 1.35 times as much. Fresh back and release/rebuild are close to the vector,
while clear/rebuild is faster in this experiment. These distinct workloads do not establish a
universally faster container or isolate an allocation's cost by subtracting lifecycle medians.

Segment sizes show tradeoffs: S1024 improves fresh growth over S256 on this host, while S256 has
the lowest measured forward queue median and the smallest warm arena byte count for this window.
This one-window, one-machine experiment is insufficient to select a general segment-size winner.

### Arena reuse and directory growth

All six arena queue families, across both directions and S64/S256/S1024, report zero timed
allocations and zero timed deallocations in every raw repetition. Both segment and directory
storage use the same bounded caller-owned arena. The following warm-up counts are identical in
both queue directions; they include obsolete directory arrays retained by the monotonic resource.
Requested bytes exclude alignment padding and the resource object's own storage.

| Segment size | Acquired segments | Element capacity | Warm allocation calls | Warm requested bytes |
| -----------: | ----------------: | ---------------: | --------------------: | -------------------: |
|           64 |               257 |            16448 |                   267 |               147992 |
|          256 |                65 |            16640 |                    73 |               137240 |
|         1024 |                17 |            17408 |                    23 |               140312 |

The isolated third-segment insertion compares initial directory reservation one with 128. Full
preallocation removes one 32-byte directory allocation and one deallocation from that event;
element segment storage is unchanged. It reduces the recorded median by about 4.0% at the back
and 3.2% at the front. The event means include benchmark timing/instrumentation overhead and do
not establish a tail-latency percentile or a general latency benefit from full preallocation.
The default reservation therefore remains one; callers can reserve for their known workloads.

| End   | Directory    | Median ns/event | Allocation calls | Requested bytes | Deallocation calls |
| ----- | ------------ | --------------: | ---------------: | --------------: | -----------------: |
| back  | Grows 2 to 4 |        1199.306 |                2 |            2112 |                  1 |
| back  | 128 reserved |        1151.594 |                1 |            2080 |                  0 |
| front | Grows 2 to 4 |        1198.245 |                2 |            2112 |                  1 |
| front | 128 reserved |        1159.923 |                1 |            2080 |                  0 |

### Evidence limits

The indexed group has 21 of 40 families above 10% CV, with a maximum of 31.46%; larger working-set
and shaped-element close rankings are diagnostic. The traversal group has one noisy family,
`SegmentedVector/S64/Arithmetic/Offset0/16384` at 23.79% CV. Its S256 comparisons above are steady.
All 31 lifecycle and 10 arena/growth families remain below 3% CV. The generated report retains
all samples' summaries, including minimum, fastest-three mean, median, mean, sample SD, CV, and
maximum; it does not discard the noisy families.

macOS did not provide usable CPU-frequency metadata or thread affinity. Host load snapshots are
recorded in each envelope; local build, test, and lint jobs finished before timing began. UTC spans
and monotonic durations agree within 0.01 seconds for each run. These limits and the missing Zen 5
run restrict the conclusions to this available-host experiment. No result changes the vector
representation, introduces a public ring container, or selects a performance-tuned default.
