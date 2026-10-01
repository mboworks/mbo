// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_NODE_STRING_INDEX_H_
#define MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_NODE_STRING_INDEX_H_

#include <functional>
#include <string_view>

#include "mbo/container/experimental/hamt/hamt_node_map.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"

namespace mbo::container::experimental::hamt {

// Node-owned index payloads; character bytes remain borrowed from string storage.
template<
    typename Id = mbo::strings::experimental::StringId<>,
    typename Hash = std::hash<std::string_view>,
    typename Equal = std::equal_to<>,
    mbo::container::experimental::hamt::HamtOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
using HamtNodeStringIndex = HamtStringIndex<
    Id,
    Hash,
    Equal,
    Options,
    Source,
    mbo::container::experimental::hamt::HamtNodeMap<std::string_view, Id, Hash, Equal, Options, Source>>;

}  // namespace mbo::container::experimental::hamt

#endif  // MBO_CONTAINER_EXPERIMENTAL_HAMT_HAMT_NODE_STRING_INDEX_H_
