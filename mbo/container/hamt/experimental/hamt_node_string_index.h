// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_HAMT_EXPERIMENTAL_HAMT_NODE_STRING_INDEX_H_
#define MBO_CONTAINER_HAMT_EXPERIMENTAL_HAMT_NODE_STRING_INDEX_H_

#include <functional>
#include <string_view>

#include "mbo/container/hamt/experimental/hamt_node_map.h"
#include "mbo/container/hamt/experimental/hamt_string_index.h"

namespace mbo::container::hamt::experimental {

// Node-owned index payloads; character bytes remain borrowed from string storage.
template<
    typename Id = mbo::strings::experimental::StringId<>,
    typename Hash = std::hash<std::string_view>,
    typename Equal = std::equal_to<>,
    mbo::container::hamt::experimental::HamtOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
using HamtNodeStringIndex = HamtStringIndex<
    Id,
    Hash,
    Equal,
    Options,
    Source,
    mbo::container::hamt::experimental::HamtNodeMap<std::string_view, Id, Hash, Equal, Options, Source>>;

}  // namespace mbo::container::hamt::experimental

#endif  // MBO_CONTAINER_HAMT_EXPERIMENTAL_HAMT_NODE_STRING_INDEX_H_
