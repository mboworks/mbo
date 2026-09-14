// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_HAMT_NODE_STRING_INDEX_H_
#define MBO_STRINGS_HAMT_NODE_STRING_INDEX_H_

#include <functional>
#include <string_view>

#include "mbo/container/hamt_node_map.h"
#include "mbo/strings/hamt_string_index.h"

namespace mbo::strings {

// Node-owned index payloads; character bytes remain borrowed from string storage.
template<
    typename Id = StringId<>,
    typename Hash = std::hash<std::string_view>,
    typename Equal = std::equal_to<>,
    mbo::container::HamtOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
using HamtNodeStringIndex = HamtStringIndex<
    Id,
    Hash,
    Equal,
    Options,
    Source,
    mbo::container::HamtNodeMap<std::string_view, Id, Hash, Equal, Options, Source>>;

}  // namespace mbo::strings

#endif  // MBO_STRINGS_HAMT_NODE_STRING_INDEX_H_
