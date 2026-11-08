# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)
include(FetchContent)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(MBO_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(MBO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/mbo-generated")
option(MBO_BUILD_TESTING "Build MBO C++ tests and register them with CTest" ${PROJECT_IS_TOP_LEVEL})
option(MBO_BUILD_BINARIES "Build MBO command-line programs and developer tools" ${PROJECT_IS_TOP_LEVEL})
option(MBO_BUILD_BENCHMARKS "Build MBO benchmark programs" OFF)
option(MBO_FETCH_DEPENDENCIES "Fetch pinned dependencies when their targets are absent" ON)
option(MBO_ENABLE_EXCEPTIONS "Enable exceptions when compiling MBO targets" OFF)
option(MBO_REQUIRE_THROWS "Throw on requirement violations when exceptions are enabled" OFF)
set(MBO_LIMITED_ORDERED_MAX_UNROLL_CAPACITY 16 CACHE STRING "LimitedOrdered unroll capacity [4, 32]")
set(MBO_HASH_MANGLE_SEED "" CACHE STRING "Printable ASCII hash mangle seed")
set(MBO_HASH_MANGLE_SEED_BUCKETS 8 CACHE STRING "Hash mangle buckets (0 disables mangling)")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(MBO_COMPILER gcc)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR "MBO requires GCC 15 or newer and matching libstdc++")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(MBO_COMPILER clang)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22)
    message(FATAL_ERROR "MBO requires Clang 22 or newer and a supported standard library")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
    message(FATAL_ERROR "MBO requires Xcode 16.3 or newer and matching Apple Clang/libc++")
  endif()
else()
  message(FATAL_ERROR "MBO CMake supports GCC and Clang on Unix platforms")
endif()
if(NOT UNIX)
  message(FATAL_ERROR "MBO CMake currently requires a Unix platform")
endif()

# The standard is scoped to this directory and its dependencies, not the parent.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# MBO uses headers and ordinary translation units, with no C++ module units.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
file(GLOB_RECURSE MBO_BUILD_FILES CONFIGURE_DEPENDS "${MBO_SOURCE_DIR}/mbo/BUILD.bazel")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${MBO_BUILD_FILES}
  "${MBO_SOURCE_DIR}/MODULE.bazel"
  "${CMAKE_CURRENT_LIST_DIR}/generate.py"
  "${MBO_SOURCE_DIR}/mbo/config/internal/config.h.in"
  "${MBO_SOURCE_DIR}/mbo/hash/hash.bzl"
  "${MBO_SOURCE_DIR}/mbo/hash/internal/hash_mangle_seed.bzl"
)
set(MBO_GENERATOR_ARGS
  --source "${MBO_SOURCE_DIR}" --output "${MBO_GENERATED_DIR}"
  --compiler "${MBO_COMPILER}" --unroll "${MBO_LIMITED_ORDERED_MAX_UNROLL_CAPACITY}"
  --buckets "${MBO_HASH_MANGLE_SEED_BUCKETS}"
)
if(MBO_REQUIRE_THROWS)
  list(APPEND MBO_GENERATOR_ARGS --require-throws)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate.py"
  ${MBO_GENERATOR_ARGS} "--seed=${MBO_HASH_MANGLE_SEED}" COMMAND_ERROR_IS_FATAL ANY)
include("${MBO_GENERATED_DIR}/versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies.cmake")

