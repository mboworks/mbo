// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_STRING_ID_H_
#define MBO_STRINGS_STRING_ID_H_

#include <concepts>
#include <cstdint>

#include "mbo/types/strong_id.h"

namespace mbo::strings {

template<typename T>
concept IsStringIdRepresentation = std::integral<T> && !std::same_as<T, bool>;

namespace strings_internal {
struct StringIdTag;
}  // namespace strings_internal

// Dense identifier, not a hash. String IDs intentionally expose the immutable-in-semantics
// ConstStrongId surface: zero is valid and the largest representation value is reserved as invalid.
template<IsStringIdRepresentation Representation = std::uint64_t, typename TagType = strings_internal::StringIdTag>
using StringId = mbo::types::ConstStrongId<TagType, Representation>;

template<typename TagType = strings_internal::StringIdTag>
using StringIdU32 = StringId<std::uint32_t, TagType>;

template<typename TagType = strings_internal::StringIdTag>
using StringIdU64 = StringId<std::uint64_t, TagType>;

template<typename Type, typename Tag = strings_internal::StringIdTag>
concept IsStringId = mbo::types::IsConstStrongId<Type, Tag> && std::same_as<Type, std::remove_cv_t<Type>>
                     && std::same_as<Type, std::remove_reference_t<Type>>;

template<typename Tag = strings_internal::StringIdTag>
concept IsStringIdU32 = IsStringId<StringId<std::uint32_t, Tag>, Tag>;

template<typename Tag = strings_internal::StringIdTag>
concept IsStringIdU64 = IsStringId<StringId<std::uint64_t, Tag>, Tag>;

}  // namespace mbo::strings

#endif  // MBO_STRINGS_STRING_ID_H_
