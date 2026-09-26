// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_ID_H_
#define MBO_STRINGS_STRING_ID_H_

#include <concepts>
#include <cstdint>

#include "mbo/types/strong_id.h"

namespace mbo::strings {

template<typename T>
concept StringIdRepresentation = std::integral<T> && !std::same_as<T, bool>;

namespace strings_internal {
struct StringIdTag;
}  // namespace strings_internal

// Dense identifier, not a hash. String IDs intentionally expose the immutable-in-semantics
// ConstStrongId surface: zero is valid and the largest representation value is reserved as invalid.
template<StringIdRepresentation Representation = std::uint32_t>
using StringId = mbo::types::ConstStrongId<strings_internal::StringIdTag, Representation>;

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_ID_H_
