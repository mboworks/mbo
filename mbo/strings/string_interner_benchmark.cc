// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
// Deliberate standard-container comparison, not a production recommendation.
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/hash/hash.h"
#include "mbo/memory/block_source.h"
#include "mbo/strings/container_string_index.h"
#include "mbo/strings/hamt_node_string_index.h"
#include "mbo/strings/hamt_string_index.h"
#include "mbo/strings/string_interner.h"
#include "mbo/strings/string_interner_map.h"

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

template<
    typename Index,
    mbo::memory::ArenaOptions ArenaOptions,
    mbo::container::SegmentedSequenceOptions SequenceOptions>
struct StorageProfile final {};

template<typename Profile>
struct BenchmarkStorage final {
  using IndexType = Profile;
  using StorageType = ArenaStringStorage<>;
  using EntriesType = mbo::container::SegmentedSequence<std::string_view>;
};

template<
    typename Index,
    mbo::memory::ArenaOptions ArenaOptions,
    mbo::container::SegmentedSequenceOptions SequenceOptions>
struct BenchmarkStorage<StorageProfile<Index, ArenaOptions, SequenceOptions>> final {
  using IndexType = Index;
  using StorageType = ArenaStringStorage<mbo::memory::Arena<mbo::memory::NewDeleteBlockSource, ArenaOptions>>;
  using EntriesType = mbo::container::SegmentedSequence<std::string_view, SequenceOptions>;
};

struct BoundedCharacterProfile final {};

template<>
struct BenchmarkStorage<BoundedCharacterProfile> final {
  using IndexType = FlatIndex<5>;
  using StorageType = ArenaStringStorage<mbo::memory::Arena<mbo::memory::InlineBlockSource<4'096>>>;
  using EntriesType = mbo::container::SegmentedSequence<std::string_view>;
};

template<typename Profile>
using IndexRepresentation = decltype(std::declval<const typename BenchmarkStorage<Profile>::IndexType&>().find(
    std::string_view{}))::value_type::value_type;

template<typename Index>
using Interner = StringInterner<
    IndexRepresentation<Index>,
    typename BenchmarkStorage<Index>::StorageType,
    typename BenchmarkStorage<Index>::EntriesType,
    typename BenchmarkStorage<Index>::IndexType>;

template<typename Index>
using InternerMap = StringInternerMap<std::uint64_t, Interner<Index>>;

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

template<typename Index>
bool PopulateMap(benchmark::State& state, InternerMap<Index>& interner, std::span<const std::string> inputs) {
  const auto initial_size = interner.size();
  for (std::size_t ordinal = 0; ordinal < inputs.size(); ++ordinal) {
    const auto inserted = interner.try_emplace(inputs[ordinal], static_cast<std::uint64_t>(initial_size + ordinal));
    using Insertion = std::pair<typename InternerMap<Index>::id_type, bool>;
    const auto* const result = std::get_if<Insertion>(&inserted);
    if (result == nullptr || !result->second || result->first.value() != initial_size + ordinal) {
      state.SkipWithError("map setup exhausted storage or did not assign the expected dense ID");
      return false;
    }
  }
  return true;
}

template<typename Index, int HashBits>
void BmMapUniqueLifecycle(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const auto inputs = MakeInputs(count, length, state.range(2) != 0, "map/");
  for (auto iteration : state) {
    (void)iteration;
    InternerMap<Index> interner;
    if (!PopulateMap<Index>(state, interner, inputs)) {
      return;
    }
    benchmark::DoNotOptimize(interner.size());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(count * length));
}

