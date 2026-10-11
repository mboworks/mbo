# CMake support (EXPERIMENTAL)

The CMake build provides one component target per `mbo/*` library, an overall library target,
individual binaries, and CTest registration for the existing C++ unit tests. Bazel remains the
primary build system. The root `CMakeLists.txt` includes the build definitions in this directory.

## Requirements and build

- CMake 3.28 or newer.
- Python 3.10 or newer at configure time.
- C++23 with GCC 15 and matching libstdc++, Clang 22 and a supported standard library,
  or the Apple Clang toolchain described in the root README.
- A Unix platform. CI verifies Ubuntu with GCC 15.
- Network access for dependencies, unless their sources or CMake targets are supplied locally.

```sh
CC=gcc-15 CXX=g++-15 cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel 2
ctest --test-dir build-cmake --output-on-failure --parallel 2
```

MBO's compiled sources, tests, and binaries use `-Wall -Wextra -Wpedantic -Werror` and the
existing compiler-specific warning exceptions. Ordinary MBO compilation disables exceptions;
existing exception tests retain their target-specific compiler options. Warning options are
private to MBO compilation and do not change the parent application's warning policy.

## Library targets

Link the overall `mbo` target through `mbo::mbo`, or select a component. Compiled components
are static libraries; components containing only headers are interface libraries. Each component
includes its subdirectories, including `mbo/container/experimental` and `mbo/file/ini`.
C++23, generated headers, include directories, and required link dependencies propagate to consumers.

| Library directory | Build target    | Link target      |
| :---------------- | :-------------- | :--------------- |
| All libraries     | `mbo`           | `mbo::mbo`       |
| `mbo/config`      | `mbo_config`    | `mbo::config`    |
| `mbo/container`   | `mbo_container` | `mbo::container` |
| `mbo/diff`        | `mbo_diff`      | `mbo::diff`      |
| `mbo/digest`      | `mbo_digest`    | `mbo::digest`    |
| `mbo/file`        | `mbo_file`      | `mbo::file`      |
| `mbo/hash`        | `mbo_hash`      | `mbo::hash`      |
| `mbo/json`        | `mbo_json`      | `mbo::json`      |
| `mbo/log`         | `mbo_log`       | `mbo::log`       |
| `mbo/memory`      | `mbo_memory`    | `mbo::memory`    |
| `mbo/mope`        | `mbo_mope`      | `mbo::mope`      |
| `mbo/status`      | `mbo_status`    | `mbo::status`    |
| `mbo/strings`     | `mbo_strings`   | `mbo::strings`   |
| `mbo/testing`     | `mbo_testing`   | `mbo::testing`   |
| `mbo/types`       | `mbo_types`     | `mbo::types`     |

```cmake
add_subdirectory(path/to/mbo)
add_executable(application main.cc)
target_link_libraries(application PRIVATE mbo::container mbo::strings)
# Or: target_link_libraries(application PRIVATE mbo::mbo)
```

Embedding through `FetchContent` also uses the root `CMakeLists.txt`. Tests and command-line
binaries default to off when MBO is a subproject. The `mbo/testing` component depends on GoogleTest
and compiles the existing `rules_cc` runfiles implementation. Linking `mbo::mbo` includes this component.

## Binary targets

Every existing `cc_binary` has a CMake target named `mbo_bin_<name>` and an executable alias
`mbo::bin::<name>`. Executables are written to `bin/<target>/<name>` in the build tree.
Command-line programs and developer tools require `MBO_BUILD_BINARIES`; benchmarks require
`MBO_BUILD_BENCHMARKS` independently.

| Binary name                                | Source library               | Option                 |
| :----------------------------------------- | :--------------------------- | :--------------------- |
| `arena_benchmark`                          | `mbo/memory`                 | `MBO_BUILD_BENCHMARKS` |
| `arena_layout_benchmark`                   | `mbo/memory`                 | `MBO_BUILD_BENCHMARKS` |
| `diff`                                     | `mbo/diff`                   | `MBO_BUILD_BINARIES`   |
| `diff_benchmark`                           | `mbo/diff`                   | `MBO_BUILD_BENCHMARKS` |
| `digest`                                   | `mbo/digest`                 | `MBO_BUILD_BINARIES`   |
| `frozen_benchmark`                         | `mbo/container/experimental` | `MBO_BUILD_BENCHMARKS` |
| `frozen_probe`                             | `mbo/container/experimental` | `MBO_BUILD_BINARIES`   |
| `glob`                                     | `mbo/file`                   | `MBO_BUILD_BINARIES`   |
| `hash_benchmark`                           | `mbo/hash`                   | `MBO_BUILD_BENCHMARKS` |
| `hash_test_vectors_gen`                    | `mbo/hash`                   | `MBO_BUILD_BINARIES`   |
| `hash_tool`                                | `mbo/hash`                   | `MBO_BUILD_BINARIES`   |
| `limited_set_benchmark`                    | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |
| `mope`                                     | `mbo/mope`                   | `MBO_BUILD_BINARIES`   |
| `ordered_lookup_benchmark`                 | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |
| `segmented_deque_benchmark`                | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |
| `segmented_vector_benchmark`               | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |
| `segmented_vector_element_shape_benchmark` | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |
| `segmented_vector_lifecycle_benchmark`     | `mbo/container`              | `MBO_BUILD_BENCHMARKS` |

