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

#if __has_include("mbo/config/config_gen.h")
# include "mbo/config/config_gen.h"  // IWYU pragma: export
#else
# include "mbo/config/internal/config.h.in"  // IWYU pragma: export
# if __STDC_VERSION__ >= (20L * 100 + 23) * 100 + 11
#  if !defined(IS_CLANGD)
#   warning "The correctly generated header is not available. Falling back to template."
#  endif  // __STDC_VERSION__ >= 202311L
# endif   // !defined(IS_CLANGD)
#endif

#if __cplusplus >= (20L * 100 + 23) * 100 + 2
# define MBO_CONFIG_CONSTEXPR_23 constexpr
#else  // __cplusplus >= 202302L
# define MBO_CONFIG_CONSTEXPR_23
#endif  // __cplusplus >= 202302L

#if defined(__clang__)
# if __has_attribute(trivial_abi)
#  define MBO_CONFIG_TRIVIAL_ABI [[clang::trivial_abi]]
# else
#  define MBO_CONFIG_TRIVIAL_ABI
# endif
#else
# define MBO_CONFIG_TRIVIAL_ABI
#endif

#endif  // MBO_CONFIG_CONFIG_H_