template<typename Index, int HashBits>
void BmMapFindMapped(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto inputs = MakeInputs(count, static_cast<std::size_t>(state.range(1)), state.range(2) != 0, "map/");
  InternerMap<Index> interner;
  if (!PopulateMap<Index>(state, interner, inputs)) {
    return;
  }
  for (std::size_t ordinal = 0; ordinal < inputs.size(); ++ordinal) {
    const auto id = interner.find(inputs[ordinal]);
    if (!id || interner.mapped(*id) == nullptr || *interner.mapped(*id) != ordinal) {
      state.SkipWithError("map lookup preflight disagrees with the inserted mapped value");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    for (const auto& text : inputs) {
      const auto id = interner.find(text);
      const auto* const mapped = id ? interner.mapped(*id) : nullptr;
      benchmark::DoNotOptimize(mapped == nullptr ? std::uint64_t{} : *mapped);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<typename Index, int HashBits>
void BmMapIterate(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto inputs = MakeInputs(count, static_cast<std::size_t>(state.range(1)), state.range(2) != 0, "map/");
  InternerMap<Index> interner;
  if (!PopulateMap<Index>(state, interner, inputs)) {
    return;
  }
  std::uint64_t expected = 0;
  for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
    expected += ordinal + inputs[ordinal].size();
  }
  for (auto iteration : state) {
    (void)iteration;
    std::uint64_t sum = 0;
    for (const auto entry : interner) {
      sum += entry.mapped + entry.key.size();
    }
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
    if (sum != expected) {
      state.SkipWithError("map iteration disagrees with dense key/value order");
      return;
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
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
    if (!Populate<Index>(state, interner, inputs)) {
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
  if (!Populate<Index>(state, interner, inputs)) {
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

template<typename Index, bool Duplicate, StringInternError ExpectedError>
void RunCapacity(benchmark::State& state, Interner<Index>& interner, std::string_view query, std::size_t capacity) {
  const auto character_bytes_used = interner.local_character_bytes_used();
  const auto detailed = interner.intern(query);
  if constexpr (Duplicate) {
    using Result = std::pair<typename Interner<Index>::id_type, bool>;
    const auto* const inserted = std::get_if<Result>(&detailed);
    if (inserted == nullptr || inserted->first.value() != capacity - 1 || inserted->second) {
      state.SkipWithError("full-capacity duplicate returned an incorrect ID or insertion flag");
      return;
    }
  } else {
    const auto* const error = std::get_if<StringInternError>(&detailed);
    if (error == nullptr || *error != ExpectedError) {
      state.SkipWithError("capacity preflight failed for an unexpected exhaustion reason");
      return;
    }
  }
  const auto preflight = interner.try_intern_id(query);
  if (preflight.has_value() != Duplicate || interner.size() != capacity
      || interner.local_character_bytes_used() != character_bytes_used) {
    state.SkipWithError("full capacity did not preserve duplicate/failure semantics");
    return;
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = interner.try_intern_id(query);
    benchmark::DoNotOptimize(result);
  }
  if (interner.size() != capacity || interner.local_character_bytes_used() != character_bytes_used) {
    state.SkipWithError("timed capacity attempts changed size or committed character bytes");
    return;
  }
  state.counters["capacity"] = static_cast<double>(capacity);
  if (const auto bytes = interner.local_character_bytes_used(); bytes.has_value()) {
    state.counters["character_bytes_used"] = static_cast<double>(*bytes);
  }
  if (const auto bytes = interner.local_character_bytes_reserved(); bytes.has_value()) {
    state.counters["character_bytes_reserved"] = static_cast<double>(*bytes);
  }
  state.SetItemsProcessed(state.iterations());
}

template<
    typename Index,
    std::size_t Capacity,
    bool Duplicate,
    StringInternError ExpectedError = StringInternError::kIdExhausted>
void BmCapacity(benchmark::State& state) {
  RecordWidths<Index, 64>(state);
  const auto inputs = MakeInputs(Capacity, static_cast<std::size_t>(state.range(0)), state.range(1) != 0, "key/");
  const auto misses = MakeInputs(1, static_cast<std::size_t>(state.range(0)), state.range(1) != 0, "miss/");
  Interner<Index> interner;
  if (!Populate<Index>(state, interner, inputs)) {
    return;
  }
  RunCapacity<Index, Duplicate, ExpectedError>(state, interner, Duplicate ? inputs.back() : misses.front(), Capacity);
}

template<bool Duplicate>
void BmCharacterCapacity(benchmark::State& state) {
  RecordWidths<BoundedCharacterProfile, 64>(state);
  const auto length = static_cast<std::size_t>(state.range(0));
  if (length == 0) {
    state.SkipWithError("character exhaustion requires nonempty strings");
    return;
  }
  const auto inputs = MakeInputs((4'096 / length) + 1, length, state.range(1) != 0, "key/");
  Interner<BoundedCharacterProfile> interner;
  for (const auto& text : inputs) {
    const auto result = interner.intern(text);
    if (const auto* error = std::get_if<StringInternError>(&result); error != nullptr) {
      if (*error != StringInternError::kCharacterStorageExhausted) {
        state.SkipWithError("bounded character setup exhausted an unrelated resource");
        return;
      }
      break;
    }
  }
  const auto capacity = interner.size();
  if (capacity == 0 || capacity == inputs.size()) {
    state.SkipWithError("bounded character setup did not reach a populated exhaustion state");
    return;
  }
  RunCapacity<BoundedCharacterProfile, Duplicate, StringInternError::kCharacterStorageExhausted>(
      state, interner, inputs.at(Duplicate ? capacity - 1 : capacity), capacity);
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
      if (!Populate<Index>(state, *owner, inputs.subspan(level * local_count, local_count))) {
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

  Interner<Index>& Root() noexcept { return *levels_.front(); }

  std::size_t CharacterBytesReserved() const noexcept {
    std::size_t reserved = 0;
    for (const auto& owner : levels_) {
      reserved += owner->local_character_bytes_reserved().value_or(0);
    }
    return reserved;
  }

  void RecordStorageCounters(benchmark::State& state) const {
    // All registered profiles use SegmentedSequence, which supplies these
    // reservation measurements. Index and allocator overhead are not included.
    std::size_t live_descriptors = 0;
    std::size_t reserved_descriptors = 0;
    std::size_t lookup_directory = 0;
    std::size_t segment_directory = 0;
    for (const auto& owner : levels_) {
      const auto diagnostics = owner->local_entry_storage_diagnostics();
      live_descriptors += diagnostics.live_descriptor_bytes;
      reserved_descriptors += diagnostics.segment_bytes_reserved.value_or(0);
      lookup_directory += diagnostics.lookup_directory_bytes_reserved.value_or(0);
      segment_directory += diagnostics.segment_directory_bytes_reserved.value_or(0);
    }
    state.counters["descriptor_bytes_live"] = static_cast<double>(live_descriptors);
    state.counters["descriptor_bytes_reserved"] = static_cast<double>(reserved_descriptors);
    state.counters["lookup_directory_bytes_reserved"] = static_cast<double>(lookup_directory);
    state.counters["segment_directory_bytes_reserved"] = static_cast<double>(segment_directory);
  }

 private:
  std::vector<std::unique_ptr<Interner<Index>>> levels_;
  bool ready_ = false;
};

template<typename Index, bool Reverse, int HashBits>
void BmLateParentLookup(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const auto depth = static_cast<std::size_t>(state.range(3));
  const auto inputs = MakeInputs(count, length, state.range(2) != 0, "key/");
  CascadeFixture<Index> fixture(state, inputs, depth);
  if (!fixture.Ready()) {
    return;
  }
  const auto local_count = count / depth;
  const auto queries = MakeInputs(local_count, length, state.range(2) != 0, "late/");
  if (!Populate<Index>(state, fixture.Root(), queries)) {
    return;
  }
  const auto& child = fixture.Leaf();
  const auto expected_size = depth == 1 ? count + local_count : count;
  if (child.size() != expected_size || fixture.Root().size() != 2 * local_count) {
    state.SkipWithError("late parent growth changed the wrong visible size");
    return;
  }
  for (std::size_t pos = 0; pos < queries.size(); ++pos) {
    const auto& text = queries.at(pos);
    const auto forward = child.find(text);
    const auto reverse = child.rfind(text);
    if (forward.has_value() != (depth == 1) || reverse.has_value() != (depth == 1)) {
      state.SkipWithError("late parent growth violated the captured child cutoff");
      return;
    }
    if (forward.has_value() && reverse.has_value()
        && (forward->value() != local_count + pos || reverse->value() != local_count + pos)) {
      state.SkipWithError("root-only late insertion returned an incorrect dense ID");
      return;
    }
  }
  fixture.RecordStorageCounters(state);
  for (auto iteration : state) {
    (void)iteration;
    for (const auto& text : queries) {
      auto result = Reverse ? child.rfind(text) : child.find(text);
      benchmark::DoNotOptimize(result);
    }
  }
  state.counters["chain_depth"] = static_cast<double>(depth);
  state.counters["late_parent_strings"] = static_cast<double>(local_count);
  state.counters["visible_strings"] = static_cast<double>(child.size());
  state.counters["hidden_parent_growth"] = depth == 1 ? 0 : 1;
  state.counters["character_bytes_reserved"] = static_cast<double>(fixture.CharacterBytesReserved());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(queries.size()));
}

template<typename Index, bool Reverse, int HashBits>
void BmCascadeIteration(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto depth = static_cast<std::size_t>(state.range(3));
  const auto inputs = MakeInputs(count, static_cast<std::size_t>(state.range(1)), state.range(2) != 0, "key/");
  const CascadeFixture<Index> fixture(state, inputs, depth);
  if (!fixture.Ready()) {
    return;
  }
  const auto& leaf = fixture.Leaf();
  fixture.RecordStorageCounters(state);
  std::size_t ordinal = 0;
  for (const auto text : leaf) {
    if (text != inputs.at(ordinal++)) {
      state.SkipWithError("cascade iteration did not preserve dense-ID order");
      return;
    }
  }
  if (ordinal != count) {
    state.SkipWithError("cascade iteration omitted visible strings");
    return;
  }
  for (auto iterator = leaf.rbegin(); iterator != leaf.rend(); ++iterator) {
    if (ordinal == 0) {
      state.SkipWithError("reverse cascade iteration produced extra strings");
      return;
    }
    --ordinal;
    if (*iterator != inputs.at(ordinal)) {
      state.SkipWithError("reverse cascade iteration did not preserve reverse dense-ID order");
      return;
    }
  }
  if (ordinal != 0) {
    state.SkipWithError("reverse cascade iteration omitted visible strings");
    return;
  }
  for (auto iteration : state) {
    (void)iteration;
    if constexpr (Reverse) {
      for (auto iterator = leaf.rbegin(); iterator != leaf.rend(); ++iterator) {
        auto text = *iterator;
        benchmark::DoNotOptimize(text);
      }
    } else {
      for (auto text : leaf) {
        benchmark::DoNotOptimize(text);
      }
    }
  }
  state.counters["chain_depth"] = static_cast<double>(depth);
  state.counters["character_bytes_reserved"] = static_cast<double>(fixture.CharacterBytesReserved());
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

struct StringSizeTotals final {
  void operator()(std::size_t length) noexcept {
    ++strings;
    bytes += length;
  }

  std::size_t strings = 0;
  std::size_t bytes = 0;
};

template<typename Index, int HashBits>
void BmStringSizeDiagnostics(benchmark::State& state) {
  RecordWidths<Index, HashBits>(state);
  const auto count = static_cast<std::size_t>(state.range(0));
  const auto length = static_cast<std::size_t>(state.range(1));
  const auto depth = static_cast<std::size_t>(state.range(3));
  const auto inputs = MakeInputs(count, length, state.range(2) != 0, "key/");
  const CascadeFixture<Index> fixture(state, inputs, depth);
  if (!fixture.Ready()) {
    return;
  }
  auto* child = std::addressof(fixture.Leaf());
  StringSizeTotals preflight;
  child->visit_string_sizes(preflight);
  if (preflight.strings != count || preflight.bytes != count * length) {
    state.SkipWithError("string-size diagnostic visitor lost visible strings or bytes");
    return;
  }
  fixture.RecordStorageCounters(state);
  benchmark::DoNotOptimize(child);
  for (auto iteration : state) {
    (void)iteration;
    benchmark::ClobberMemory();
    StringSizeTotals totals;
    child->visit_string_sizes(totals);
    benchmark::DoNotOptimize(totals);
  }
  state.counters["chain_depth"] = static_cast<double>(depth);
  state.counters["string_bytes_reported"] = static_cast<double>(preflight.bytes);
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

template<
    typename Index,
    bool Reverse,
    int HashBits,
    bool Shuffled = false,
    std::size_t RootWeight = 1,
    std::size_t LocalWeight = 1,
    std::size_t MissWeight = 1>
void BmCascadeMixedLookup(benchmark::State& state) {
  static_assert(RootWeight > 0 && LocalWeight > 0 && MissWeight > 0);
  constexpr std::size_t kQueriesPerString = RootWeight + LocalWeight + MissWeight;
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
  fixture.RecordStorageCounters(state);
  std::vector<std::string_view> queries;
  queries.reserve(local_count * kQueriesPerString);
  for (std::size_t pos = 0; pos < local_count; ++pos) {
    for (std::size_t repeat = 0; repeat < RootWeight; ++repeat) {
      queries.push_back(views.subspan(pos).front());
    }
    for (std::size_t repeat = 0; repeat < LocalWeight; ++repeat) {
      queries.push_back(views.last(local_count).subspan(pos).front());
    }
    for (std::size_t repeat = 0; repeat < MissWeight; ++repeat) {
      queries.push_back(std::span<const std::string>(misses).subspan(pos).front());
    }
  }
  for (std::size_t pos = 0; pos < queries.size(); ++pos) {
    const auto query_class = pos % kQueriesPerString;
    const bool expected = query_class < RootWeight + LocalWeight;
    const auto text = std::span<const std::string_view>(queries).subspan(pos).front();
    const auto forward = child.find(text);
    const auto reverse = child.rfind(text);
    if (forward.has_value() != expected || reverse.has_value() != expected) {
      state.SkipWithError("mixed lookup preflight disagrees with expected presence");
      return;
    }
    if (forward.has_value() && reverse.has_value()) {
      const auto ordinal = pos / kQueriesPerString;
      const auto expected_id = query_class < RootWeight ? ordinal : count - local_count + ordinal;
      if (forward->value() != expected_id || reverse->value() != expected_id) {
        state.SkipWithError("mixed lookup preflight returned an incorrect dense ID");
        return;
      }
    }
  }
  if constexpr (Shuffled) {
    std::mt19937 generator(0x4d424f);
    std::shuffle(queries.begin(), queries.end(), generator);
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
  state.counters["queries_per_iteration"] = static_cast<double>(queries.size());
  state.counters["root_query_fraction"] = static_cast<double>(RootWeight) / kQueriesPerString;
  state.counters["local_query_fraction"] = static_cast<double>(LocalWeight) / kQueriesPerString;
  state.counters["miss_query_fraction"] = static_cast<double>(MissWeight) / kQueriesPerString;
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
  fixture.RecordStorageCounters(state);
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

template<typename InternerType>
bool PopulateEmpty(benchmark::State& state, InternerType& interner) {
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
  add("FindMixedShuffled", BmCascadeMixedLookup<Index, false, HashBits, true>, true);
  add("RfindMixedShuffled", BmCascadeMixedLookup<Index, true, HashBits, true>, true);
  add("FindRootHeavyShuffled", BmCascadeMixedLookup<Index, false, HashBits, true, 8, 1, 1>, true);
  add("RfindRootHeavyShuffled", BmCascadeMixedLookup<Index, true, HashBits, true, 8, 1, 1>, true);
  add("FindLocalHeavyShuffled", BmCascadeMixedLookup<Index, false, HashBits, true, 1, 8, 1>, true);
  add("RfindLocalHeavyShuffled", BmCascadeMixedLookup<Index, true, HashBits, true, 1, 8, 1>, true);
  add("FindMissHeavyShuffled", BmCascadeMixedLookup<Index, false, HashBits, true, 1, 1, 8>, true);
  add("RfindMissHeavyShuffled", BmCascadeMixedLookup<Index, true, HashBits, true, 1, 1, 8>, true);
  add("Iterate", BmCascadeIteration<Index, false, HashBits>, true);
  add("ReverseIterate", BmCascadeIteration<Index, true, HashBits>, true);
  add("VisitStringSizes", BmStringSizeDiagnostics<Index, HashBits>, true);
  add("FindLateParent", BmLateParentLookup<Index, false, HashBits>, true);
  add("RfindLateParent", BmLateParentLookup<Index, true, HashBits>, true);
}

template<typename Index, int HashBits = 64>
void RegisterMapIndex(std::string_view name) {
  const auto add = [&](std::string_view operation, auto function) {
    const auto label = absl::StrCat("StringInternerMap/", name, "/", operation);
    auto* registered = benchmark::RegisterBenchmark(label.c_str(), function);
    for (const auto count : std::to_array<std::int64_t>({64, 1'024})) {
      for (const auto length : std::to_array<std::int64_t>({16, 64, 512})) {
        for (const auto nul_mode : std::to_array<std::int64_t>({0, 1})) {
          registered->Args({count, length, nul_mode});
        }
      }
    }
    registered->ArgNames({"strings", "bytes", "embedded_nul"});
  };
  add("UniqueLifecycle", BmMapUniqueLifecycle<Index, HashBits>);
  add("FindMapped", BmMapFindMapped<Index, HashBits>);
  add("Iterate", BmMapIterate<Index, HashBits>);
}

[[maybe_unused]] const bool kRegistered = [] {
  // Google Benchmark copies names supplied through its C-string API.
  auto* exhausted = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Id8/IdExhaustion", BmCapacity<WidthIndex<std::uint8_t>, 256, false>);
  auto* duplicate = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Id8/DuplicateAtIdCapacity", BmCapacity<WidthIndex<std::uint8_t>, 256, true>);
  using BoundedEntries = StorageProfile<
      FlatIndex<5>, {}, mbo::container::SegmentedSequenceOptions{.segment_capacities = {64}, .maximum_size = 64}>;
  auto* entries_exhausted = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Entries64Bounded/EntryExhaustion",
      BmCapacity<BoundedEntries, 64, false, StringInternError::kEntryStorageExhausted>);
  auto* entries_duplicate = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Entries64Bounded/DuplicateAtEntryCapacity", BmCapacity<BoundedEntries, 64, true>);
  auto* characters_exhausted = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Arena4096Bounded/CharacterExhaustion", BmCharacterCapacity<false>);
  auto* characters_duplicate = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Arena4096Bounded/DuplicateAtCharacterCapacity", BmCharacterCapacity<true>);
  using BoundedIndex =
      HamtStringIndex<Id, Hash, std::equal_to<>, mbo::container::HamtOptions{.fragment_bits = 5, .maximum_size = 64}>;
  auto* index_exhausted = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Index64Bounded/IndexExhaustion",
      BmCapacity<BoundedIndex, 64, false, StringInternError::kIndexExhausted>);
  auto* index_duplicate = benchmark::RegisterBenchmark(
      "StringInterner/HamtFlat5Index64Bounded/DuplicateAtIndexCapacity", BmCapacity<BoundedIndex, 64, true>);
  for (const auto length : std::to_array<std::int64_t>({16, 64, 512})) {
    for (const auto nul_mode : std::to_array<std::int64_t>({0, 1})) {
      exhausted->Args({length, nul_mode});
      duplicate->Args({length, nul_mode});
      entries_exhausted->Args({length, nul_mode});
      entries_duplicate->Args({length, nul_mode});
      characters_exhausted->Args({length, nul_mode});
      characters_duplicate->Args({length, nul_mode});
      index_exhausted->Args({length, nul_mode});
      index_duplicate->Args({length, nul_mode});
    }
  }
  exhausted->ArgNames({"bytes", "embedded_nul"});
  duplicate->ArgNames({"bytes", "embedded_nul"});
  entries_exhausted->ArgNames({"bytes", "embedded_nul"});
  entries_duplicate->ArgNames({"bytes", "embedded_nul"});
  characters_exhausted->ArgNames({"bytes", "embedded_nul"});
  characters_duplicate->ArgNames({"bytes", "embedded_nul"});
  index_exhausted->ArgNames({"bytes", "embedded_nul"});
  index_duplicate->ArgNames({"bytes", "embedded_nul"});
  RegisterIndex<FlatIndex<4>>("HamtFlat4");
  RegisterIndex<FlatIndex<5>>("HamtFlat5");
  RegisterIndex<StorageProfile<FlatIndex<5>, mbo::memory::ArenaOptions{.initial_block_size = 512}, {}>>(
      "HamtFlat5Arena512");
  RegisterIndex<StorageProfile<FlatIndex<5>, mbo::memory::ArenaOptions{.initial_block_size = 16'384}, {}>>(
      "HamtFlat5Arena16384");
  RegisterIndex<StorageProfile<FlatIndex<5>, {}, mbo::container::SegmentedSequenceOptions{.segment_capacities = {64}}>>(
      "HamtFlat5Entries64");
  RegisterIndex<
      StorageProfile<FlatIndex<5>, {}, mbo::container::SegmentedSequenceOptions{.segment_capacities = {1'024}}>>(
      "HamtFlat5Entries1024");
  RegisterIndex<FlatIndex<6>>("HamtFlat6");
  RegisterIndex<FlatIndex<7>>("HamtFlat7");
  RegisterIndex<NodeIndex<4>>("HamtNode4");
  RegisterIndex<NodeIndex<5>>("HamtNode5");
  RegisterIndex<NodeIndex<6>>("HamtNode6");
  RegisterIndex<NodeIndex<7>>("HamtNode7");
  RegisterIndex<StandardIndex>("StdUnordered");
  RegisterIndex<AbseilFlatIndex>("AbseilFlat");
  RegisterIndex<AbseilNodeIndex>("AbseilNode");
  RegisterMapIndex<FlatIndex<5>>("HamtFlat5");
  RegisterMapIndex<NodeIndex<5>>("HamtNode5");
  RegisterMapIndex<StandardIndex>("StdUnordered");
  RegisterMapIndex<AbseilFlatIndex>("AbseilFlat");
  RegisterMapIndex<AbseilNodeIndex>("AbseilNode");
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
