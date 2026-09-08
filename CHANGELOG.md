# 0.15.1

- Restored C++20 as the project baseline while continuing to use newer standard-library features
  when they are available.

# 0.15.0

- Added constexpr aggregate field-name discovery on GCC 14, complementing the existing Clang
  backend while preserving mbo's per-type name overrides and per-field formatting, conversion,
  suppression, and redaction controls. Added a comprehensive `mbo/types` guide plus reflection
  implementation notes with the support matrix, limitations, and comparison with other libraries.
- Added reproducible ordered-container microbenchmarks for exact lookup and both bounds across
  container kinds, capacities, key positions, ascending/descending order, and comparator types.
- Restored the empty-input guards in log timing's reverse parsers, added direct regression tests,
  and stopped counting unreachable generated `index_of` expansions as uncovered lines.
- Raised the development compiler baseline to GCC 14 and Clang/LLVM 22, selected the newer language
  mode then used by development, and updated the default Bazel release to 9.2 while retaining Bazel
  8 compatibility.
- Switched the bundled LLVM setup from its temporary prerelease override to the published
  `toolchains_llvm@1.9.0` Bazel Central Registry module.
- Updated Abseil to 20260817.0 and the associated dependency selections and compatibility patches.
- Made the primary Clang toolchain authoritative for coverage while retaining GCC 14 and the
  supported Bazel/compiler combinations in the CI matrix.
- Fixed immutable GitHub releases by assembling standard releases as drafts until their assets
  are uploaded, updated BCR publication, and restored the reviewed post-release version-bump PR.

# 0.14.0

- Updated the bundled LLVM setup to the official `toolchains_llvm` 1.9.0 prerelease commit while
  its BCR publication is pending, removing the remaining `helly25/toolchains_llvm` fork dependency.
- Added live per-translation-unit clang-tidy progress with parallel worker counts, completion
  percentages, failure-only diagnostics, configurable concurrency, and complete interruption
  cleanup. A fail-closed clang-tidy database removes redundant fuzz-transition commands only when
  their semantic compiler arguments match the ordinary build.
- Transferred the project to `mboworks/mbo`, renamed the Bazel module to `mboworks_mbo`, and
  migrated shell tests to `mboworks_bashtest@0.6.1`. Historical `helly25_mbo` releases remain
  available unchanged.
- Extended SHGLOB braces with bounded ascending and descending integer and ASCII-letter sequences,
  including signed and zero-padded integers. Expansions above 10,000 terms are rejected, and the
  optional Bash increment form remains literal.
- Replaced the previous glob translation with the canonical fast GLOB/SHGLOB implementation. It
  provides locale-independent structural bracket parsing, positive and negated character classes,
  precise validation, nested SHGLOB alternatives, and complete-component `**`; notably,
  `foo/**/bar` matches `foo/bar` and any number of intervening directory levels.
- Added `mbo::StringOrView`, a read-only string-like value that either owns a `std::string` or
  borrows a `std::string_view`. It exposes the full applicable string/string-view interface,
  comparisons, `contains`/`subview`, stream and Abseil formatting, plus Abseil and
  `std::hash` integration.
- Fixed `OptionalDataOrRef` construction and assignment so borrowed values cannot silently become
  dangling references; added the corresponding lifetime constraints and tests.
- Fixed `Required` replacement and generic operations, including move-only values and converting
  construction/assignment, while preserving its configurable failure behavior.
- Fixed `NoDestruct` construction and its formatting/hash customizations for non-copyable and
  explicitly constructed values.
- Added strict INI parsing: malformed sections, missing keys, and invalid trailing content now
  return diagnostics instead of being partially accepted.
- Fixed JSON value-iterator assignment and made JSON's configurable requirement failures testable
  through the exception-enabled configuration without weakening the default fatal behavior.
- Fixed alphabetic hexadecimal escapes so the full accepted digit set is decoded correctly.
- Fixed independently quoted option parsing: quoting one option no longer changes how subsequent
  options are parsed.
- Rejected truncated and structurally malformed glob ranges rather than passing altered semantics
  to RE2.
- Made `NormalizePath` preserve the filesystem's native character type instead of assuming narrow
  characters.
- Made `GetContents` report seek failures and `GetMaxLines` report read failures instead of
  returning incomplete data as success.
- Corrected cache-cleanup prefix matching so one branch or configuration cannot select unrelated
  cache entries.
- Added fuzz targets for glob conversion, string parsing, INI parsing, diff inputs/options, and
  digest checksum parsing.
- Added LCOV line/function/branch coverage enforcement for the overall tree, logical modules, and
  changed code. Reports use a shared policy and baseline, publish durable per-PR HTML detail to
  GitHub Pages, and fail with actionable uncovered-line/branch diagnostics.
- Raised measured coverage across every core module through behavioral tests rather than production
  exclusions: overall coverage is 98.11% lines, 98.63% functions, and 89.32% branches; every module
  is rated `GOOD`, including `mbo/types` at 86.12% branch coverage.
- Bounded CI cache entries below 500 MB and made cache cleanup best-effort, so cleanup API failures
  cannot turn an otherwise successful job red.
- Enabled UndefinedBehaviorSanitizer explicitly in the AddressSanitizer configuration.
- Updated the compile-command extractor and reused the clang-tidy Bazel configuration, eliminating
  an avoidable configuration switch while keeping header-only changes in clang-tidy's scope.
- Adopted the shared contributor style guides and enforcement: matcher-based tests, named typed and
  parameterized cases, explicit test sizes, aligned Markdown tables, and pre-commit rejection of
  forbidden GoogleTest comparison macros and spelling errors in C++ and documentation.
- Added repository Git orchestration rules for dependency-graph-aware autonomous PR handling,
  synchronized-head validation, conflict resolution, and safe parent/child merge sequencing.
- Made pull-request validation run in pull-request context, cancel superseded runs, and retain the
  final aggregate `done` gate.
- Hardened release automation: release bumps retain required review protection, only the requested
  tag is published, and temporary Git indexes use a portable private directory.
- Documented the policy of using readable GitHub Actions version tags rather than commit hashes
  unless a concrete security or reproducibility reason requires pinning.
- Silenced external-header warnings in exec-configuration builds without relaxing warnings for
  first-party code.
