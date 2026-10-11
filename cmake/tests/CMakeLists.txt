# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
project(mbo_cmake_consumer LANGUAGES CXX)

# Exercise the documented embedding path with no parent C++ standard or includes.
add_subdirectory(../.. mbo)
# Rebuild an existing program solely through the aggregate's public requirements.
add_executable(consumer ../../mbo/hash/hash_tool.cc)
target_link_libraries(consumer PRIVATE mbo::mbo)
enable_testing()
add_test(NAME consumer COMMAND consumer dumbo "CMake consumer")
