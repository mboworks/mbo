// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
// Deliberate standard-container comparison, not a production recommendation.
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/hash/hash.h"
#include "mbo/strings/container_string_index.h"
#include "mbo/strings/hamt_node_string_index.h"
#include "mbo/strings/hamt_string_index.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {

using Id = StringId<>;
using Hash = mbo::hash::DefaultHasher;

struct FoldedHash32 final {
  constexpr std::uint32_t operator()(std::string_view text) const noexcept { return Hash::GetHash32(text); }
};

static_assert(std::same_as<std::invoke_result_t<FoldedHash32, std::string_view>, std::uint32_t>);
static_assert(!mbo::hash::HasGetHash32<Hash::Algorithm>);

template<std::size_t Bits>
using FlatIndex = HamtStringIndex<Id, Hash, std::equal_to<>, mbo::container::HamtOptions{.fragment_bits = Bits}>;

template<std::size_t Bits>
using NodeIndex = HamtNodeStringIndex<Id, Hash, std::equal_to<>, mbo::container::HamtOptions{.fragment_bits = Bits}>;
using StandardIndex = ContainerStringIndex<Id, std::unordered_map<std::string_view, Id, Hash>>;
using AbseilFlatIndex = ContainerStringIndex<Id, absl::flat_hash_map<std::string_view, Id, Hash>>;
using AbseilNodeIndex = ContainerStringIndex<Id, absl::node_hash_map<std::string_view, Id, Hash>>;

template<typename Index>
using IndexRepresentation = decltype(std::declval<const Index&>().find(std::string_view{}))::value_type::value_type;

template<typename Index>
using Interner = StringInterner<
    IndexRepresentation<Index>,
    ArenaStringStorage<>,
    mbo::container::SegmentedSequence<std::string_view>,
    Index>;

template<typename Representation>
using WidthIndex = HamtStringIndex<StringId<Representation>, Hash>;

template<std::size_t Bits>
using FoldedFlatIndex =
    HamtStringIndex<Id, FoldedHash32, std::equal_to<>, mbo::container::HamtOptions{.fragment_bits = Bits}>;

template<typename Index, int HashBits>
void RecordWidths(benchmark::State& state) {
  state.counters["id_bits"] = std::numeric_limits<IndexRepresentation<Index>>::digits;
  state.counters["hash_bits"] = HashBits;
}

// Decimal IDs remain in the prefix; changing the final padding byte cannot
// collapse distinct inputs. Input generation is outside every timed loop.
std::vector<std::string> MakeInputs(std::size_t count, std::size_t length, bool embedded_nul, std::string_view prefix) {
  std::vector<std::string> inputs;
  inputs.reserve(count);
  for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
    auto text = absl::StrCat(prefix, ordinal, "/");
    text.resize(length, 'x');
    if (embedded_nul) {
      text.back() = '\0';
    }
    inputs.push_back(std::move(text));
  }
  return inputs;
}

template<typename Index>
bool Populate(benchmark::State& state, Interner<Index>& interner, std::span<const std::string> inputs) {
  const auto expected_size = interner.size() + inputs.size();
  for (const auto& text : inputs) {
    if (!interner.try_intern_id(text)) {
      state.SkipWithError("interner setup exhausted storage or IDs");
      return false;
    }
  }
  if (interner.size() != expected_size) {
    state.SkipWithError("setup inputs were not unique");
    return false;
  }
  return true;
}

template<typename Index, int HashBits>
void BmUniqueLifecycle(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const auto inputs = MakeInputs(count, length, state.range(2) != 0, "key/");
  for (auto iteration : state) {
    (void)iteration;
    Interner<Index> interner;
    if (!Populate(state, interner, inputs)) {
      return;
    }
    auto size = interner.size();
    benchmark::DoNotOptimize(size);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(count * length));
}

