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

#ifndef MBO_MOPE_INI_H_
#define MBO_MOPE_INI_H_
#include <string_view>

#include "absl/status/status.h"
#include "mbo/mope/mope.h"

namespace mbo::mope {

// Reads the INI file `filename` and configures `root_template` with its data.
//
// INI groups are used as sections. They can build a hierarchy:
//
// * The group names are split at '.' to make up the nesting levels.
// * Each level can be repeated by appending a different ':' to the level name.
//
// Example:
//
// ```
// [person]
// id=0
// [person.contact]
// phone=1234
// [person.contact:1]
// phone=2345
// [person:1]
// id=1
// [person:1.contact]
// phone=3456
// [person:1.contact:1]
// phone=4567
// ```
absl::Status ReadIniToTemplate(std::string_view filename, Template& root_template);

// Compatibility alias for ReadIniToTemplate.
absl::Status ReadIniToTemlate(std::string_view filename, Template& root_template);

}  // namespace mbo::mope

#endif  // MBO_MOPE_INI_H_
