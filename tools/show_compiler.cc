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

#include <iostream>
#include <version>

#if defined(__APPLE__)
# include <Availability.h>
#endif

int main() {
#if defined(__GNUC__) && !defined(__clang__)
  std::cout << "Compiler: GCC\n";
  std::cout << "Compiler version: " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
  std::cout << "Compiler version extra: " << __VERSION__ << "\n";
#elif defined(__clang__) && defined(__clang_major__) && defined(__clang_minor__) && defined(__clang_patchlevel__)
# if defined(__apple_build_version__)
  std::cout << "Compiler: Apple Clang\n";
# else
  std::cout << "Compiler: Clang\n";
# endif
  std::cout << "Compiler version: " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
  std::cout << "Compiler version extra: " << __clang_version__ << "\n";
# if defined(__apple_build_version__)
  std::cout << "Compiler build version: " << __apple_build_version__ << "\n";
# endif
#else   // __clang__ / __GNUC__
  std::cerr << "Unknown compiler!";
  return 1;
#endif  // __clang__ / __GNUC__

  std::cout << "Language: __cplusplus=" << __cplusplus << "\n";
#if defined(_LIBCPP_VERSION)
  std::cout << "Standard library: libc++ " << _LIBCPP_VERSION << "\n";
#elif defined(__GLIBCXX__)
  std::cout << "Standard library: libstdc++";
# if defined(_GLIBCXX_RELEASE)
  std::cout << " " << _GLIBCXX_RELEASE;
# endif
  std::cout << " (__GLIBCXX__=" << __GLIBCXX__ << ")\n";
#else
  std::cout << "Standard library: unknown\n";
#endif

#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
  std::cout << "macOS deployment target: " << __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ << "\n";
#endif
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED)
  std::cout << "macOS SDK maximum: " << __MAC_OS_X_VERSION_MAX_ALLOWED << "\n";
#endif
  return 0;
}
