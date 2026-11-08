# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Versions come from MODULE.bazel. Existing parent-project targets take priority.
if(NOT TARGET GTest::gmock)
  if(MBO_FETCH_DEPENDENCIES)
    set(INSTALL_GTEST OFF)
    # Bootstrap before Abseil's test helpers; wire its Abseil backend below.
    set(GTEST_HAS_ABSL OFF)
    FetchContent_Declare(googletest SYSTEM EXCLUDE_FROM_ALL
      URL "https://github.com/google/googletest/archive/refs/tags/v${MBO_VERSION_googletest}.tar.gz")
    FetchContent_MakeAvailable(googletest)
    set(MBO_FETCHED_GOOGLETEST ON)
  else()
    find_package(GTest CONFIG REQUIRED)
  endif()
endif()
set(MBO_GOOGLETEST_SOURCE_DIR "${googletest_SOURCE_DIR}" CACHE PATH
  "GoogleTest source tree (needed for the runfiles test's LICENSE fixture)")

if(NOT TARGET absl::strings)
  if(MBO_FETCH_DEPENDENCIES)
    set(ABSL_BUILD_TESTING OFF)
    set(ABSL_BUILD_TEST_HELPERS ${MBO_BUILD_TESTING})
    set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
    set(ABSL_FIND_GOOGLETEST OFF)
    set(ABSL_ENABLE_INSTALL OFF)
    FetchContent_Declare(abseil SYSTEM EXCLUDE_FROM_ALL
      URL "https://github.com/abseil/abseil-cpp/archive/refs/tags/${MBO_VERSION_abseil_cpp}.tar.gz")
    FetchContent_MakeAvailable(abseil)
  else()
    find_package(absl CONFIG REQUIRED)
  endif()
endif()

if(NOT TARGET re2::re2)
  if(MBO_FETCH_DEPENDENCIES)
    set(RE2_BUILD_TESTING OFF)
    set(RE2_INSTALL OFF)
    FetchContent_Declare(re2 SYSTEM EXCLUDE_FROM_ALL
      URL "https://github.com/google/re2/releases/download/${MBO_VERSION_re2}/re2-${MBO_VERSION_re2}.zip")
    FetchContent_MakeAvailable(re2)
  else()
    find_package(re2 CONFIG REQUIRED)
  endif()
endif()

# Match Bazel's --define absl=1, including RE2 regular expressions in death tests.
# GoogleTest and Abseil's test helpers depend on each other; their CMake targets
# can be wired once both projects and RE2 have been declared.
if(MBO_FETCHED_GOOGLETEST)
  target_compile_definitions(gtest PUBLIC GTEST_HAS_ABSL=1)
  target_link_libraries(gtest PUBLIC
    absl::failure_signal_handler absl::stacktrace absl::symbolize
    absl::flags_parse absl::flags_reflection absl::flags_usage absl::strings re2::re2)
elseif(MBO_BUILD_TESTING)
  get_target_property(gtest_defines GTest::gtest INTERFACE_COMPILE_DEFINITIONS)
  if(NOT "GTEST_HAS_ABSL=1" IN_LIST gtest_defines)
    message(FATAL_ERROR "MBO tests require GoogleTest built with GTEST_HAS_ABSL=ON")
  endif()
endif()

# rules_cc has no CMake project; only its standalone runfiles source is compiled.
FetchContent_Declare(rules_cc
  URL "https://github.com/bazelbuild/rules_cc/archive/refs/tags/${MBO_VERSION_rules_cc}.tar.gz"
  SOURCE_SUBDIR cmake-unused)
if(NOT MBO_FETCH_DEPENDENCIES AND NOT FETCHCONTENT_SOURCE_DIR_RULES_CC)
  message(FATAL_ERROR "Set FETCHCONTENT_SOURCE_DIR_RULES_CC to rules_cc sources when fetching is disabled")
endif()
FetchContent_MakeAvailable(rules_cc)

if(MBO_BUILD_BENCHMARKS AND NOT TARGET benchmark::benchmark)
  if(MBO_FETCH_DEPENDENCIES)
    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    FetchContent_Declare(benchmark SYSTEM EXCLUDE_FROM_ALL
      URL "https://github.com/google/benchmark/archive/refs/tags/v${MBO_VERSION_google_benchmark}.tar.gz")
    FetchContent_MakeAvailable(benchmark)
  else()
    find_package(benchmark CONFIG REQUIRED)
  endif()
endif()

if(MBO_BUILD_TESTING)
  if(NOT MBO_GOOGLETEST_SOURCE_DIR)
    message(FATAL_ERROR "Set MBO_GOOGLETEST_SOURCE_DIR for the runfiles test's LICENSE fixture")
  endif()
  # Reuse the dev-only xxHash download URL from the Bazel declaration.
  file(READ "${MBO_SOURCE_DIR}/bazelmod/dev.MODULE.bazel" dev_module)
  string(REGEX MATCH "https://github.com/Cyan4973/xxHash/archive/refs/tags/[^\"]+" xxhash_url "${dev_module}")
  FetchContent_Declare(xxhash URL "${xxhash_url}" SOURCE_SUBDIR cmake-unused)
  if(NOT MBO_FETCH_DEPENDENCIES AND NOT FETCHCONTENT_SOURCE_DIR_XXHASH)
    message(FATAL_ERROR "Set FETCHCONTENT_SOURCE_DIR_XXHASH when fetching is disabled")
  endif()
  FetchContent_MakeAvailable(xxhash)
  add_library(xxhash INTERFACE)
  target_include_directories(xxhash SYSTEM INTERFACE "${xxhash_SOURCE_DIR}")
  target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
endif()