template<typename Index, int HashBits>
void BmDuplicate(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto inputs = MakeInputs(count, static_cast<std::size_t>(state.range(1)), state.range(2) != 0, "key/");
  Interner<Index> interner;
  if (!Populate(state, interner, inputs)) {
    return;
  }
  for (auto iteration : state) {
    (void)iteration;
    for (const auto& text : inputs) {
      auto result = interner.try_intern_id(text);
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Index>
class CascadeFixture final {
 public:
  CascadeFixture(benchmark::State& state, std::span<const std::string> inputs, std::size_t depth) {
    if (depth == 0 || inputs.size() % depth != 0) {
      state.SkipWithError("chain depth must divide the total input count");
      return;
    }
    levels_.reserve(depth);
    const auto local_count = inputs.size() / depth;
    for (std::size_t level = 0; level < depth; ++level) {
      auto owner = std::make_unique<Interner<Index>>(levels_.empty() ? nullptr : levels_.back().get());
      if (!Populate(state, *owner, inputs.subspan(level * local_count, local_count))) {
        return;
      }
      levels_.push_back(std::move(owner));
    }
    ready_ = true;
  }

  CascadeFixture(const CascadeFixture&) = delete;
  CascadeFixture& operator=(const CascadeFixture&) = delete;
  CascadeFixture(CascadeFixture&&) = delete;
  CascadeFixture& operator=(CascadeFixture&&) = delete;

  ~CascadeFixture() {
    // Do not rely on a standard container's element destruction order.
    while (!levels_.empty()) {
      levels_.pop_back();
    }
  }

  bool Ready() const noexcept { return ready_; }

  const Interner<Index>& Leaf() const noexcept { return *levels_.back(); }

  std::size_t CharacterBytesReserved() const noexcept {
    std::size_t reserved = 0;
    for (const auto& owner : levels_) {
      reserved += owner->local_character_bytes_reserved().value_or(0);
    }
    return reserved;
  }

 private:
  std::vector<std::unique_ptr<Interner<Index>>> levels_;
  bool ready_ = false;
};

template<typename Index, bool Reverse, int HashBits>
void BmCascadeMixedLookup(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const auto depth = static_cast<std::size_t>(state.range(3));
  const auto inputs = MakeInputs(count, length, state.range(2) != 0, "key/");
  const std::span<const std::string> views(inputs);
  const CascadeFixture<Index> fixture(state, views, depth);
  if (!fixture.Ready()) {
    return;
  }
  const auto local_count = count / depth;
  const auto misses = MakeInputs(local_count, length, state.range(2) != 0, "miss/");
  const auto& child = fixture.Leaf();
  std::vector<std::string_view> queries;
  queries.reserve(local_count * 3);
  for (std::size_t pos = 0; pos < local_count; ++pos) {
    queries.push_back(views.subspan(pos).front());
    queries.push_back(views.last(local_count).subspan(pos).front());
    queries.push_back(std::span<const std::string>(misses).subspan(pos).front());
  }
  for (std::size_t pos = 0; pos < queries.size(); ++pos) {
    const bool expected = pos % 3 != 2;
    const auto text = std::span<const std::string_view>(queries).subspan(pos).front();
    if (child.find(text).has_value() != expected || child.rfind(text).has_value() != expected) {
      state.SkipWithError("mixed lookup preflight disagrees with expected presence");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    for (const auto text : queries) {
      auto result = Reverse ? child.rfind(text) : child.find(text);
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(queries.size()));
  state.counters["chain_depth"] = static_cast<double>(depth);
  state.counters["strings_per_level"] = static_cast<double>(local_count);
  state.counters["character_bytes_reserved"] = static_cast<double>(fixture.CharacterBytesReserved());
}

template<typename Index, bool Reverse, bool Local, bool Missing, int HashBits>
void BmCascadeLookup(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const bool embedded_nul = state.range(2) != 0;
  const auto inputs = MakeInputs(count, length, embedded_nul, "key/");
  const std::span<const std::string> views(inputs);
  const auto depth = static_cast<std::size_t>(state.range(3));
  const CascadeFixture<Index> fixture(state, views, depth);
  if (!fixture.Ready()) {
    return;
  }
  const auto local_count = count / depth;
  const auto& child = fixture.Leaf();
  const auto misses = MakeInputs(local_count, length, embedded_nul, "miss/");
  const std::span<const std::string> queries = Missing ? std::span<const std::string>(misses)
                                               : Local ? views.last(local_count)
                                                       : views.first(local_count);
  for (const auto& text : queries) {
    if (child.find(text).has_value() == Missing || child.rfind(text).has_value() == Missing) {
      state.SkipWithError("lookup preflight disagrees with the requested hit/miss workload");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    for (const auto& text : queries) {
      auto result = [&]() {
        if constexpr (Reverse) {
          return child.rfind(text);
        } else {
          return child.find(text);
        }
      }();
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(queries.size()));
  state.counters["chain_depth"] = static_cast<double>(depth);
  state.counters["strings_per_level"] = static_cast<double>(local_count);
  state.counters["character_bytes_reserved"] = static_cast<double>(fixture.CharacterBytesReserved());
}

template<typename Index>
bool PopulateEmpty(benchmark::State& state, Interner<Index>& interner) {
  const auto result = interner.try_intern_id(std::string_view{});
  if (!result || result->value() != 0 || interner.local_character_bytes_reserved().value_or(1) != 0) {
    state.SkipWithError("empty-string preflight expected valid ID zero and no character allocation");
    return false;
  }
  return true;
}

template<typename Index, int HashBits>
void BmEmptyLifecycle(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  for (auto iteration : state) {
    (void)iteration;
    Interner<Index> interner;
    if (!PopulateEmpty(state, interner)) {
      return;
    }
    auto size = interner.size();
    benchmark::DoNotOptimize(size);
  }
  state.SetItemsProcessed(state.iterations());
}

template<typename Index, int HashBits, bool Duplicate, bool Reverse>
void BmEmptyCascade(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  Interner<Index> root;
  if (!PopulateEmpty(state, root)) {
    return;
  }
  Interner<Index> child(&root);
  const auto forward = child.find(std::string_view{});
  const auto reverse = child.rfind(std::string_view{});
  if (!forward || forward->value() != 0 || !reverse || reverse->value() != 0) {
    state.SkipWithError("empty-string cascade lookup lost parent ID zero");
    return;
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = [&]() {
      if constexpr (Duplicate) {
        return child.try_intern_id(std::string_view{});
      } else if constexpr (Reverse) {
        return child.rfind(std::string_view{});
      } else {
        return child.find(std::string_view{});
      }
    }();
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
  if (child.local_size() != 0 || child.local_character_bytes_reserved().value_or(1) != 0) {
    state.SkipWithError("empty-string parent lookups or duplicates changed child storage");
    return;
  }
  state.counters["chain_depth"] = 2;
  state.counters["character_bytes_reserved"] = 0;
}

template<typename Index, int HashBits = 64>
void RegisterIndex(std::string_view name) {
  const auto add_empty = [&](std::string_view operation, auto function) {
    const auto label = absl::StrCat("StringInterner/", name, "/", operation);
    // Google Benchmark copies the supplied name during registration.
    benchmark::RegisterBenchmark(label, function);
  };
  add_empty("EmptyLifecycle", BmEmptyLifecycle<Index, HashBits>);
  add_empty("EmptyDuplicate", BmEmptyCascade<Index, HashBits, true, false>);
  add_empty("EmptyFind", BmEmptyCascade<Index, HashBits, false, false>);
  add_empty("EmptyRfind", BmEmptyCascade<Index, HashBits, false, true>);
  const auto add = [&](std::string_view operation, auto function, bool cascade) {
    const auto label = absl::StrCat("StringInterner/", name, "/", operation);
    // Google Benchmark copies the supplied name during registration.
    auto* registered = benchmark::RegisterBenchmark(label, function);
    static constexpr auto kCounts = std::to_array<std::int64_t>({64, 1'024});
    static constexpr auto kLengths = std::to_array<std::int64_t>({16, 64, 512});
    static constexpr auto kNulModes = std::to_array<std::int64_t>({0, 1});
    static constexpr auto kDepths = std::to_array<std::int64_t>({1, 2, 8, 32});
    for (const auto count : kCounts) {
      if (std::cmp_greater(count - 1, std::numeric_limits<IndexRepresentation<Index>>::max())) {
        continue;
      }
      for (const auto length : kLengths) {
        for (const auto nul_mode : kNulModes) {
          if (cascade) {
            for (const auto depth : kDepths) {
              registered->Args({count, length, nul_mode, depth});
            }
          } else {
            registered->Args({count, length, nul_mode});
          }
        }
      }
    }
    if (cascade) {
      registered->ArgNames({"strings", "bytes", "embedded_nul", "depth"});
    } else {
      registered->ArgNames({"strings", "bytes", "embedded_nul"});
    }
  };
  add("UniqueLifecycle", BmUniqueLifecycle<Index, HashBits>, false);
  add("Duplicate", BmDuplicate<Index, HashBits>, false);
  add("FindParent", BmCascadeLookup<Index, false, false, false, HashBits>, true);
  add("FindLocal", BmCascadeLookup<Index, false, true, false, HashBits>, true);
  add("FindMiss", BmCascadeLookup<Index, false, false, true, HashBits>, true);
  add("RfindParent", BmCascadeLookup<Index, true, false, false, HashBits>, true);
  add("RfindLocal", BmCascadeLookup<Index, true, true, false, HashBits>, true);
  add("RfindMiss", BmCascadeLookup<Index, true, false, true, HashBits>, true);
  add("FindMixed", BmCascadeMixedLookup<Index, false, HashBits>, true);
  add("RfindMixed", BmCascadeMixedLookup<Index, true, HashBits>, true);
}

[[maybe_unused]] const bool kRegistered = [] {
  RegisterIndex<FlatIndex<4>>("HamtFlat4");
  RegisterIndex<FlatIndex<5>>("HamtFlat5");
  RegisterIndex<FlatIndex<6>>("HamtFlat6");
  RegisterIndex<FlatIndex<7>>("HamtFlat7");
  RegisterIndex<NodeIndex<4>>("HamtNode4");
  RegisterIndex<NodeIndex<5>>("HamtNode5");
  RegisterIndex<NodeIndex<6>>("HamtNode6");
  RegisterIndex<NodeIndex<7>>("HamtNode7");
  RegisterIndex<StandardIndex>("StdUnordered");
  RegisterIndex<AbseilFlatIndex>("AbseilFlat");
  RegisterIndex<AbseilNodeIndex>("AbseilNode");
  RegisterIndex<WidthIndex<std::uint8_t>>("HamtFlat5Id8");
  RegisterIndex<WidthIndex<std::uint16_t>>("HamtFlat5Id16");
  RegisterIndex<WidthIndex<std::uint64_t>>("HamtFlat5Id64");
  RegisterIndex<FoldedFlatIndex<4>, 32>("HamtFlat4Fold32");
  RegisterIndex<FoldedFlatIndex<5>, 32>("HamtFlat5Fold32");
  RegisterIndex<FoldedFlatIndex<6>, 32>("HamtFlat6Fold32");
  RegisterIndex<FoldedFlatIndex<7>, 32>("HamtFlat7Fold32");
  RegisterIndex<HamtNodeStringIndex<Id, FoldedHash32>, 32>("HamtNode5Fold32");
  RegisterIndex<HamtStringIndex<StringId<std::uint64_t>, FoldedHash32>, 32>("HamtFlat5Fold32Id64");
  return true;
}();

}  // namespace
}  // namespace mbo::strings

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  benchmark::Initialize(&argc, argv);
  benchmark::AddCustomContext("component", "StringInterner");
  benchmark::AddCustomContext("hash", "DefaultHasher/64-bit; Fold32 profiles XOR-fold the same 64-bit hash");
  benchmark::AddCustomContext("id_bits", "per-benchmark id_bits counter");
  benchmark::AddCustomContext("cxx_standard", "c++20");
#if defined(__clang__)
  benchmark::AddCustomContext("compiler_version", __clang_version__);
#elif defined(__GNUC__)
  benchmark::AddCustomContext("compiler_version", __VERSION__);
#endif
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