```sh
cmake -S . -B build-cmake -DMBO_BUILD_BENCHMARKS=ON
cmake --build build-cmake --target mbo_bin_glob mbo_bin_frozen_benchmark --parallel 2
build-cmake/bin/mbo_bin_glob/glob --help
```

## Configuration options

| Cache variable                            | Default                          | Meaning                                                                       |
| :---------------------------------------- | :------------------------------- | :---------------------------------------------------------------------------- |
| `MBO_BUILD_BENCHMARKS`                    | `OFF`                            | Build all benchmark binaries; fetch Google Benchmark when needed.             |
| `MBO_BUILD_BINARIES`                      | Top-level: `ON`; embedded: `OFF` | Build command-line programs and developer tools.                              |
| `MBO_BUILD_TESTING`                       | Top-level: `ON`; embedded: `OFF` | Build C++ unit tests and register CTest cases.                                |
| `MBO_FETCH_DEPENDENCIES`                  | `ON`                             | Fetch pinned dependency sources when their targets are absent.                |
| `MBO_GOOGLETEST_SOURCE_DIR`               | Fetched GoogleTest directory     | GoogleTest sources providing the runfiles test's `LICENSE` fixture.           |
| `MBO_HASH_MANGLE_SEED`                    | Empty string                     | Printable ASCII seed selecting the hash mangle bucket.                        |
| `MBO_HASH_MANGLE_SEED_BUCKETS`            | `8`                              | Bucket count; `0` disables mangling, `1` selects a fixed nonzero constant.    |
| `MBO_LIMITED_ORDERED_MAX_UNROLL_CAPACITY` | `16`                             | `LimitedOrdered` unroll capacity, from `4` through `32`.                      |
| `MBO_REQUIRE_THROWS`                      | `OFF`                            | Throw on requirement violations when the translation unit enables exceptions. |

Use consistent requirement and hash options throughout a program. The generated headers are
written under `mbo-generated/` in the build tree. Their configuration template, hash arithmetic,
and hash header template come from the existing Bazel generators. The module version and dependency
versions come from `MODULE.bazel`; the xxHash test reference comes from `bazelmod/dev.MODULE.bazel`.

## Dependency reuse

Existing `GTest::gmock`, `absl::strings`, `re2::re2`, and `benchmark::benchmark` targets take
priority over fetching. Dependencies must provide the targets used by MBO; builds enabling tests
also need Abseil's test helpers, including `absl::hash_testing` and `absl::scoped_mock_log`.
GoogleTest must use `GTEST_HAS_ABSL=ON` for the existing logging tests' RE2 expressions.
Fetched GoogleTest targets enable that backend after Abseil and RE2 targets are available.

To reuse local sources while retaining the dependency build configuration, set
`FETCHCONTENT_SOURCE_DIR_ABSEIL`, `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`,
`FETCHCONTENT_SOURCE_DIR_RE2`, and `FETCHCONTENT_SOURCE_DIR_RULES_CC`. Set
`FETCHCONTENT_SOURCE_DIR_BENCHMARK` for benchmarks and `FETCHCONTENT_SOURCE_DIR_XXHASH` for tests.

With `MBO_FETCH_DEPENDENCIES=OFF`, absent CMake targets are loaded through `find_package(... CONFIG)`.
Supply `FETCHCONTENT_SOURCE_DIR_RULES_CC` in every configuration and `FETCHCONTENT_SOURCE_DIR_XXHASH`
when tests are enabled. Tests using an externally provided GoogleTest package also require
`MBO_GOOGLETEST_SOURCE_DIR` for the runfiles fixture.

## Updating the build automatically

`generate.py` runs automatically during CMake configuration. It writes `targets.cmake`,
`versions.cmake`, and the generated configuration/hash headers into `mbo-generated/` in the
build tree. These generated files are not committed. No separate CMake source list or dependency
version list needs updating when a supported Bazel C++ declaration changes.

```sh
# Configure or regenerate after editing a BUILD.bazel file or MODULE.bazel.
cmake -S . -B build-cmake
# A build also reconfigures when tracked generator inputs change.
cmake --build build-cmake --parallel 2
# Verify the adapter independently of CMake or Bazel.
python3 cmake/generate_test.py
```

`python3 cmake/generate.py --help` lists the standalone generator's options. CMake passes its compiler
and configuration options automatically. Add adapter support when introducing an expression,
dependency, compiler option, or link option that the adapter rejects.

## Validation and scope

The GCC CI job builds all components and binaries, runs CTest, checks requirement exceptions
with disabled hash mangling, and builds the separate [`tests/`](tests/CMakeLists.txt) consumer.
Unit tests retain their Bazel source lists, dependencies, compiler options, and compatibility selects.
CTest supplies isolated temporary directories and a runfiles tree for the existing data tests.

`generate.py` parses the existing `cc_library`, `cc_binary`, and `cc_test` declarations. It fails
on unsupported expressions or dependencies. Edits to those definitions trigger CMake regeneration.
The adapter's tests check component and binary coverage, compiler and policy selects, dependency
translation, and canonical hash header generation.

CMake does not provide installation/package-export rules, Bazel/Starlark tests, shell integration
tests, fuzz targets, generated-source freshness checks, coverage, or sanitizer configurations.
The committed generated C++ sources remain the build inputs. Bazel continues to verify their
freshness and the additional test families. CMake target naming and configuration are experimental.
