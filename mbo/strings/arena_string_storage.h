// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_STRINGS_ARENA_STRING_STORAGE_H_
#define MBO_STRINGS_ARENA_STRING_STORAGE_H_

#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>

#include "mbo/memory/arena.h"

namespace mbo::strings {

// Byte ownership only: indexing and dense-ID assignment belong to the interner.
// Checkpoints may rewind uncommitted copies; published views must never be rewound.
// NOLINTBEGIN(readability-identifier-naming): storage adapter follows STL container vocabulary.
template<typename Arena = mbo::memory::Arena<>>
class ArenaStringStorage final {
 public:
  using checkpoint_type = Arena::Checkpoint;

  ArenaStringStorage() = default;

  template<typename ArenaFactory>
  requires(std::is_nothrow_invocable_v<ArenaFactory&> && std::same_as<std::invoke_result_t<ArenaFactory&>, Arena>)
  explicit ArenaStringStorage(ArenaFactory factory) noexcept : arena_(std::invoke(factory)) {}

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

  checkpoint_type checkpoint() const noexcept { return arena_.checkpoint(); }

  void rewind(const checkpoint_type& checkpoint) noexcept { arena_.rewind(checkpoint); }

  std::size_t bytes_used() const noexcept { return arena_.bytes_used(); }

  std::size_t bytes_reserved() const noexcept { return arena_.bytes_reserved(); }

 private:
  Arena arena_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::strings

#endif  // MBO_STRINGS_ARENA_STRING_STORAGE_H_
