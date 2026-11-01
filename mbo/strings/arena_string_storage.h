// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_ARENA_STRING_STORAGE_H_
#define MBO_STRINGS_ARENA_STRING_STORAGE_H_

#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>

#include "mbo/memory/arena.h"

namespace mbo::strings {

namespace arena_string_storage_internal {
inline constexpr mbo::memory::ArenaOptions kDefaultArenaOptions{
    .initial_block_size = 4'096,
    .maximum_block_size = 65'536,
    .growth_numerator = 2,
    .growth_denominator = 1,
};
}  // namespace arena_string_storage_internal

// Byte ownership only: indexing and dense-ID assignment belong to the interner.
// NOLINTBEGIN(readability-identifier-naming): storage adapter follows STL container vocabulary.
template<
    typename Arena =
        mbo::memory::Arena<mbo::memory::NewDeleteBlockSource, arena_string_storage_internal::kDefaultArenaOptions>>
class ArenaStringStorage final {
 public:
  ArenaStringStorage() = default;
  ArenaStringStorage(const ArenaStringStorage&) = delete;
  ArenaStringStorage& operator=(const ArenaStringStorage&) = delete;
  ArenaStringStorage(ArenaStringStorage&&) = delete;
  ArenaStringStorage& operator=(ArenaStringStorage&&) = delete;
  ~ArenaStringStorage() = default;

  [[nodiscard]] std::optional<std::string_view> try_store(std::string_view input) noexcept {
    if (input.empty()) {
      return std::string_view{};
    }
    auto* const bytes = arena_.TryAllocate(input.size(), alignof(char));
    if (bytes == nullptr) {
      return std::nullopt;
    }
    auto* const data = reinterpret_cast<char*>(bytes);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    std::memcpy(data, input.data(), input.size());
    return std::string_view(data, input.size());
  }

  std::size_t bytes_used() const noexcept { return arena_.bytes_used(); }

  std::size_t bytes_reserved() const noexcept { return arena_.bytes_reserved(); }

 private:
  Arena arena_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_ARENA_STRING_STORAGE_H_