function(mbo_compile_options target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
  if(MBO_ENABLE_EXCEPTIONS)
    target_compile_options(${target} PRIVATE -fexceptions)
  else()
    target_compile_options(${target} PRIVATE -fno-exceptions)
  endif()
  if(MBO_COMPILER STREQUAL "clang")
    target_compile_options(${target} PRIVATE
      -Wno-missing-designated-field-initializers -Wno-c2y-extensions
      -Wno-gcc-compat -Wno-nullability-extension)
  endif()
endfunction()

function(mbo_add_library component)
  cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
  if(ARG_SOURCES)
    add_library(mbo_${component} STATIC ${ARG_SOURCES})
    set(scope PUBLIC)
    mbo_compile_options(mbo_${component})
  else()
    add_library(mbo_${component} INTERFACE)
    set(scope INTERFACE)
  endif()
  add_library(mbo::${component} ALIAS mbo_${component})
  target_compile_features(mbo_${component} ${scope} cxx_std_23)
  target_include_directories(mbo_${component} ${scope}
    "${MBO_GENERATED_DIR}" "${MBO_SOURCE_DIR}")
  set_property(GLOBAL APPEND PROPERTY MBO_COMPONENTS mbo_${component})
endfunction()

function(mbo_link_library component)
  cmake_parse_arguments(ARG "" "" "DEPS" ${ARGN})
  get_target_property(kind mbo_${component} TYPE)
  if(kind STREQUAL "INTERFACE_LIBRARY")
    set(scope INTERFACE)
  else()
    set(scope PUBLIC)
  endif()
  target_link_libraries(mbo_${component} ${scope} ${ARG_DEPS})
endfunction()

function(mbo_add_executable target)
  cmake_parse_arguments(ARG "" "KIND;OUTPUT" "SOURCES;DEPS;COPTS;DEFINES" ${ARGN})
  if(ARG_KIND STREQUAL "test")
    if(NOT MBO_BUILD_TESTING)
      return()
    endif()
  elseif("benchmark::benchmark" IN_LIST ARG_DEPS)
    if(NOT MBO_BUILD_BENCHMARKS)
      return()
    endif()
  elseif(NOT MBO_BUILD_BINARIES)
    return()
  endif()
  add_executable(${target} ${ARG_SOURCES})
  mbo_compile_options(${target})
  target_link_libraries(${target} PRIVATE ${ARG_DEPS})
  target_compile_options(${target} PRIVATE ${ARG_COPTS})
  target_compile_definitions(${target} PRIVATE ${ARG_DEFINES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${ARG_OUTPUT}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${target}")
  if(ARG_KIND STREQUAL "test")
    add_test(NAME ${target}
      COMMAND "${Python3_EXECUTABLE}" "${MBO_SOURCE_DIR}/cmake/run_test.py"
        "${CMAKE_CURRENT_BINARY_DIR}/test-tmp/${target}" "$<TARGET_FILE:${target}>")
    set_tests_properties(${target} PROPERTIES
      ENVIRONMENT "TEST_WORKSPACE=_main;TEST_SRCDIR=${MBO_RUNFILES_DIR}")
  else()
    add_executable(mbo::bin::${ARG_OUTPUT} ALIAS ${target})
  endif()
endfunction()

if(MBO_BUILD_TESTING)
  enable_testing()
  set(MBO_RUNFILES_DIR "${CMAKE_CURRENT_BINARY_DIR}/tests.runfiles")
  file(MAKE_DIRECTORY "${MBO_RUNFILES_DIR}")
  file(CREATE_LINK "${MBO_SOURCE_DIR}" "${MBO_RUNFILES_DIR}/_main" SYMBOLIC)
  file(CREATE_LINK "${MBO_GOOGLETEST_SOURCE_DIR}" "${MBO_RUNFILES_DIR}/googletest" SYMBOLIC)
  file(WRITE "${MBO_RUNFILES_DIR}/_repo_mapping" "_main,googletest,googletest\n")
endif()
include("${MBO_GENERATED_DIR}/targets.cmake")

# Compile the existing runfiles implementation in the single testing component.
target_sources(mbo_testing PRIVATE "${rules_cc_SOURCE_DIR}/cc/runfiles/runfiles.cc")
set_source_files_properties("${rules_cc_SOURCE_DIR}/cc/runfiles/runfiles.cc"
  PROPERTIES COMPILE_OPTIONS -w)
file(MAKE_DIRECTORY "${MBO_GENERATED_DIR}/rules_cc")
file(CREATE_LINK "${rules_cc_SOURCE_DIR}/cc" "${MBO_GENERATED_DIR}/rules_cc/cc" SYMBOLIC)

add_library(mbo INTERFACE)
add_library(mbo::mbo ALIAS mbo)
get_property(MBO_COMPONENTS GLOBAL PROPERTY MBO_COMPONENTS)
target_link_libraries(mbo INTERFACE ${MBO_COMPONENTS})

if(MBO_BUILD_TESTING)
  add_test(NAME mbo_cmake_generator
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_test.py")
endif()
