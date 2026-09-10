# Ordered lookup microbenchmarks

The production and proof protocol for the new append-oriented stable-address container lives in
[`SEGMENTED_SEQUENCE.md`](SEGMENTED_SEQUENCE.md). Its measurements use immutable JSON artifacts and
the two-machine evidence gate; the historical ordered-lookup study below predates that shared
artifact contract.

The ordered-lookup experiment for mbo pull request 411 measured 648 cases with nine repetitions
each. It was built with `-c opt` on an 18-core Apple M5 Pro (Mac17,9, 64 GB) and measured on
2026-09-04.

The matrix covers `LimitedSet` and `LimitedMap`; capacities 2, 4, 8, 16, 17, and 32; ascending
`std::less` and `CompareLess` plus descending `std::greater`; `index_of`, `lower_bound`, and
`upper_bound`; and keys before, at the first element, between middle elements, at a middle element,
at the last element, and after the container.

## Measurement and comparison method

For each named case, sort the nine raw `cpu_time` values from lowest to highest and take the
arithmetic mean of the lowest three. This average-of-best-3-of-9 value is the repository's
established microbenchmark aggregation. Compare a candidate with its baseline as:

```text
delta_percent = ((candidate_best3 / baseline_best3) - 1) * 100
```

A negative delta means the candidate is faster. The bounds experiment required every case to be
equal or faster; any positive delta therefore failed that requirement. Capacities 17 and 32 do not
select the small-container unrolled implementation and serve as noise controls. Raw and derived
results are intentionally not tracked because the benchmark and procedure reproduce them.

## Reproducing a measurement

Build the revision to measure and run:

```sh
bazel build -c opt //mbo/container:ordered_lookup_benchmark
bazel-bin/mbo/container/ordered_lookup_benchmark \
  --benchmark_repetitions=9 \
  --benchmark_min_time=0.005s \
  --benchmark_out=ordered_lookup_result.json \
  --benchmark_out_format=json
```

Run competing revisions under otherwise identical conditions on the same idle machine. Do not
compare the JSON's generated aggregate rows: select the nine raw iteration rows for each benchmark
name and apply the calculation above. Retain the output only as long as needed to calculate and
review the comparison.

The baseline was measured from the benchmark-only implementation merged by pull request 412. The
bounds candidate can be reconstructed at commit `023e19c91`. To reproduce the non-unrolled
`index_of` comparison from the baseline, temporarily replace its `kOptimizeIndexOf` initializer in
`mbo/container/internal/limited_ordered.h` with `false`, rebuild, and rerun the same command.

Google Benchmark could not read `hw.cpufrequency` or set thread affinity on macOS. Those limitations
apply equally to the baseline and comparison runs; comparisons must use repeated samples and treat
noise statistically rather than relying on a single timing.

## Bounds-unrolling candidate

The candidate did not meet the no-regression requirement: 333 of 648 cases regressed, including
228 by more than 5% and 199 by more than 10%. Lower-bound first/middle cases at capacities 8 and 16
show repeated large losses, with the worst measured case regressing by 151.2%. The implementation
must not proceed on these results. `lower_bound` had a 17.8% median regression across the changed
capacities. `upper_bound` improved by 2.0% at the median but was inconsistent, with 63 of 144 cases
regressing. The conclusion was to retain the standard-library bound algorithms.

The decisive subset is the 288 bounds cases at capacities 2, 4, 8, and 16, where the candidate
actually selected the unrolled path:

| Operation     | Faster or equal |  Slower | Median delta | Mean delta | Worst regression |
| ------------- | --------------: | ------: | -----------: | ---------: | ---------------: |
| `lower_bound` |          58/144 |  86/144 |       +17.8% |     +19.2% |          +151.2% |
| `upper_bound` |          81/144 |  63/144 |        -2.0% |      -1.6% |           +60.3% |
| Combined      |         139/288 | 149/288 |        +4.9% |      +8.8% |          +151.2% |

The capacity breakdown shows why a general bounds optimization was rejected:

| Operation     | Capacity | Faster or equal | Median delta | Mean delta | Worst regression |
| ------------- | -------: | --------------: | -----------: | ---------: | ---------------: |
| `lower_bound` |        2 |           22/36 |        -3.2% |      +1.3% |           +40.1% |
| `lower_bound` |        4 |           15/36 |       +19.5% |     +12.0% |           +45.6% |
| `lower_bound` |        8 |            8/36 |       +27.7% |     +32.6% |          +108.9% |
| `lower_bound` |       16 |           13/36 |       +41.0% |     +30.8% |          +151.2% |
| `upper_bound` |        2 |           15/36 |       +13.0% |      +9.2% |           +60.3% |
| `upper_bound` |        4 |           23/36 |        -3.4% |      +1.5% |           +34.3% |
| `upper_bound` |        8 |           16/36 |        +9.3% |      +3.5% |           +59.5% |
| `upper_bound` |       16 |           27/36 |       -16.4% |     -20.8% |           +11.5% |

The backward linear scan favored answers near the end: for `lower_bound`, the median changes were
-4.6% at the last element and -26.8% after the range. It regressed by 27.8% before the range and by
30.8% to 31.0% around the middle. The position-dependent linear work explains the loss against the
standard library's binary search.

## Non-unrolled `index_of` comparison

For capacities 2, 4, 8, and 16, which select the optimized implementation normally, the unrolled
implementation was faster or equal in 143 of 144 cases. Disabling it was 80.6% slower at the
median. The only apparent loss was 1.0%, while the capacity-17 and capacity-32 controls had a
combined median movement of 0.4%. The existing `index_of` optimization should therefore remain.

|                        Capacity | Unrolled faster or equal | Median cost of disabling | Mean cost of disabling |
| ------------------------------: | -----------------------: | -----------------------: | ---------------------: |
|                               2 |                    36/36 |                   +90.3% |                 +79.0% |
|                               4 |                    35/36 |                   +50.3% |                 +55.5% |
|                               8 |                    36/36 |                   +72.6% |                +122.4% |
|                              16 |                    36/36 |                  +108.5% |                +164.5% |
|     Changed capacities combined |                  143/144 |                   +80.6% |                +105.3% |
| Capacities 17 and 32 (controls) |                    46/72 |                    +0.4% |                  +0.7% |

The improvement held across `std::less`, `std::greater`, and `CompareLess`, and across every lookup
position. The single 1.0% apparent loss was consistent with the control noise. This is conclusive
evidence to retain the existing small-container `index_of` unrolling.
