// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MBO_CONFIG_CONFIG_H_
#define MBO_CONFIG_CONFIG_H_

#include <version>

#if !defined(__cplusplus) || __cplusplus < 202'302L
# error "mbo requires C++23 (__cplusplus >= 202302L)"
#endif

#if !defined(__cpp_constexpr) || __cpp_constexpr < 202'211L
# error "mbo requires C++23 constexpr support (__cpp_constexpr >= 202211L)"
#endif

#if !defined(__cpp_if_consteval) || __cpp_if_consteval < 202'106L
# error "mbo requires if consteval support (__cpp_if_consteval >= 202106L)"
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202'211L
# error "mbo requires C++23 std::expected monadic operations (__cpp_lib_expected >= 202211L)"
#endif

#if !defined(__cpp_lib_containers_ranges) || __cpp_lib_containers_ranges < 202'202L
# error "mbo requires C++23 container range support (__cpp_lib_containers_ranges >= 202202L)"
#endif

#if !defined(__cpp_lib_string_contains) || __cpp_lib_string_contains < 202'011L
# error "mbo requires string contains support (__cpp_lib_string_contains >= 202011L)"
#endif

#if !defined(__cpp_lib_byteswap) || __cpp_lib_byteswap < 202'110L
# error "mbo requires std::byteswap support (__cpp_lib_byteswap >= 202110L)"
#endif

#if __has_include("mbo/config/config_gen.h")
# include "mbo/config/config_gen.h"  // IWYU pragma: export
#else
# include "mbo/config/internal/config.h.in"  // IWYU pragma: export
# if !defined(IS_CLANGD)
#  warning "The correctly generated header is not available. Falling back to template."
# endif  // !defined(IS_CLANGD)
#endif

#endif  // MBO_CONFIG_CONFIG_H_
