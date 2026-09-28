# SegmentedDeque benchmark harness

The public contract is [SegmentedDeque](SEGMENTED_DEQUE.md). Retained segmented measurement
results have been removed; rerun the harness for the source revision under consideration.

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
