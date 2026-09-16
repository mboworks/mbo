// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_KEY_OF_H_
#define MBO_CONTAINER_INTERNAL_HAMT_KEY_OF_H_

#include <utility>

namespace mbo::container::container_internal {

// Source-independent extractor identities let cloned cores use another source.
template<typename Key>
struct HamtIdentityKey final {
  // The HAMT owns every argument passed here, so the returned reference cannot
  // outlive its key. The generic lint cannot infer that container contract.
  // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
  constexpr const Key& operator()(const Key& key) const noexcept { return key; }
};

template<typename Key, typename Mapped>
struct HamtPairKey final {
  // The HAMT owns every argument passed here, so the returned reference cannot
  // outlive its entry. The generic lint cannot infer that container contract.
  // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
  constexpr const Key& operator()(const std::pair<const Key, Mapped>& entry) const noexcept { return entry.first; }
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_KEY_OF_H_