- Fixed `LimitedVector`'s comparison operators, which were declared over `std::size_t` capacities while the class takes `auto`: instances spelled via `MakeLimitedVector`/`LimitedOptions` or an `int` literal matched no operator at all and failed to compile.
- Enabled `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`: unchecked `operator[]` is now a clang-tidy error. Containers that bounds-check themselves (`LimitedMap`, `LimitedVector`, `Json`) and insert-semantics maps (absl's) are excluded by class; the hash/digest kernels and other in-range-by-construction code carry scoped `NOLINT` blocks.
- Fixed `LimitedVector` comparison operators (`==`, `<=>`, `<`), which looped to `min` of the CAPACITIES instead of the sizes: comparing partially-filled vectors read uninitialized slots (or threw in a require-throws build). Found by `pro-bounds-avoid-unchecked-container-access`.
- Converted unchecked `operator[]` to `.at()`/`std::get` (or a reasoned `NOLINT` in benchmarks' timed loops and compile-time code) across the hash, digest, json, types and container libraries.
- Converted unchecked `operator[]` to `.front()`/`.at()` (or a reasoned `NOLINT` on hot paths) across the diff, strings, log, mope, file and testing libraries, preparing `pro-bounds-avoid-unchecked-container-access`.
- Re-measured `pro-bounds-avoid-unchecked-container-access` (task: attempt to enable): 2,953 findings, ~2,500 in hash/digest kernels where indices are in-range by construction; stays disabled with the data recorded in `.clang-tidy`.
- Enabled `-Wpedantic` (as errors) on all first-party code. Two named carve-outs for absl macros expanding in our translation units (`ABSL_FLAG`'s `__COUNTER__`, `ABSL_CHECK`'s `_Nullable`); the repo's own deliberate GNU extensions (`__int128`, statement expressions in `decompose_count.h`) are annotated `__extension__` in code. The bazel-7/8 compat CI rungs relax it because those bazels half-apply `external_include_paths` and leak external-header findings.
- Removed the temporary `hash_tembo.h` allowlist entry from the headers-claimed check; #311 lists the header in a target again.
- The warning set is now owned by the repo: explicit `-Wall -Wextra` (as errors) on all first-party code, with `missing-field-initializers` suppressed as the deliberate designated-init idiom; previously only `-Werror` was ours and the set came from each toolchain's defaults.
- CI no longer caches the hermetic LLVM (ported from helly25/xff #409+#411): jobs cache bazel's disk cache instead of the output root, non-clang cells keep a small repo cache, and the symbolizer wrapper resolves `llvm-symbolizer` via runfiles so a fresh LLVM fetch location cannot break asan/tsan.
- Added a `headers-claimed` pre-commit check: every tracked header must be listed in its package's BUILD file. Found `hash_tembo.h` orphaned since #306 on its first run (temporarily allowlisted; re-listed by #311).
- Replaced the reachability-based `library-test-coverage` hook with xff's strict rule: every `cc_library` needs a test in its own package with a direct dependency; exceptions live in the tool's `_ALLOWLIST` with reasons.
- Converted assert-status-then-dereference in the new tests to `IsOkAndHolds`/`MBO_ASSERT_OK_AND_ASSIGN`, and imported four rules from helly25/xff's style guide (`_cc` naming, per-package tests, no braced-init-list iteration, `lhs`/`rhs` comparator parameters).
- Added tests for `hash_extra_cc`, `hash_internal_util_cc`, `mope_cc`, `ini_cc`, `runfiles_dir_cc`, `extend_cc`, `extender_cc` and `test_types_cc` - every library now has a test in its own package.
- Added tests for all ten diff libraries: the three algorithms (`diff_direct_cc`, `diff_myers_cc`, `diff_naive_cc`), the shared plumbing (`base_diff_cc`, `chunked_diff_cc`) and the internals (`chunk_cc`, `context_cc`, `data_cc`, `output_cc`, `update_absl_log_flags_cc`).
- Removed every `-stdlib=libc++` and every `-Wno-unused-command-line-argument` suppression: the hermetic toolchain resolves libc++ headers via `-cxx-isystem` and links libc++ explicitly, so the flag was inert (driver-confirmed) at compile and link alike; the one formerly load-bearing upstream copy is fixed in the official `toolchains_llvm` 1.9.0 prerelease selected by `git_override` until BCR publication.
- Removed the redundant `--cxxopt=-stdlib=libc++` from the `clang-tidy` bazel config; the hermetic toolchain already passes it, so it appeared twice and produced "unused argument" warnings.
- Fixed `mbo::file::GetMTime` on libstdc++: it read `std::filesystem::file_clock` time as Unix seconds, but that clock's epoch is implementation-defined (2174-01-01 on libstdc++), so every file's mtime was ~148 years off on Linux.
- Added `mbo/diff:diff_options_test`, covering every flag parser including the unknown-value paths.
- Added `mbo/file:artefact_test`.
- Added tests for `container_proxy_cc`, `cases_cc`, `traits_cc` (internal), `config_cc` and `require_cc`.
- Fixed `mbo::types::TypedView` to derive from `std::ranges::view_interface` PUBLICLY; private inheritance made every member it supplies inaccessible.
- Added `mbo/types:typed_view_test`, and normalised colon-less deps in BUILD files.
- Fixed `mbo::types::IsVariantMemberType`, which was always `false`: its recursion specialised the out-of-range case, so an in-range index fell through to the `false_type` primary template.
- Added `mbo/types:variant_test` and `mbo/log:demangle_test`.
- Added `mbo/types:compare_test`, covering `CompareLess`, `CompareFloat`'s NaN total order and `CompareScalar`'s mixed-signedness branches.
- Fixed `//mbo/types:compare_cc` to declare its dependency on `traits_cc`, which it includes but never listed.
- Added a `cc-target-naming` pre-commit check (ported from helly25/xff): `cc_library` target names must end in `_cc`. Renamed `hash_internal_util` and `hash_test_util` accordingly.
- `mbo::types::CompareLess` now declares `is_transparent`, so its existing heterogeneous overloads are reachable through `LimitedSet`/`LimitedMap` instead of every lookup having to build a key first.
- Extended transparent lookup to every read operation: `LimitedMap::at`, `contains_all`/`contains_any` (which previously rejected foreign keys outright), and `index_of`, so a foreign key now takes the same unrolled fast path as an exact key instead of falling back to a binary search.
- `LimitedSet`/`LimitedMap` `erase` no longer constructs a key when the comparator is transparent, and its template overload is now constrained to foreign keys only.
- Added transparent (heterogeneous) lookup to `LimitedSet`/`LimitedMap`: when the comparator declares `is_transparent`, `find`, `contains`, `count`, `index_of`, `lower_bound`, `upper_bound` and `equal_range` accept any type the comparator can order against the key, without constructing a key.
- Fixed `LimitedOrdered::count` which returned `last - false` instead of `last - first` and did not compile at all (no test instantiated it).
- Added a `library-test-coverage` pre-commit check: every `cc_library` under `//mbo/...` must be reachable from some test target.
- Added `diff-charts` sub-command for the `hash_benchmark_report.py` tool.
- Added `bundle-context` sub-command for the `hash_benchmark_report.py` tool.
- Updated `mumbo` to V5 which aims at improved Instruction-level parallelism (ILP).
- Improved publish for `hash_benchmark_report.py` which updates README.md with the latest benchmark results, charts and quality tables.
- Added `--cwd` param to `mbo/digest` tool.
- Added the public `mbo::log::ScopedStream` helpers. The accompanying `MBO_LOG_CHECK` experiment
  remains package-private and is not a supported API.
- Moved `clang-tidy` from `trunk` to the opt-in `clang-tidy` pre-commit hook (`tools/clang_tidy.sh`).
- Fixed `.clang-tidy`: `WarningsAsErrors` never escalated findings, and a misspelled option key was dead.
- Disabled `llvm-header-guard` and `llvm-prefer-static-over-anonymous-namespace`, which contradict `STYLE_CPP.md`.
- Fixed `compile_commands-update.sh` to record the hermetic clang, so clangd and clang-tidy parse what `--config=clang` builds.
- Dropped 196 duplicate exec-configuration compile commands via the extractor's new `--bcce-prefer-target-config`.
- Fixed `IniFile::SetKey` to move its `std::string&&` instead of copying it through `assign`.
- Removed a pointless `std::floor` over integer division in `mbo/strings/numbers.h`, which round-tripped a `size_t` through `double`.
- Fixed `mbo::types::tstring::is` to take its ignored argument by const reference rather than by value.
- Promoted `clang-tidy` to an enforcing pre-commit gate: it now runs automatically on the C++ sources a commit touches, and any finding fails the commit.
- Exempted the standard-mandated container member type names (`value_type`, `size_type`, ...) from `readability-identifier-naming`, which demanded CamelCase the standard does not permit.
- Disabled `misc-use-internal-linkage` (its only fix is `static`, while `STYLE_CPP.md` wants an anonymous namespace) and `readability-redundant-parentheses` (its fix deletes the `__builtin_dump_struct` callee).

# 0.13.2

- Added `BmHash128Throughput` (128-bit bounded-range throughput) and guarded the 64-/128-bit benchmark paths by `HasGetHash64`/`HasGetHash128`.
- Reworked the measurement report around the latency (ns at exact length) / throughput (GiB/s over the Short/Web bounded distributions) split: each renders as chart + table per hash width, absent sections dropped; the report re-distills a bundle from its raw on read, so old bundles still yield latency tables and bundles need no re-packing when the tool changes.
- Reworked the hash mixed-length benchmark: dropped the hash-indexed key walk that collapsed into a rho-cycle (near-zero anomalies) and added `BmHash64Throughput` reporting bytes/s over two documented length distributions (Short ≤128 B, Web ≤4096 B) truncated to each upper bound; the per-exact-length `BmHash64`/`BmHash128` remain the latency view. Distributions are exported as the `throughput_dists` context.
- Added a `compare` command reporting per-case Δ% and a geomean between two datasets.
- Made `tables`/`plot`/`compare`/`quality` accept a bundle `.tgz` or a results JSON, positionally or via `--results`/`--bundle`.
- Added `plot --kind` (throughput/latency/all) and `--scale` (log-log/linear-log).
- Expanded the benchmark `kFullSizes` and swept latency over the full set (dense ns-vs-length latency curve).
- Added a `quality` command generating the overview + SMHasher3 Results tables from `hash_algorithms.json` and a measured bundle; `--check` gates them.
- Added a `consistency` command verifying same-source-SHA bundles agree on SMHasher3 verdicts.
- Fixed SMHasher3 parsing: a bad name or crash reads as `ERROR` not a false `PASS`; score and failing families parse from the Summary; legacy names aliased.
- Integrated `mbo/hash/measurements` as a normal dev package (still release-stripped) with a `quality_sh_test` gating the tables; test CI fetches the LFS bundles.
- Added a `no-deps-on-measurements` pre-commit guard.
- Ported `mumbo` (64-bit) to Starlark (`hash.mumbo`), byte-identical to C++; `hash.bzl` now offers `mumbo`, `dumbo`, and `fnv1a`.

# 0.13.1

- Switched the build-seed mangle constant header to per-build generation (no longer committed): removed `internal/hash_mangle_seed.h.in` and `hash_mangle_seed_default_test`, so a version bump no longer needs a committed regeneration. `hash_mangle.h` includes the generated header directly (missing is an `#error`; clangd falls back under `-DIS_CLANGD`).
- Added Starlark ports of the `dumbo` and `fnv1a` hashes in `//mbo/hash:hash.bzl` (the public `hash` struct), byte-identical to C++ and verified against it.
- Switched the mangle version/seed fold from FNV-1a to the in-house `dumbo` hash.
- Added `//mbo/hash:hash_tool`, a minimal `<algo> [<data>]` hash CLI (plain `GetHash64`, no mangle).
- Excluded dev-only files from the release archive (`mbo/hash/measurements`, `.trunk`, and git metadata).

# 0.13.0

- Added `DiffOptions::ignore_missing_final_newline` (the `--ignore_missing_final_newline` flag and the `diff_test` attribute): a file with and one without a trailing newline compare equal.
- Made an empty `DiffOptions::time_format` omit the file-header timestamp (git-style `--- name` / `+++ name`), so output is reproducible across machines and time zones.
- Added a Myers diff algorithm (`DiffMyers`, `--algorithm=myers`) and made it the default: minimal diffs, >2x faster on scattered edits, and the disjoint-files worst case dropped from ~16s to <1ms.
- Sped up the `myers` tokenizer (`string_view` interning keys hashed with `mbo::hash::GetHash64`, single-fold `ignore_case`): ~16% faster on tokenize-heavy inputs.
- Made the `mbo::diff` binary follow the POSIX exit-code contract (0 equal, 1 different, 2 trouble) and gave `diff_test` a `minimal` attribute.
- Rounded out the `mbo::diff` CLI/test surface: `--minimal`, algorithm/format names in the usage message, `diff_test` `width`/`skip_left_deletions` attributes, and a CLI bashtest over the algorithm x format matrix.
- Renamed the previous default `mbo::diff` algorithm `unified` to `naive` (`DiffNaive`); `unified` stays a deprecated alias that now selects `myers`.
- Verified the `mbo::diff` feature/algorithm support matrix with tests across `naive`, `myers`, and `direct`.
- Added `mbo::diff` context (`diff -c`) and normal (plain `diff`) output formats (`--format=unified|context|normal`), byte-compatible with GNU diff and `patch`.
- Fixed `mbo::diff` unified output for empty ranges so chunk headers match GNU diff (and apply cleanly with `patch`).
- Split `mbo::diff` chunk rendering into `mbo/diff/internal/output.{h,cc}` (`AppendChunk`).
- Added `mbo::hash::GetHash64` / `GetHash128` and the `Hash128` type: constexpr-safe, non-cryptographic hashing with the in-house `mumbo` as default.
- Renamed the legacy `mbo::hash::simple` to `mbo::hash::dumbo` and redesigned it into a compact single-lane MUM hash (SMHasher3 PASS 188/188); removed the `simple` namespace outright (pre-1.0, no alias).
- Made all `mbo::hash` entry points templates over an algorithm struct (per-algorithm `Algorithm`, the `HasGetHash*`/`IsHashAlgorithm` concepts, and `Hasher<Algo>` fallbacks).
- Added constexpr-safe `fnv1a`, `xxh64`, `xxh3` (64-bit), and `murmur3` (64/128), all matching published reference values.
- Exposed `mbo::hash::Hash128To64(Hash128)` to fold a 128-bit hash into a mixed 64-bit one.
- Added the build-seed mangle entry point `hash_mangle.h` (`GetHash` / `MangledHasher`): XORs one build-selected constant (from the module version and the `--//mbo/hash:mangle_seed*` flags) into `GetHash64` values, so they deliberately do not compare across independently configured builds.
- Switched runtime loads to `memcpy` (~3x faster on gcc) with branch-lite overlapping tail reads; kept the byte-assembly path for constant evaluation.
- Made `Hasher<Algo>` a transparent functor, usable directly by `absl`/`std` hash containers with heterogeneous `string_view` lookup.
- Added `mbo::hash::CombineHashes` and `hash_internal::Mul128Fold64`.
- Added seed-bit avalanche and structured/sparse-key distinctness tests.
- Added constexpr-safe `xxh3::GetHash128` (`XXH3_128bits`), verified against reference vectors and libxxhash.
- Added constexpr-safe `rapidhash::GetHash64` (rapidhash V3) and `siphash::GetHash64` / `SipHash<C, D>`, verified against reference vectors.
- Added a differential test comparing `xxh64`/`xxh3` bit-for-bit against the reference library over randomized inputs.
- Added `mbo::hash::Hash64To32` (XOR-fold to 32 bits).
- Added `GetHash32<Algo>` / `Hasher<Algo>::GetHash32` and the `HasGetHash32` concept (native variant or synthesized fold).
- Added a mixed-length latency benchmark (`BmHash64Latency`).
- Added streaming hashing (the `HasStreaming` concept and `Streamer<Algo>`) for `mumbo`, `xxh64`, and `siphash`.
- Added a repository-root `NOTICE` reproducing the upstream notices of the transcribed algorithms.
- Added `mumbo`, the library's own MUM-based hash and the default behind `GetHash*`: SMHasher3 PASS 188/188 in both widths, with best-in-class small-key latency and bulk throughput.
- Split the NOTICE-bearing transcriptions into `//mbo/hash:hash_extra_cc` (`rapidhash`, `xxh64`/`xxh3`); the default `//mbo/hash:hash_cc` stays notice-free.
- Documented that hash values are not stable across library versions and are not for persistence or cryptographic use.
- Added the `mbo/digest` library: constexpr-safe message digests (SHA-2 family, SHA-3/SHAKE, BLAKE2b, BLAKE3, and legacy SHA-1/MD5), each pinned against reference vectors.
- Mirrored mbo/hash's plug-in architecture in `mbo/digest` (per-algorithm `Algorithm` structs, `Streamer`, `Hmac<Algo>`, `ToHexString`).
- Added the `digest` binary (`//mbo/digest:digest`): `sha256sum`-compatible output, `-a` selects any of the 17 algorithms, reads stdin, streaming reads.
- Added `--check` to the `digest` binary (coreutils-compatible verification with `--quiet`, `--status`, `--ignore_missing`, and `--strict`).
- Added the bazel `verify_digest_test` rule (`//mbo/digest:digest.bzl`): re-digests files against a saved sidecar or inline checksum so they only change when the digest is updated too.
- Factored the shared load primitives into `//mbo/hash:hash_internal_util` and added the big-endian `Load32BE` / `Load64BE` loads.

# 0.12.0

- Fixed visibility of already-documented APIs whose Bazel `cc_library` targets had defaulted to `//visibility:private` since they were added, so external code could not `deps` them. Now `//visibility:public`: `//mbo/types:optional_ref_cc` (`OptionalRef`), `//mbo/types:optional_data_or_ref_cc` (`OptionalDataOrRef` / `OptionalDataOrConstRef`), and the `//mbo/json:json` / `:json_cc` library - all added in 0.10.0.
- Made `//mbo/types:stringify_ostream_cc` public (`stringify_ostream.h`): the ostream `<<` integration for `Stringify` / `Extend` types plus the `SetStringifyOstreamOutputMode` / `SetStringifyOstreamOptions` configuration functions.
- Renamed the module repo to `helly25_mbo` and dropped the `com_helly25_mbo` alias (`repo_name`). The last internal user (the `ini_file` golden test) now resolves its runfiles via the repo-relative `//mbo/file/ini:tests/test.ini`, so nothing depends on the old name. Consumers referencing `@com_helly25_mbo//...` must switch to `@helly25_mbo//...`.
- Bumped `helly25_bashtest` 0.3.1 -> 0.4.0 and dropped its `com_helly25_bashtest` repo alias (a WORKSPACE-era compatibility name); BUILD files now `load("@helly25_bashtest//bashtest:bashtest.bzl", ...)`. mbo is bzlmod-only (`--noenable_workspace`, no `WORKSPACE` file).
- Bumped `rules_cc` 0.2.19 -> 0.2.20.

# 0.11.1

- Release/packaging fixes (no API changes): self-contained release archives; hardened `trigger_release.sh`.

# 0.11.0

- Added `mbo::testing::IsElementOf(container)` - element-on-the-left orientation of gmock's `Contains`. Iterates with raw `operator==` (with a `std::pair` component-wise fallback) so heterogeneous subject types (string literals, `std::string_view`, etc.) compile without caller-side conversion, on both libc++ and libstdc++.
- Added `mbo::testing::IsKeyOf(map)` - convenience matcher for "is this value among the keys of the map?". Failure messages report `"is a key of {…}"`.
- Added `mbo::testing::IsValueOf(map)` - parallel to `IsKeyOf` for mapped values. Failure messages report `"is a value of {…}"`.
- Bumped Bazel to 9.1.1 (`.bazelversion`).
- Updated module dependencies: `bazel_skylib` 1.8.2 → 1.9.0, `platforms` 1.0.0 → 1.1.0, `rules_cc` 0.2.14 → 0.2.21, `rules_shell` 0.6.1 → 0.8.0, `re2` 2025-11-05 → 2025-11-05.bcr.1, `googletest` 1.17.0 → 1.17.0.bcr.2, `google_benchmark` 1.9.4 → 1.9.5, `helly25_bashtest` 0.1.0 → 0.5.0, `toolchains_llvm` 1.5.0 → 1.7.0, `depend_on_what_you_use` 0.7.0 → 0.16.0, and the `hedron_compile_commands` pin.
- `helly25_bashtest` 0.3.0 loads the `sh_*` rules from `@rules_shell` directly, so Bazel 9 (which no longer autoloads the legacy native `sh_*` rules) needs no `--incompatible_autoload_externally` workaround.
- Bumped `abseil-cpp` 20250814.1 → 20250814.2. Held at the 20250814 series because newer Abseil (20260107+) deprecates the `absl::MutexLock(Mutex*)` constructor that `re2` 2025-11-05 still uses, which fails under `--cxxopt=-Werror`.
- Bumped the pinned LLVM toolchain 17.0.4 → 20.1.8. LLVM 17.0.4's bundled linker segfaults against the macOS 26 SDK when building tools in the exec configuration (e.g. `//mbo/diff:diff`, which `_diff_test` builds with `cfg=exec`); 20.1.x links cleanly and compiles the tree without new `-Werror` warnings.
- Refreshed the newest rung of the CI clang matrix 22.1.7 → 22.1.8 (the default pin stays at the 20.1.8 minimum baseline; 21.1.8 unchanged).
- Migrated the `_diff_test` `is_windows` select off the deprecated `@bazel_tools//src/conditions:host_windows` to `@platforms//os:windows`.
- Fixed compilation under clang 21 (e.g. Apple clang on macOS 26): its tightened constexpr rules reject placement-`construct_at` into a const-qualified object, so `struct_names_clang.h` and `limited_vector.h` now store the non-const value type.

# 0.10.0

- Bumped minimum GCC to 13.
- Added `mbo/json` which adds JSon write support for almost any structured type. If anything is missing it can easily be added on the client side.
- Added direct support for `-fno-exceptions` irrespective of config setting.
- Added support for ASAN symbolizer with `--config=clang`.
- Added AbslStringify and hash support to `NoDestruct`, `RefWrap`, `Required`.
- Added struct `OptionalDataOrRef` similar to `std::optional` but can hold `std::nullopt`, a type `T` or a reference `T&`/`const T&`.
- Added struct `OptionalDataOrConstRef` similar to `std::optional` but can hold `std::nullopt`, a type `T` or a const reference `const T&`.
- Added struct `OptionalRef` similar to `std::optional` but can hold `std::nullopt` or a reference `T&`/`const T&`.
- Improved `Stringify` (breaking change):
  - Improved control for `Stringify` with Json output.
  - Updated `StringifyOptions` (breaking change).
  - Updated `StringifyWithFieldNames` (breaking change).
  - Moved `StringifyOptions` factories to `Stringify` so they can be constexpr even in C++20.
  - Modified `MboTypesStringifyOptions` (breaking change) to be more flexible - but users must deal with the change.
  - Added ability to detect bad `MboTypesStringifyOptions` signatures.
  - Added `StringifyFieldOptions` which holds outer and inner `StringifyOptions`.
  - Added `Stringify::AsJsonPretty()` and `StringifyOptions::AsJsonPretty()`.
  - Added API extension point functoin `MboTypesStringifyValueAccess` which allows to replace a struct with a single value in `Stringify` processing.
  - Added `Stringify` support for empty types and types that match `IsStringKeyedContainer`.
  - Fixed `Stringify` null\* representations to stream as `'null'` as opposed to `0`.
  - Added ability to sort string keyed containers.
  - Added function `SetStringifyOstreamOutputMode` which sets global Stringify stream options by mode.
  - Added function `SetStringifyOstreamOptions` which sets global Stringify stream options.
  - Changed `Stringify::ToString` and `Stringify::Stream` to not depend on any mutable member of `Stringify`.
- Improved traits:
  - Added concept `ConstructibleFrom` implements a variant of `std::constructible_from` that works around its limitations to deal with array args.
  - Added concept `IsEmptyType` determines whether a type is empty (calls `std::is_empty_v`).
  - Fixed concept `IsAggregate` (breaking change).
  - Added concept `IsStringKeyedContainer` which determines whether a type is a container whose elements are pairs and whose keys are convertible to a std::string_view.
  - Added concept `IsReferenceWrapper` determines whether a type is a `std::reference_wrapper`.
  - Added concept `IsSameAsAnyOf` which determines whether a type is the same as one of a list of types. Similar to `IsSameAsAnyOfRaw` but using exact types.
  - Added template struct `TypedView` a wrapper for STL views that provides type definitions, most importantly `value_type`. That allows such views to be used with GoogleTest container matchers.
  - Added concept `IsOptionalDataOrRef` which determines whether a type is a `OptionalDataOrRef`.
  - Added concept `IsOptionalRef` which determines whether a type is a `OptionalRef`.
  - Added function `CompareArithmetic` which compares two values that are scalar-numbers (including foat/double, excluding pointers and references).
  - Added function `CompareIntegral` which compares two values that are integral-numbers (no float/double, no pointers, no references).
  - Added function `CompareScalar` which compares two values that are scalar-numbers (including float/double and pointers, excluding references).
  - Added concept `IsArithmetic` uses `std::is_arithmetic_v<T>`.
  - Added concept `IsIntegral` uses `std::integral<T>`.
  - Added concept `IsScalar` uses `std::is_scalar_v<T>`.
  - Added concept `IsFloatingPoint` determines whether a type is a floating point type (uses `std::floating_point`).
  - Added concept `ThreeWayComparableTo` which is similar to `std::three_way_comparable_with` but we only verify that `L <=> R` can be interpreted as `Cat` in the presented argument order.
- Added `Demangle` to log de-mangled typeid names.
- Added struct `Overloaded` which implements an Overload handler for `std::visit(std::variant<...>)` and `std::variant::visit` (technically moved).
- Added function `CompareFloat` which can compare two `float`, `double` or `long double` values returning `std::strong_ordering`.
- Added function `WeakToStrong` which converts a `std::weak_ordering` to a `std::strong_ordering`.
- Fixed some IWYU pragmas.
- Fixed some template requirements.
- Fixed bug in `mbo::strings::ParseString` with access past end of `string_view` for illegal hex and oct escape sequences (introduced in 0.9.0).
- Updated various dependencies.
- Renamed old dependency naming styles to new (matching bzlmod and other deps' needs) styles where possible.
- Removed WORKSPACE support.

# 0.9.0

- Added gmomck-matcher `mbo::testing::EqualsText` which compares text using line by line unified text diff.
- Added gmock-matcher-modifier `mbo::testing::WithDropIndent` which modifies `EqualsText` so that `mbo::strings::DropIndent` will be applied to the expected text.

# 0.8.0

- Renamed diff tooling options `ignore_space_change` to `ignore_trailing_space.
- Renamed `lhs_regex_replace` and `rhs_regex_replace` to `regex_replace_lhs` and `regex_replace_rhs` respectively.
- Added `regex_replace_lhs` and `regex_replace_rhs` to bzl rules `//mbo/diff:diff_test` and `//mbo/diff/tests:diff_test_test`.
- Renamed `mbo::diff::UnifiedDiff` to `mbo::diff::Diff`.
- Implemented diff algorithm `kDirect` which performs a direct side by side comparison.
- Renamed `//mbo/diff/unified_diff(_main).cc/h` to `//mbo/diff/diff(_main).cc/h`.
- Renamed `//mbo/diff:unified_diff` to `//mbo/diff`.
- Renamed `unified` to `context` for flags and attributes.
- Reorganized files and rules in directory `mbo/diff`.

# 0.7.0

- Added caching for unified `mbo::diff::UnifiedDiff` algorithm.
- Added `--lhs_regex_replace` and `--rhs_regex_replace` flags to `//mbo/diff:unified_diff`.
- Dropped all `using std::size_t` declarations.

# 0.6.0

- Moved bashtest out into `@com_helly25_bashtest` and used it from there.
- Added dependency on `@com_helly25_bashtest`.
- Dropped all remaining bashtest components.
- Added concept `ConstructibleInto` determined whether one type can be constructed from another. Similar to `std::convertible_to` but with the argument order of `std::constructible_from`.

# 0.5.0

- Added CI config LLVM-20.1.0 for Linux-Arm64 and MacOs-Arm64 platforms.
- Applied DWYU cleanup.
- Added field `mbo::file::GlobOptions::recursive` to select between recursive and flat globbing.
- Added function `mbo::file::GlobSplit` that splits a pattern into root and pattern.
- Improved program `//mbo/file:glob` to automatically split a single arg pattern.
- Added API extension point function `MboTypesStringifyConvert(I, T, V)` which allows to control conversion based on field types via a static call to the owning type, receiving the field index, the object and the field value.
- Added (experimental, removed in 0.6.0, moved to @helly25/bashtest) sh_library bashtest which provides a test runner for complex shell tests involving golden files that provides built-in golden update functionality (see `(. mbo/testing/bashtest.sh --help)`).

# 0.4.4

- Added an opt-in newer-language configuration to bazelrc and CI testing.
- Updated code to comply with current clang-tidy warnings.
- Enabled Bazel layering_check.

# 0.4.3

- Address const-ness issues in constexpr functions found by Clang 20.1.0.
- Dropped space in front of string-literal notation found by Clang 20.1.0.

# 0.4.2

- Tweaked automated release tooling.
- Switched to matrix for merge testing that verifies various GCC and Clang setups.
- Fixed `mbo::types::ContainerProxy` for GCC opt builds (issue with aliasing interpretation).
- Added `mbo::strings::StripPrefix` and `mbo::strings::StripSuffix` for temp `std::string&&`.

# 0.4.1

- Added load statements for all cc_binary, cc_library, cc_test functions in all bazel files.
- Added `mbo::strings::ConsumePrefix` and `mbo::strings::ConsumeSuffix`.

# 0.4.0

- Added function `mbo::strings::BigNumber`: Convert integral number to string with thousands separator.
- Added function `mbo::strings::BigNumberLen`: Calculate required string length for `BigNumer`.
- Added glob functionality:
  - Added struct `mbo::file::Glob2Re2Options`: Control conversion of a [glob pattern](https://man7.org/linux/man-pages/man7/glob.7.html) into a [RE2 pattern](https://github.com/google/re2/wiki/Syntax).
  - Added struct `mbo::file::GlobEntry`: Stores data for a single globbed entry (file, dir, etc.).
  - Added struct `mbo::file::GlobOptions`: Options for functions `Glob2Re2` and `Glob2Re2Expression`.
  - Added enum `mbo::file::GlobEntryAction`: Allows GlobEntryFunc to control further glob progression.
  - Added type `mbo::file::GlobEntryFunc`: Callback for acceptable glob entries.
  - Added function `mbo::file::GlobRe2`: Performs recursive glob functionality using a RE2 pattern.
  - Added function `mbo::file::Glob`: Performs recursive glob functionality using a `fnmatch` style pattern.
  - Added program `glob`: A recursive glob, see `glob --help`.
- WORKSPACE support:
  - Added rules_python and brought back com_google_protobuf support.
- Fixed `RunfilesDir/OrDie` to use environment variable `TEST_WORKSPACE` if present.
- Fixed formatting issue with `mbo/types/internal/decompose_count.h`.
- Downgraded Clang from 19.1.7 to 19.1.6.
- Updated GitHub workflow to test both Bazel flavors.
- Fixed issue in with struct names generation when compiling with Clang in mode ASAN.
- Added ability for unified_diff tool flag `--strip_file_header_prefix` to accept re2 expressions.
- Fixed various issues for bazelmod based builds.
- Improved `mbo::testing::RunfilesDir/OrDie` to support a single param variant that understands bazel labels. Further add support for other repos than the current one by reading the repo mapping.
- Fixed `//mbo/file/ini:ini_file_test` to be able to pass when run as remote repository.
- Added struct `mbo::types::ContainerProxy` which allows to add container access to other types including smart pointers of containers.
- Added struct `mbo::types::OpaquePtr` an opaque alternative to `std::unique_ptr` which works with forward declared types.
- Added struct `mbo::types::OpaqueValue` an `OpaquePtr` with direct access, comparison and hashing which will not allow a nullptr.
- Added struct `mbo::types::OpaqueContainer` an `OpaqueValue` with direct container access.
- Changed pre-commit to use clang-format 19.1.6.
- Added ability to construct Extended types from conversions (`ConstructFromConversions`).
- Removed support for type marker `MboTypesExtendDoNotPrintFieldNames`.

# 0.3.0

- Added struct `mbo::strings::AtLast` which allows `absl::StrSplit' to split on the last occurrence of a separator.
- Added concept `mbo::types::IsSet`.
- Added concept `mbo::types::IsVector`.
- Fixed concept `mbo::types::IsPair` and `mbo::types::IsPairFirstString` to not remove cvref.
- Fixed function `mbo::file::SetContents` to explicitly use binary mode.
- Added bazelmod support.
- Updated Clang to 19.1.7 (17.0.4 still ok).
- Fixed error/exception handling in `LimitedOrdered/Map/Set` to respect the configuration.

# 0.2.35

- Added ability to retrieve field names for structs without default constructor (e.g. due to reference fields).

# 0.2.34

- Added `mbo::types::Required` which is similar to `RefWrap` but stores the actual type (and unlike `std::optional` cannot be reset).
- Added `mbo::testing::WhenTransformedBy` which allows to compare containers after transforming them.
- Added custom Bazel flag `--//mbo/config:require_throws` which controls whether `MBO_CONFIG_REQUIRE` throw exceptions or use crash logging (the default `False` or `0`). This mostly affects containers.
- Added custom Bazel flag `--//mbo/config:limited_ordered_max_unroll_capacity`. This was undocumented as `--//mbo/container:limited_ordered_max_unroll_capacity` until now (though listed in the changelog). It controls the maximum unroll size for LimitedOrdered/Map/Set.

# 0.2.33

- Improved `MBO_RETURN_IF_ERROR` to correctly accept `absl::StatusOr` expressions independently of their implementation.
- Added `mbo::status::StatusBuilder` which allows to modify the message of an `absl::Status`.
- Added `mbo::status::GetStatus` which allows to convert its argument to an `absl::Status` if supported.
- Added macro `MBO_MOVE_TO_OR_RETURN` which can assign to structured binadings and other complex types.
- Added macro `MBO_ASSERT_OK_AND_MOVE_TO` which can assign to structured binadings and other complex types.
- Added matcher `mbo::testing::StatusHasPayload` that matches presence of any, a specific payload url, or a payload url with specific content.
- Added matcher `mbo::testing::StatusPayloads` that matches against `Status`/`StatusOr<>` payload maps.

# 0.2.32

- Added support for smart pointer types in `Stringify`.
- Added support for std::optional in `Stringify`.
- Added builtin ability to suppress `nullptr` and `nullopt` field values (in particular for Json output).
- Added support for move only decomposing in `StructToTuple`.
- Added (extendable) concept `IsSmartPtr`.
- Added concept `IsOptional`.
- Added extension API type `MboTypesStringifyDisable` which can be used to suppress printing.

# 0.2.31

- Prevent clangd indexing issues with `DecomposeCount` implementation.

# 0.2.30

- Added class `Stringify` which can turn arbitrary structs into strings.
- Added formatting control for `AbslStringify` externder using struct `StringifyOptions`.
- Added API extension point type `MboTypesStringifySupport` which enables `Stringify` support even if not otherwise enabled (disables Abseil stringify support in `Stringify`).
- Added API extension point type `MboTypesStringifyDoNotPrintFieldNames` which if present disables field names in `Stringify`.
- Added `AbslStringify` ADL API extension entry points `MboTypesStringifyFieldNames` and `MboTypesStringifyOptions`.
- Added function `StringifyWithFieldNames` a format control adapter for `AbslStringify`.
- Added field name support for non literal types in Clang.
- Added numeric field name (aka key) support and enforced for Json output.
- Added rule `mbo/types:stringify_ostream_cc` and header `mbo/types/stringify_ostream.h` for automatic ostream support using `Stringify`.
- Added static constructors `Extend::ConstructFromTuple` and `Extend::ConstructFromArgs`.
- Fixed a bug where Mope did not correctly enable and disable sections.
- Added documentation and tests on how to use a mope's `--set` flag to enable/disable sections.
- Fixed a bug with `DecomposeCount`. It must never return 0 (an empty aggregate cannot be decomposed using structured bindings).
- Changed Clang to use more reliable overload sets (they cannot be used in GCC).

# 0.2.29

- Added crtp-struct `ExtendNoPrint` Like `Extend` but without `Printable` and `Streamable` extender functionality.
- Added support for type `char` in extender `AbslStringify`.
- Added `mbo::types::TupleCat` which concatenates tuple types.
- Fixed `//mbo/log:log_timing_test` for systems with limited `std::source_location` support (e.g. MacOS).

# 0.2.28

- Improved `LimitedVector::insert` to deal better with conversions and complex types.
- Improved `LimitedMap`, `LimitedOrdered`, `LimitedSet` and `LimitedVector` ability to handle conversions.
- Added diff tooling options `ignore_all_space` and `ignore_consecutive_space`.
- Changed diff tooling options `ignore_space_change` to only ignore trailing space to match `git diff`.
- Added `mbo::log::LogTiming` a simple timing logger.
- Pinned Bazel version to 7.2.1.
- Updated hedron compile commands to include a patch for pre-processed headers.

# 0.2.27

- Implemented `LimitedVector::insert`.
- Fixed a bug in `LimitedVector::assign`.
- Updated https://github.com/bazel-contrib/toolchains_llvm past version 1.0.0.
- Changed `LimitedMap` and `LimitedSet` to not verify whether input is sorted, if they use `kRequireSortedInput` and `NDEBUG` is defined.

# 0.2.26

- Fixed comparison of `Extend` types with other types. Requires the other type can be turned into a tuple.
- Fixed internal consistencies.
- Updated `mope` to allow comments by setting a section to nothing: `{{#section=}}...{{/section}}`.
- Added concept `IsTuple` which determines whether a type is a `std::tuple`.
- Added `mbo::hash::simple::GetHash(std::string_view)` which is constexpr safe. (Renamed to `mbo::hash::dumbo` in 0.13.0.)
- Added hash support to tstring.

# 0.2.25

- Added concept `mbo::types::IsVariant`.
- Added concept `mbo::types::HasVariantMember`.
- Fixed issue with `Extend` ability to handle struct names when compiled with Clang.
- Fixed issue with `Extend` handling move only types when used in decompose assignment.
- Added template struct `BinarySearch` implements templated binary search algorithm.
- Added template struct `LinearSearch` implements templated linear search algorithm.
- Added template struct `ReverseSearch` implements templated reverse linear search algorithm.
- Added template struct `MaxSearch` implements templated linear search for last match algorithm.

# 0.2.24

- Improved `Extend` support.
- Renamed `MboExtendDoNotPrintFieldNames` to `MboTypesStringifyDoNotPrintFieldNames` which is the logical naming that follows the internal structure.

# 0.2.23

- Added llvm/clang which can be triggered with `bazel ... --config=clang`.
- Shortened generated extender names drastically (< 1/3rd).
- Added union member identification (enables union members for `Extend`'s printing/streaming).
- Added ability to suppress field name support in `Extend` by adding `using MboExtendDoNotPrintFieldNames = void;`.

# 0.2.22

- Changed the way `mbo::types::Extend` types are constructed to support more complex and deeper nested types.
- Provided a new `mbo::extender::Default` which wraps all default extenders.

# 0.2.21

- Optimized `AnyScan`, `ConstScan` and `ConvertingScan` by dropping clone layer. We also explicitly support multiple iterations on one object.

# 0.2.20

- Added `empty` and `size` to `AnyScan`, `ConstScan` and `ConvertingScan`.

# 0.2.19

- Changed `Extender` print and stream ability to output pointers to containers.

# 0.2.18

- Changed `LimitedSet` and `LimitedMap`:
  - Changed to new optimized code for `std::less` and make that the default.
  - Added custom bazel flag `--//mbo/container:limited_ordered_max_unroll_capacity=N`. This controls the maximum capacity `N` for which `index_of` will be unrolled (defaults to 16, range [4...32], see `mbo::container::container_internal::kUnrollMaxCapacityDefault`.
- Changed `RefWrap`:
  - Added constexpr support.
  - Made constructor implicit.

# 0.2.17

- Added template-type `RefWrap<T>`: similar to `std::reference_wrapper` but supports operators `->` and `*`.
- Fixed double computation in `MBO_RETURN_IF_ERROR`.

# 0.2.16

- Fixed `LimitedOptionsFlag::kEmptyDestructor` use in `LimitedVector`.

# 0.2.15

- Added `LimitedOptions` support to `LimitedVector`.

# 0.2.14

- Changed `tstring::find_first_of` and `tstring::find_last_of` solely with `string_view::find` to solve ASAN issue.
- Added `LimitedSet::at_index` and `LimitedMap::at_index`.

# 0.2.13

- Applied several tweaks for `*Scan`.
- Improved compiler error message when using `AnyScan` with incompatible pairs due
  to `first_type` being const on the right side.
- Added concept `IsSameAsAnyOfRaw` / `NotSameAsAnyOfRaw` which determine whether type is one of a list of types.
- Added `const char* tstring::c_str` which is occasionally needed.

# 0.2.12

- Added `ConstScan` and `MakeConstScan`.
- Changed all `Make*Scan` types to create intermediate adapters.
- Made all `*Scan` and `Make*Scan` to accept `std::initializer` types.
- Added support for move-only types.
- Improved `*scan` handling of initializer_lists.

# 0.2.11

- Made `AnyScan` constructor private.
- Separated `AnyScan` and `ConvertingScan` into distinct type with the same internal base.
- Restricted `AnyScanImpl` access to `MakeAnyScan` and `MakeConvertingScan`.
- Added `initializer_list` support for `AnyScan` and `ConvertingScan`.

# 0.2.10

- Added missing `LimitedVector` non-converting constructor for `std::initializer_list`.
- Added trait `ContainerConstIteratorValueType`.
- Added `ConvertingScan` and `MakeConvertingScan`.

# 0.2.9

- Added `mbo::container::AnyScan` for type erased container views (scanning).
- In `//mbo/types:traits_cc`:
  - Added concept `ContainerHasForwardIterator` determines whether a container has `begin`, `end` and `std::forward_iterator` compliant iterators.
  - Added concept `ContainerHasInputIterator` determines whether a container has `begin`, `end` and `std::input_iterator` compliant iterators.
  - Added struct `GetDifferenceType` is either set to the type's `difference_type` or `std::ptrdiff_t`.
  - Added concept `HasDifferenceType` determines whether a type has a `difference_type`.

# 0.2.8

- Made `//mbo/types:traits_cc` publicly visible.

# 0.2.7

- Improved `LimitedMap` and `LimitedSet`:
  - Better ASAN compatibility for constexpr.
  - Allow `LimitedOption` configuration instead of simple length specification.
    - Option to suppress calling clear in the destructor for cases where that is not a constexpr.
    - Option to require presorted input: Allows much large constexpr `LimitedSet`/`LimitedValue`.
- Renamed `types::AbslFormat` to `types::AbslStringify` to better reflect its purpose.
- Changed `types::AbslStringify` to print field names prefixed with a dot '.' (requires Clang).

# 0.2.6

- Made `RunfilesDir/OrDie` work correctly.
- Added a new comparator `mbo::types::CompareLess` which allows container optimizations.
- Moved most functionality of `LimitedMap` and `LimitedSet` to new base `internal::LimitedOrdered`.
- Added `LimitedMap::index_of` and `LimitedSet::index_of` (leaves one location to optimize search).
- Optimized `LimitedMap` and `LimitedSet` with `mbo::types::CompareLess`.

# 0.2.5

- Made `Limited{Map|Set|Vector}` iterators compliant with `std::contiguous_iterator`.
- Added `Limited{Map|Set|Vector}::data()`.

# 0.2.4

- Moved `CopyConvertContainer` to `mbo::container::ConvertContainer` and add conversion functions.
- Various traits fixes to correctly handle C++ concepts (pretty much every published explanation makes the same mistake).
- Added matcher `CapacityIs`.
- Added `LimitedMap`.

# 0.2.3

- For `LimitedVector` add an unused sentinel, so that `end` and other functions do not cause memory issues.

# 0.2.2

- Made `LimitedSet` and `LimitedVector` use C-arrays instead of `std::array` in order to solve some ASAN issues.
- Made `NoDestruct` ASAN friendly.
- Made tests PASS in mode ASAN out-of-the-box for Clang.
- Made `//mbo/types:tstring_test` PASS in ASAN mode.

# 0.2.1

- Fixed issue with MOPE in case it runs as an external dependency.
- Made tests work when run as external repository dependency.
- Added `LimitedSet::contains_all(other)` which performs contains-all-of functionality (not part of STL).
- Added `LimitedSet::contains_any(other)` which performs contains-any-of functionality (not part of STL).

# 0.2

- Added support for GCC (11.4/Ubuntu 22.04).
- Changed Print Extender's `Print` to `ToString` which is a more widely used name.
- Changed Print Extender's `ToString` to print field names (if available, e.g. Clang 16).
- When compiling with Clang show field names with the `AbslStringify` extender.
- Enabled `static constexpr NoDestruct<>` for more cases and compiler versions.
- container:
  - Added:
    - class `LimitedSet`: A space loimited, constexpr compliant set.
    - class `LimitedVector`: A space limited, constexpr compliant vector.
- diff:
  - Updated `unified_diff` with many more diff features.
- file:
  - Added:
    - function `GetMaxLines`: Reads at most given number of text lines from a file or returns absl::Status error.
    - function `JoinPathsRespectAbsolute`: Join multiple path elements respecting absolute path elements.
    - class `IniFile`: A simple INI file reader.
- Mope:
  - The `MOPE` templating engine. Run `bazel run //mbo/mope -- --help` for detailed documentation.
  - mbo/mope
    - binary `mope`.
  - mbo/mope:mope_cc, mbo/mope/mope.h
    - class `Template`: The mope template engine and data holder.
  - mbo/mope:ini_cc, mbo/mope/ini.h
    - function `ReadIniToTemlate`: Helper to initialize a mope Template from an INI file.
  - mbo/mope:mope_bzl, mbo/mope/mope.bzl
    - bzl-rule `mope`: A rule that expands a mope template file.
    - bazl-macro `mope_test`: A test rule compares mope template expansion against golden files. This
      supports `clang-format` and thus can be used for source-code generation and verification.
- status:
  - Added:
    - macro `MBO_RETURN_IF_ERROR`: Macro that simplifies handling functions returning `absl::Status`.
    - macro `MBO_ASSIGN_OR_RETURN`: Macro that simplifies handling functions returning `absl::StatusOr<T>`.
- strings:
  - Added:
    - struct `StripCommentsArgs`: Arguments for `StripComments` and `StripLineComments`.
    - function `StripComments`: Strips comments from lines.
    - function `StripLineComments`: Strips comments from a single line.
    - struct `StripParsedCommentsArgs`: Arguments for `StripParsedComments` and `StripParsedLineComments`.
    - function `StripParsedComments`: Strips comments from parsed lines.
    - function `StripLineParsedComments`: Strips comments from a single parsed line.
- testing:
  - Added:
    - macro `MBO_ASSERT_OK_AND_ASSIGN`: Simplifies testing with functions that return `absl::StatusOr<T>`.
- types:
  - Added:
    - concept `ContainerIsForwardIteratable` determines whether a types can be used in forward iteration.
    - concept `ContainerHasEmplace` determines whether a container has `emplace`.
    - concept `ContainerHasEmplaceBack` determines whether a container has `emplace_back`.
    - concept `ContainerHasInsert` determines whether a container has `insert`.
    - concept `ContainerHasPushBack` determines whether a container has `push_back`.
    - conversion struct `CopyConvertContainer` simplifies copying containers to value convertible containers.
    - concept `IsAggregate` determines whether a type is an aggregate.
    - concept `IsDecomposable` determines whether a type can be used in static-bindings.
    - concept `IsBracesConstructibleV` determines whether a type can be constructe from given argument types.
    - struct `NoDestruct<T>`: Implements a type that allows to use any type as a static constant.

# 0.1

- Initial release: Clang support only.
