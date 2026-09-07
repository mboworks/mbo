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

#ifndef MBO_MOPE_MOPE_H_
#define MBO_MOPE_MOPE_H_

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mbo/container/any_scan.h"
#include "mbo/types/extend.h"
#include "mbo/types/variant.h"

namespace mbo::mope {

// MOPE: Mope Over Pump Ends - Is a simple templating system.
//
// See Mope binary for details.
// ```
class Template {
 public:
  // Determines whether `name` is allowed for `SetValue` and `AddSection`.
  static bool IsValidName(std::string_view name);

  explicit Template() = default;

  // Sets template variable `name` to `value`.
  // The function by default only inserts new values, but if `allow_update` is true, it will also
  // overwrite existing values. It will however fail if an existing value is not a matching value type.
  // Returns whether `name` was newly added (or overwritten).
  absl::Status SetValue(std::string_view name, std::string_view value, bool allow_update = false);

  // Add a sub-dictionary under `name`.
  // The purpose of a sub-dictionary is to be filled, which can be done by calling `SetValue` or `AddSubDictionary`, on
  // the returned value (so it must be used to do so).
  // If the function is called repeatedly with the same `name`, then each time a new dictionary for that `name` will be
  // added.
  absl::StatusOr<Template*> AddSection(std::string_view name);

  // Expands the template `output` in-place.
  absl::Status Expand(std::string& output) const;
  absl::Status Expand(
      std::string& output,
      const mbo::container::ConvertingScan<std::pair<std::string_view, std::string_view>>& context_data) const;

 private:
  friend struct MopeTest;

  enum class TagType {
    kValue,
    kSection,
    kControl,
  };

  struct TagInfo : mbo::types::Extend<TagInfo> {
    std::string name;
    std::string start;
    std::string end;
    std::optional<std::string> config;
    std::optional<std::string> option;
    TagType type = TagType::kValue;
  };

  template<class DataType>
  struct TagData : mbo::types::Extend<TagData<DataType>> {
    const TagInfo tag;
    DataType data;
  };

  struct Section {
    mutable std::string join;
    std::vector<Template> dictionary;
  };

  struct Range : mbo::types::Extend<Range> {
    int start = 0;
    int end = 0;
    int step = 1;
    std::string join;
    mutable bool expanding = false;
    mutable int curr = 0;
  };

  struct RangeData : mbo::types::Extend<RangeData> {
    std::string start;
    std::string end;
    std::string step;
    std::string join;
  };

  // Type `Data` holds all possible information variants. Each of these needs
  // to have a matching `Expand(const TagInfo<Data-Type>&, std::string*)`.
  using Data = std::variant<TagData<Section>, TagData<Range>, TagData<std::string>>;
  using DataMap = absl::node_hash_map<std::string, Data>;

  friend std::ostream& operator<<(std::ostream& os, const Data& data) {
    std::visit(
        mbo::types::Overloaded{
            [&os](const TagData<Section>& /*arg*/) { os << "Section"; },
            [&os](const TagData<Range>& /*arg*/) { os << "Range"; },
            [&os](const TagData<std::string>& arg) { os << "String: '" << arg.data << "'"; },
        },
        data);
    return os;
  }

  struct Context {
    DataMap data;
  };

  static std::optional<const Template::TagInfo> FindAndConsumeTag(std::string_view& pos);
  static std::pair<std::size_t, std::size_t> MaybeExpandWhiteSpace(
      std::string_view output,
      const TagInfo& tag,
      std::size_t tag_pos);

  static absl::Status SetValueInternal(std::string_view name, std::string_view value, bool allow_update, DataMap& data);

  bool Exists(std::string_view name, const Context& ctx) const;
  const Data* Lookup(std::string_view name, const Context& ctx) const;
  absl::Status MaybeLookup(const TagInfo& tag_info, std::string_view data, const Context& ctx, std::string& value)
      const;
  absl::Status MaybeLookup(const TagInfo& tag_info, std::string_view data, const Context& ctx, int& value) const;
  absl::Status ExpandRangeTag(const TagInfo& tag, Range& range, Context& ctx, std::string& output) const;
  absl::Status ExpandRangeData(const TagInfo& tag, const RangeData& range_data, Context& ctx, std::string& output)
      const;
  absl::Status ExpandConfiguredSection(
      std::string_view name,
      std::vector<std::string> str_list,
      std::string_view join,
      Context& ctx,
      std::string& output) const;
  absl::Status ExpandConfiguredList(
      const TagInfo& tag,
      std::string_view str_list_data,
      Context& ctx,
      std::string& output) const;
  absl::Status ExpandSectionTag(const TagInfo& tag, Context& ctx, std::string& output) const;
  absl::Status ExpandControlTag(const TagInfo& tag, Context& ctx) const;
  absl::StatusOr<bool> ExpandValueTag(const TagInfo& tag, Context& ctx, std::string& output) const;

  absl::Status ExpandInternal(Context& ctx, std::string& output) const;

  static absl::Status ExpandSection(const TagData<Section>& tag, Context& ctx, std::string& output);

  template<typename Sink>
  friend void AbslStringify(Sink& /*sink*/, TagType /*tag*/);

  DataMap data_;
};

}  // namespace mbo::mope

#endif  // MBO_MOPE_MOPE_H_
