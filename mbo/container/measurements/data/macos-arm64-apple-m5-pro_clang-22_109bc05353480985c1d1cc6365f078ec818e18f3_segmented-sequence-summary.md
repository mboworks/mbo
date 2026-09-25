# Apple M5 Pro C++23 SegmentedSequence evidence

These tables are derived from the three immutable JSON envelopes recorded at source `109bc05353480985c1d1cc6365f078ec818e18f3`.
That source tree is byte-for-byte identical to merged commit `43bca82254757ce2f2d1023151de9b362968a175`. The measured symbol and
benchmark-family name was `SegmentedSequence`; the current API is renamed to `SegmentedVector` in
the evidence follow-up, so the historical raw names below remain unchanged for provenance.

All cases use Clang 22.1.8, C++23, libc++, Bazel 9.2.0, nine randomly interleaved repetitions,
one-second warmup, and one-second minimum time. Every timing is CPU nanoseconds for one benchmark
iteration. `Fast 3` is the arithmetic mean of the three smallest repetition means; sample SD and
CV use all nine. The corresponding JSON, not these derived tables, is authoritative.

## Production matrix

Artifact: [`macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence.json`](macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence.json)

| Family                                                                        |   Min ns | Fast 3 ns | Median ns |  Mean ns | Sample SD ns |    CV |   Max ns | Capacity | Reserved bytes | Segments |
| ----------------------------------------------------------------------------- | -------: | --------: | --------: | -------: | -----------: | ----: | -------: | -------: | -------------: | -------: |
| `Deque/FreshConstructAppendDestroy`                                           | 16328.45 |  16360.52 |  16458.54 | 16858.81 |      1101.97 | 6.54% | 19775.46 |      n/a |            n/a |      n/a |
| `SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation0`  | 47383.43 |  47469.79 |  47572.25 | 47585.33 |       111.16 | 0.23% | 47717.07 |    16384 |         133120 |       64 |
| `SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation1`  | 47310.99 |  47386.12 |  47526.08 | 47595.37 |       197.51 | 0.41% | 47822.80 |    16384 |         133120 |       64 |
| `SegmentedSequence/FreshConstructAppendDestroy/S256/Capacity64/Reservation64` | 47734.08 |  47770.96 |  47834.52 | 47853.66 |        97.09 | 0.20% | 48058.48 |    16384 |         133120 |       64 |
| `SegmentedSequence/FreshConstructAppendDestroy/Uniform1024`                   | 44515.99 |  44568.86 |  44645.75 | 44665.20 |        95.92 | 0.21% | 44808.42 |    16384 |         131584 |       16 |
| `SegmentedSequence/FreshConstructAppendDestroy/Uniform256`                    | 47235.06 |  47298.35 |  47482.65 | 47456.26 |       141.62 | 0.30% | 47623.00 |    16384 |         133120 |       64 |
| `SegmentedSequence/FreshConstructAppendDestroy/Uniform64`                     | 58964.16 |  59076.19 |  59194.30 | 60095.14 |      2793.50 | 4.65% | 67540.86 |    16384 |         139264 |      256 |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation0/Cross2To4`     |  1178.11 |   1179.14 |   1182.73 |  1182.69 |         3.34 | 0.28% |  1188.40 |      768 |           6240 |        3 |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation1/Cross2To4`     |  1170.60 |   1173.11 |   1182.12 |  1181.98 |         8.35 | 0.71% |  1195.70 |      768 |           6240 |        3 |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation64/Preallocated` |  1151.39 |   1155.92 |   1161.19 |  1162.84 |         7.22 | 0.62% |  1173.13 |      768 |           6240 |        3 |
| `SegmentedSequence/Indexed/Uniform1024`                                       |  8164.88 |   8170.48 |   8177.87 |  8180.54 |        12.00 | 0.15% |  8202.25 |    16384 |         131584 |       16 |
| `SegmentedSequence/Indexed/Uniform256`                                        |  7083.83 |   7089.57 |   7095.78 |  7200.86 |       290.81 | 4.04% |  7974.92 |    16384 |         133120 |       64 |
| `SegmentedSequence/Indexed/Uniform64`                                         | 10183.33 |  10192.68 |  10217.20 | 10337.03 |       367.62 | 3.56% | 11315.85 |    16384 |         139264 |      256 |
| `SegmentedSequence/IndexedPermuted/Uniform1024`                               |  8079.50 |   8084.82 |   8103.95 |  8108.42 |        29.61 | 0.37% |  8176.49 |    16384 |         131584 |       16 |
| `SegmentedSequence/IndexedPermuted/Uniform256`                                |  7140.78 |   7142.26 |   7211.22 |  7210.53 |        73.25 | 1.02% |  7332.27 |    16384 |         133120 |       64 |
| `SegmentedSequence/IndexedPermuted/Uniform64`                                 |  8078.68 |   8084.36 |   8098.87 |  8117.42 |        41.04 | 0.51% |  8197.35 |    16384 |         139264 |      256 |
| `SegmentedSequence/IteratorArithmetic/Uniform1024`                            |  8191.76 |   8196.25 |   8208.56 |  8247.04 |       117.11 | 1.42% |  8557.41 |    16384 |         131584 |       16 |
| `SegmentedSequence/IteratorArithmetic/Uniform256`                             |  7067.10 |   7069.01 |   7087.16 |  7085.24 |        13.30 | 0.19% |  7100.60 |    16384 |         133120 |       64 |
| `SegmentedSequence/IteratorArithmetic/Uniform64`                              |  8123.63 |   8127.60 |   8139.00 |  8140.45 |        14.35 | 0.18% |  8171.43 |    16384 |         139264 |      256 |
| `SegmentedSequence/IteratorForward/Uniform1024`                               |  8211.42 |   8213.51 |   8221.78 |  8224.02 |        10.93 | 0.13% |  8242.01 |    16384 |         131584 |       16 |
| `SegmentedSequence/IteratorForward/Uniform256`                                |  7125.35 |   7130.69 |   7137.56 |  7138.21 |         7.92 | 0.11% |  7152.61 |    16384 |         133120 |       64 |
| `SegmentedSequence/IteratorForward/Uniform64`                                 |  8143.43 |   8147.71 |   8156.18 |  8323.87 |       489.53 | 5.88% |  9628.71 |    16384 |         139264 |      256 |
| `SegmentedSequence/IteratorReverse/Uniform1024`                               |  8199.30 |   8205.30 |   8217.63 |  8222.29 |        17.23 | 0.21% |  8247.34 |    16384 |         131584 |       16 |
| `SegmentedSequence/IteratorReverse/Uniform256`                                |  7070.12 |   7073.63 |   7087.42 |  7087.73 |        15.92 | 0.22% |  7119.63 |    16384 |         133120 |       64 |
| `SegmentedSequence/IteratorReverse/Uniform64`                                 |  9643.07 |   9655.84 |   9677.13 |  9687.86 |        46.24 | 0.48% |  9800.27 |    16384 |         139264 |      256 |
| `SegmentedSequence/RetainedAppendClear/Uniform1024`                           | 77423.62 |  77522.91 |  77622.41 | 77660.70 |       142.14 | 0.18% | 77890.28 |    16384 |         131584 |       16 |
| `SegmentedSequence/RetainedAppendClear/Uniform256`                            | 78062.91 |  78137.51 |  78205.60 | 78307.36 |       191.33 | 0.24% | 78575.19 |    16384 |         133120 |       64 |
| `SegmentedSequence/RetainedAppendClear/Uniform64`                             | 77684.63 |  77727.91 |  77975.90 | 77959.05 |       200.12 | 0.26% | 78274.47 |    16384 |         139264 |      256 |
| `SegmentedSequence/Segments/Uniform1024`                                      |  1522.11 |   1523.39 |   1526.85 |  1527.85 |         4.50 | 0.29% |  1534.63 |    16384 |         131584 |       16 |
| `SegmentedSequence/Segments/Uniform256`                                       |  1864.54 |   1867.76 |   1877.38 |  1919.86 |       135.78 | 7.07% |  2281.56 |    16384 |         133120 |       64 |
| `SegmentedSequence/Segments/Uniform64`                                        |  3123.16 |   3134.96 |   3168.20 |  3157.10 |        17.99 | 0.57% |  3175.37 |    16384 |         139264 |      256 |
| `Vector/FreshConstructAppendDestroy`                                          | 20604.63 |  20845.44 |  21119.79 | 21106.79 |       249.73 | 1.18% | 21403.89 |    16384 |         131072 |      n/a |

### Isolated growth-event counters

| Family                                                                        | Source allocs | Source bytes | Directory allocs | Directory bytes | Directory frees | Directory freed bytes |
| ----------------------------------------------------------------------------- | ------------: | -----------: | ---------------: | --------------: | --------------: | --------------------: |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation0/Cross2To4`     |             1 |         2080 |                1 |              32 |               1 |                    16 |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation1/Cross2To4`     |             1 |         2080 |                1 |              32 |               1 |                    16 |
| `SegmentedSequence/GrowthBoundary/S256/Capacity64/Reservation64/Preallocated` |             1 |         2080 |                0 |               0 |               0 |                     0 |

All 32 production families have CV at or below 7.07%. At the isolated S256 third-segment
boundary, reservation 64 removes one 32-byte directory allocation and one 16-byte directory
deallocation, but changes median time only from 1,182.73/1,182.12 ns for reservations 0/1 to
1,161.19 ns: about 1.8%. The corresponding complete fresh-cycle medians of 47,572.25, 47,526.08,
and 47,834.52 ns do not establish a general reservation winner.

The S256 fresh-cycle median (47,482.65 ns) and retained append/clear median (78,205.60 ns) are
not equivalent workloads: one constructs and destroys the container and storage, while the other
reuses acquired segments and times `clear()`. Their ordering does not show that reuse is generally
slower. No segment-size or reservation default is selected from this one-machine diagnostic.

## Element-shape matrix

Artifact: [`macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence-element-shape.json`](macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence-element-shape.json)

| Family                                       |   Min ns | Fast 3 ns | Median ns |  Mean ns | Sample SD ns |     CV |   Max ns | Capacity | Reserved bytes | Segments | Element bytes | Element alignment |
| -------------------------------------------- | -------: | --------: | --------: | -------: | -----------: | -----: | -------: | -------: | -------------: | -------: | ------------: | ----------------: |
| `ElementShape/Permuted/S1024/Aligned64`      | 12081.13 |  12148.90 |  12203.16 | 12578.99 |      1190.18 |  9.46% | 15751.03 |    16384 |        1049600 |       16 |            64 |                64 |
| `ElementShape/Permuted/S1024/Blob16`         |  8392.85 |   8415.06 |   8494.64 |  9133.61 |      1661.65 | 18.19% | 13464.69 |    16384 |         262656 |       16 |            16 |                 8 |
| `ElementShape/Permuted/S1024/Blob256`        | 26881.83 |  27066.79 |  27223.57 | 27229.54 |       165.21 |  0.61% | 27419.97 |    16384 |        4194816 |       16 |           256 |                 8 |
| `ElementShape/Permuted/S1024/Blob64`         | 11815.26 |  11850.08 |  12017.97 | 12626.61 |      1944.30 | 15.40% | 17798.28 |    16384 |        1049088 |       16 |            64 |                 8 |
| `ElementShape/Permuted/S1024/String`         | 13027.30 |  13034.87 |  13244.87 | 13386.99 |       372.04 |  2.78% | 14023.77 |    16384 |         393728 |       16 |            24 |                 8 |
| `ElementShape/Permuted/S1024/StringRecord`   |  9575.49 |   9598.11 |   9632.77 |  9916.63 |       787.08 |  7.94% | 12008.71 |    16384 |         262656 |       16 |            16 |                 8 |
| `ElementShape/Permuted/S1024/U16`            |  5787.29 |   5793.59 |   5800.02 |  5804.64 |        12.85 |  0.22% |  5826.94 |    16384 |          33280 |       16 |             2 |                 2 |
| `ElementShape/Permuted/S1024/U32`            |  5791.51 |   5793.29 |   5799.75 |  5804.38 |        15.36 |  0.26% |  5841.67 |    16384 |          66048 |       16 |             4 |                 4 |
| `ElementShape/Permuted/S1024/U64`            |  8091.65 |   8097.82 |   8114.22 |  8775.92 |      1264.29 | 14.41% | 11200.69 |    16384 |         131584 |       16 |             8 |                 8 |
| `ElementShape/Permuted/S1024/U8`             |  7754.72 |   7761.29 |   7772.53 |  7787.67 |        55.05 |  0.71% |  7932.54 |    16384 |          16896 |       16 |             1 |                 1 |
| `ElementShape/Permuted/S256/Aligned64`       | 11608.67 |  11684.27 |  11818.02 | 11870.03 |       258.34 |  2.18% | 12497.25 |    16384 |        1052672 |       64 |            64 |                64 |
| `ElementShape/Permuted/S256/Blob16`          |  7550.81 |   7567.12 |   7580.58 |  7703.27 |       176.08 |  2.29% |  7992.70 |    16384 |         264192 |       64 |            16 |                 8 |
| `ElementShape/Permuted/S256/Blob256`         | 26598.45 |  26696.98 |  26850.27 | 28233.32 |      4060.94 | 14.38% | 39049.76 |    16384 |        4196352 |       64 |           256 |                 8 |
| `ElementShape/Permuted/S256/Blob64`          | 11490.80 |  11652.12 |  11829.11 | 14177.42 |      4528.28 | 31.94% | 25339.23 |    16384 |        1050624 |       64 |            64 |                 8 |
| `ElementShape/Permuted/S256/String`          | 13168.74 |  13388.09 |  13553.19 | 14041.91 |      1397.13 |  9.95% | 17694.55 |    16384 |         395264 |       64 |            24 |                 8 |
| `ElementShape/Permuted/S256/StringRecord`    | 10851.80 |  10865.07 |  10972.70 | 12055.56 |      2965.14 | 24.60% | 19908.49 |    16384 |         264192 |       64 |            16 |                 8 |
| `ElementShape/Permuted/S256/U16`             |  5329.77 |   5330.85 |   5336.09 |  5338.44 |         8.05 |  0.15% |  5351.00 |    16384 |          34816 |       64 |             2 |                 2 |
| `ElementShape/Permuted/S256/U32`             |  5331.67 |   5333.00 |   5341.86 |  5441.48 |       308.60 |  5.67% |  6264.33 |    16384 |          67584 |       64 |             4 |                 4 |
| `ElementShape/Permuted/S256/U64`             |  7140.05 |   7149.37 |   7219.10 |  7214.98 |        56.29 |  0.78% |  7307.82 |    16384 |         133120 |       64 |             8 |                 8 |
| `ElementShape/Permuted/S256/U8`              |  6650.32 |   6653.39 |   6662.84 |  6843.33 |       544.70 |  7.96% |  8295.68 |    16384 |          18432 |       64 |             1 |                 1 |
| `ElementShape/Permuted/S64/Aligned64`        | 12191.15 |  12354.98 |  12762.42 | 12810.24 |       631.37 |  4.93% | 14370.43 |    16384 |        1064960 |      256 |            64 |                64 |
| `ElementShape/Permuted/S64/Blob16`           |  9853.41 |  10265.35 |  10885.94 | 10993.29 |       706.31 |  6.42% | 12084.57 |    16384 |         270336 |      256 |            16 |                 8 |
| `ElementShape/Permuted/S64/Blob256`          | 26836.60 |  27000.37 |  27294.13 | 27243.34 |       214.92 |  0.79% | 27514.18 |    16384 |        4202496 |      256 |           256 |                 8 |
| `ElementShape/Permuted/S64/Blob64`           | 12454.78 |  12522.65 |  12836.85 | 12783.94 |       239.50 |  1.87% | 13128.39 |    16384 |        1056768 |      256 |            64 |                 8 |
| `ElementShape/Permuted/S64/String`           | 13288.10 |  13409.35 |  13789.13 | 13786.45 |       382.43 |  2.77% | 14521.58 |    16384 |         401408 |      256 |            24 |                 8 |
| `ElementShape/Permuted/S64/StringRecord`     | 10390.58 |  10602.48 |  10883.28 | 11133.99 |       851.40 |  7.65% | 13298.07 |    16384 |         270336 |      256 |            16 |                 8 |
| `ElementShape/Permuted/S64/U16`              |  5627.02 |   5636.55 |   5648.46 |  5653.10 |        18.93 |  0.33% |  5693.16 |    16384 |          40960 |      256 |             2 |                 2 |
| `ElementShape/Permuted/S64/U32`              |  5635.81 |   5642.35 |   5649.03 |  5765.96 |       284.14 |  4.93% |  6502.39 |    16384 |          73728 |      256 |             4 |                 4 |
| `ElementShape/Permuted/S64/U64`              | 10398.07 |  10401.37 |  10424.80 | 10890.56 |      1401.04 | 12.86% | 14626.12 |    16384 |         139264 |      256 |             8 |                 8 |
| `ElementShape/Permuted/S64/U8`               |  7746.85 |   7750.79 |   7763.71 |  7771.06 |        30.58 |  0.39% |  7848.26 |    16384 |          24576 |      256 |             1 |                 1 |
| `ElementShape/Sequential/S1024/Aligned64`    | 11782.56 |  11820.82 |  11888.72 | 11880.60 |        57.76 |  0.49% | 11966.99 |    16384 |        1049600 |       16 |            64 |                64 |
| `ElementShape/Sequential/S1024/Blob16`       |  8172.97 |   8179.60 |   8194.38 |  8564.23 |      1119.74 | 13.07% | 11550.05 |    16384 |         262656 |       16 |            16 |                 8 |
| `ElementShape/Sequential/S1024/Blob256`      | 26467.59 |  26547.90 |  27193.07 | 27008.95 |       351.39 |  1.30% | 27310.97 |    16384 |        4194816 |       16 |           256 |                 8 |
| `ElementShape/Sequential/S1024/Blob64`       | 11800.79 |  11839.91 |  11895.34 | 13026.03 |      2264.43 | 17.38% | 17028.62 |    16384 |        1049088 |       16 |            64 |                 8 |
| `ElementShape/Sequential/S1024/String`       |  9348.81 |   9357.48 |   9377.17 |  9588.20 |       594.86 |  6.20% | 11169.36 |    16384 |         393728 |       16 |            24 |                 8 |
| `ElementShape/Sequential/S1024/StringRecord` |  8553.60 |   8564.63 |   8577.03 |  8591.13 |        35.24 |  0.41% |  8659.54 |    16384 |         262656 |       16 |            16 |                 8 |
| `ElementShape/Sequential/S1024/U16`          |  4525.79 |   4527.29 |   4531.72 |  4850.17 |       952.54 | 19.64% |  7390.22 |    16384 |          33280 |       16 |             2 |                 2 |
| `ElementShape/Sequential/S1024/U32`          |  4353.91 |   4361.39 |   4373.97 |  4371.30 |         8.96 |  0.20% |  4380.05 |    16384 |          66048 |       16 |             4 |                 4 |
| `ElementShape/Sequential/S1024/U64`          | 10165.15 |  10178.27 |  10204.15 | 10328.18 |       376.05 |  3.64% | 11328.34 |    16384 |         131584 |       16 |             8 |                 8 |
| `ElementShape/Sequential/S1024/U8`           |  7859.78 |   7863.13 |   7872.10 |  7871.07 |         8.61 |  0.11% |  7888.87 |    16384 |          16896 |       16 |             1 |                 1 |
| `ElementShape/Sequential/S256/Aligned64`     | 11600.02 |  11622.30 |  11645.81 | 12225.79 |      1709.83 | 13.99% | 16783.76 |    16384 |        1052672 |       64 |            64 |                64 |
| `ElementShape/Sequential/S256/Blob16`        |  7047.05 |   7069.41 |   7099.35 |  7468.44 |       865.94 | 11.59% |  9665.33 |    16384 |         264192 |       64 |            16 |                 8 |
| `ElementShape/Sequential/S256/Blob256`       | 26656.45 |  26687.09 |  26833.87 | 26864.44 |       177.43 |  0.66% | 27146.27 |    16384 |        4196352 |       64 |           256 |                 8 |
| `ElementShape/Sequential/S256/Blob64`        | 11613.86 |  11622.72 |  11665.04 | 11685.87 |        70.54 |  0.60% | 11798.60 |    16384 |        1050624 |       64 |            64 |                 8 |
| `ElementShape/Sequential/S256/String`        |  9228.65 |   9239.47 |   9262.61 |  9641.53 |       854.44 |  8.86% | 11738.22 |    16384 |         395264 |       64 |            24 |                 8 |
| `ElementShape/Sequential/S256/StringRecord`  | 10104.61 |  10131.40 |  10216.46 | 10201.81 |        59.27 |  0.58% | 10285.27 |    16384 |         264192 |       64 |            16 |                 8 |
| `ElementShape/Sequential/S256/U16`           |  4415.76 |   4420.02 |   4430.27 |  4429.52 |         9.76 |  0.22% |  4447.07 |    16384 |          34816 |       64 |             2 |                 2 |
| `ElementShape/Sequential/S256/U32`           |  4318.18 |   4321.03 |   4332.76 |  4497.21 |       500.53 | 11.13% |  5831.76 |    16384 |          67584 |       64 |             4 |                 4 |
| `ElementShape/Sequential/S256/U64`           |  7094.75 |   7099.12 |   7103.20 |  7110.89 |        15.18 |  0.21% |  7141.14 |    16384 |         133120 |       64 |             8 |                 8 |
| `ElementShape/Sequential/S256/U8`            |  6697.15 |   6703.58 |   6713.06 |  6717.10 |        14.51 |  0.22% |  6745.12 |    16384 |          18432 |       64 |             1 |                 1 |
| `ElementShape/Sequential/S64/Aligned64`      | 12115.37 |  12144.18 |  12188.23 | 12566.33 |      1152.18 |  9.17% | 15637.27 |    16384 |        1064960 |      256 |            64 |                64 |
| `ElementShape/Sequential/S64/Blob16`         |  8174.59 |   8175.36 |   8180.94 |  9638.46 |      4374.65 | 45.39% | 21304.20 |    16384 |         270336 |      256 |            16 |                 8 |
| `ElementShape/Sequential/S64/Blob256`        | 26843.41 |  27115.69 |  27303.63 | 28093.36 |      2482.97 |  8.84% | 34698.30 |    16384 |        4202496 |      256 |           256 |                 8 |
| `ElementShape/Sequential/S64/Blob64`         | 12107.17 |  12119.38 |  12152.31 | 12160.93 |        45.22 |  0.37% | 12251.52 |    16384 |        1056768 |      256 |            64 |                 8 |
| `ElementShape/Sequential/S64/String`         |  9503.64 |   9513.05 |   9567.08 |  9829.92 |       780.37 |  7.94% | 11903.78 |    16384 |         401408 |      256 |            24 |                 8 |
| `ElementShape/Sequential/S64/StringRecord`   |  8515.93 |   8528.39 |   8550.29 |  8565.73 |        50.99 |  0.60% |  8687.15 |    16384 |         270336 |      256 |            16 |                 8 |
| `ElementShape/Sequential/S64/U16`            |  4340.82 |   4343.09 |   4347.75 |  4599.35 |       755.65 | 16.43% |  6614.39 |    16384 |          40960 |      256 |             2 |                 2 |
| `ElementShape/Sequential/S64/U32`            |  4328.17 |   4329.18 |   4341.08 |  4347.27 |        20.83 |  0.48% |  4386.73 |    16384 |          73728 |      256 |             4 |                 4 |
| `ElementShape/Sequential/S64/U64`            |  8130.56 |   8134.07 |   8154.49 |  8154.50 |        23.68 |  0.29% |  8210.06 |    16384 |         139264 |      256 |             8 |                 8 |
| `ElementShape/Sequential/S64/U8`             |  7807.51 |   7810.73 |   7823.43 |  7821.51 |        10.55 |  0.13% |  7837.61 |    16384 |          24576 |      256 |             1 |                 1 |

Exactly 15 of 60 element-shape families exceed 10% CV. The largest are
`Sequential/S64/Blob16` at 45.39%, `Permuted/S256/Blob64` at 31.94%, and
`Permuted/S256/StringRecord` at 24.60%. No memory counter changes across repetitions, but the
timing dispersion makes these families diagnostic only and unsuitable for fine ranking.

The envelope's UTC timestamps span 1,592.63 seconds while its recorded monotonic benchmark
duration is 922.36 seconds, an unexplained difference of about 670 seconds. No cause is asserted.
The recorded host load is sampled at artifact completion, not benchmark startup. These limitations
reinforce that a quiet rerun, especially on Zen 5, is required before selection decisions.

## Lifecycle matrix

Artifact: [`macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence-lifecycle.json`](macos-arm64-apple-m5-pro_clang-22_109bc05353480985c1d1cc6365f078ec818e18f3_segmented-sequence-lifecycle.json)

| Family                          |   Min ns | Fast 3 ns | Median ns |  Mean ns | Sample SD ns |    CV |   Max ns |
| ------------------------------- | -------: | --------: | --------: | -------: | -----------: | ----: | -------: |
| `Lifecycle/ClearRegrow/S256`    | 44995.16 |  46659.93 |  47744.24 | 47493.65 |       980.37 | 2.06% | 48480.56 |
| `Lifecycle/ClearRegrow/S64`     | 44756.60 |  46767.79 |  47867.27 | 47579.47 |      1073.90 | 2.26% | 48278.54 |
| `Lifecycle/ReleaseRegrow/S256`  | 14944.05 |  14979.22 |  15069.51 | 15077.60 |       101.09 | 0.67% | 15270.21 |
| `Lifecycle/ReleaseRegrow/S64`   | 23375.80 |  23485.75 |  23688.81 | 23676.94 |       191.18 | 0.81% | 24035.99 |
| `Lifecycle/Retained/S256/16384` | 81911.35 |  82005.71 |  82118.29 | 82295.24 |       372.78 | 0.45% | 82992.96 |
| `Lifecycle/Retained/S256/256`   |   797.53 |    809.75 |    819.07 |   817.88 |         8.88 | 1.09% |   828.94 |
| `Lifecycle/Retained/S256/4096`  | 20373.21 |  20449.73 |  20547.25 | 20539.01 |        88.64 | 0.43% | 20663.05 |
| `Lifecycle/Retained/S64/16384`  | 75264.51 |  80154.69 |  82834.99 | 82053.47 |      2556.44 | 3.12% | 83196.25 |
| `Lifecycle/Retained/S64/4096`   | 18333.77 |  19819.81 |  20627.68 | 20380.04 |       769.12 | 3.77% | 20710.50 |
| `Lifecycle/Retained/S64/64`     |   203.19 |    207.41 |    210.12 |   209.38 |         2.36 | 1.13% |   210.71 |
| `Lifecycle/Trimmed/S256/16384`  | 79714.99 |  80716.76 |  87456.22 | 84667.05 |      3552.98 | 4.20% | 87855.33 |
| `Lifecycle/Trimmed/S256/256`    |   851.18 |    853.79 |    857.29 |   856.80 |         2.68 | 0.31% |   859.44 |
| `Lifecycle/Trimmed/S256/4096`   | 20155.05 |  21133.50 |  21714.39 | 21536.15 |       520.93 | 2.42% | 21761.28 |
| `Lifecycle/Trimmed/S64/16384`   | 96966.15 |  97029.03 |  97173.06 | 97244.11 |       233.42 | 0.24% | 97624.13 |
| `Lifecycle/Trimmed/S64/4096`    | 22457.02 |  23558.05 |  24146.71 | 24124.74 |       769.98 | 3.19% | 25513.28 |
| `Lifecycle/Trimmed/S64/64`      |   248.94 |    249.35 |    250.24 |   258.31 |        12.83 | 4.97% |   278.24 |

### Lifecycle state

| Family                          | Depth | Low capacity | Low reserved | Low segments | Restored capacity | Restored reserved | Restored segments |
| ------------------------------- | ----: | -----------: | -----------: | -----------: | ----------------: | ----------------: | ----------------: |
| `Lifecycle/ClearRegrow/S256`    |   n/a |          n/a |          n/a |          n/a |             16384 |            133120 |                64 |
| `Lifecycle/ClearRegrow/S64`     |   n/a |          n/a |          n/a |          n/a |             16384 |            139264 |               256 |
| `Lifecycle/ReleaseRegrow/S256`  |   n/a |          n/a |          n/a |          n/a |             16384 |            133120 |                64 |
| `Lifecycle/ReleaseRegrow/S64`   |   n/a |          n/a |          n/a |          n/a |             16384 |            139264 |               256 |
| `Lifecycle/Retained/S256/16384` | 16384 |        16384 |       133120 |           64 |             16384 |            133120 |                64 |
| `Lifecycle/Retained/S256/256`   |   256 |        16384 |       133120 |           64 |             16384 |            133120 |                64 |
| `Lifecycle/Retained/S256/4096`  |  4096 |        16384 |       133120 |           64 |             16384 |            133120 |                64 |
| `Lifecycle/Retained/S64/16384`  | 16384 |        16384 |       139264 |          256 |             16384 |            139264 |               256 |
| `Lifecycle/Retained/S64/4096`   |  4096 |        16384 |       139264 |          256 |             16384 |            139264 |               256 |
| `Lifecycle/Retained/S64/64`     |    64 |        16384 |       139264 |          256 |             16384 |            139264 |               256 |
| `Lifecycle/Trimmed/S256/16384`  | 16384 |            0 |            0 |            0 |             16384 |            133120 |                64 |
| `Lifecycle/Trimmed/S256/256`    |   256 |        16128 |       131040 |           63 |             16384 |            133120 |                64 |
| `Lifecycle/Trimmed/S256/4096`   |  4096 |        12288 |        99840 |           48 |             16384 |            133120 |                64 |
| `Lifecycle/Trimmed/S64/16384`   | 16384 |            0 |            0 |            0 |             16384 |            139264 |               256 |
| `Lifecycle/Trimmed/S64/4096`    |  4096 |        12288 |       104448 |          192 |             16384 |            139264 |               256 |
| `Lifecycle/Trimmed/S64/64`      |    64 |        16320 |       138720 |          255 |             16384 |            139264 |               256 |

### Lifecycle event counters per cycle

| Family                          | Source allocs | Source bytes | Source releases | Source released bytes | Directory allocs | Directory bytes | Directory frees | Directory freed bytes |
| ------------------------------- | ------------: | -----------: | --------------: | --------------------: | ---------------: | --------------: | --------------: | --------------------: |
| `Lifecycle/ClearRegrow/S256`    |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/ClearRegrow/S64`     |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/ReleaseRegrow/S256`  |            64 |       133120 |              64 |                133120 |                0 |               0 |               0 |                     0 |
| `Lifecycle/ReleaseRegrow/S64`   |           256 |       139264 |             256 |                139264 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S256/16384` |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S256/256`   |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S256/4096`  |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S64/16384`  |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S64/4096`   |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Retained/S64/64`     |             0 |            0 |               0 |                     0 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S256/16384`  |            64 |       133120 |              64 |                133120 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S256/256`    |             1 |         2080 |               1 |                  2080 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S256/4096`   |            16 |        33280 |              16 |                 33280 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S64/16384`   |           256 |       139264 |             256 |                139264 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S64/4096`    |            64 |        34816 |              64 |                 34816 |                0 |               0 |               0 |                     0 |
| `Lifecycle/Trimmed/S64/64`      |             1 |          544 |               1 |                   544 |                0 |               0 |               0 |                     0 |

All 16 lifecycle families have CV at or below 4.97%. `ReleaseRegrow/S256` has a 15,069.51 ns
median versus 47,744.24 ns for `ClearRegrow/S256`, but the code paths and compiler-visible work
are not equivalent enough to generalize that release beats reuse. Both use reserved unchecked
append and retain the directory; release reacquires 64 source blocks while clear reacquires none.
They also must not be compared as like-for-like with the production fresh case, which constructs
and destroys its directory and uses checked emplacement.

## Evidence status

This is valid, clean-tree Apple M5 Pro C++23 evidence for the merged fixed-segment
implementation. It is diagnostic rather than a cross-machine selection result. Matching AMD Zen 5
evidence remains pending and is required before a performance-sensitive representation or default
is selected. Schema validity establishes provenance and completeness; it does not by itself make a
noisy family reliable for ranking.
