# SegmentedSequence design

This document specifies the planned `mbo::container::SegmentedSequence`. It separates settled
requirements from choices that still require design decisions or measurements.

## Purpose

`SegmentedSequence<T>` is a reusable append-oriented sequence built from separately allocated,
fixed-capacity segments. It provides stable element addresses while growing and efficient indexed
access without requiring one contiguous allocation. The string interner uses it for dense ID
metadata, but the container is an independent project deliverable.

Implementation and benchmarks for `SegmentedSequence` precede implementation of the string interner.

## Settled requirements

- Segment capacities are selected by a constexpr-compatible `SegmentedSequenceOptions` value.
- The options may provide a compile-time list of segment capacities.
- After that list, the options may stop at fixed capacity, repeat the final capacity, or select a
  separate growth policy.
- Uniform power-of-two segments have a specialized index-mapping fast path up to a benchmarked
  threshold.
- Only strategies and tuning parameters proven relevant by benchmarks become public API.
- Mutation is limited to append, pop, and whole-container operations. Arbitrary insertion and
  erasure are intentionally unsupported; containers such as C++26 `std::hive` address different
  stable-erasure requirements.
- Existing elements, references, and iterators remain valid when another segment is appended.
- Comparing or subtracting iterators from different sequences violates the same precondition as
  standard random-access containers. Debug builds retain enough identity to diagnose the mismatch;
  optimized builds add no identity check or representation cost.
- When popping empties a segment, that segment is retained in an unused-segment pool for later
  compatible growth. `trim_capacity()` releases retained unused segments using the C++26
  `std::hive` naming and invalidation precedent.
- Reuse and eviction are controlled by `SegmentedSequenceOptions`. A compatible retained segment
  may be reused; unusable
  or sufficiently long-unused segments may be released according to benchmark-proven limits.
  Intelligent management must not add measurable cost to append, indexed access, or iteration.
  Reclamation naturally occurs when `pop_back()` empties a segment because the sequence grows and
  shrinks only at its end. That operation may automatically retain or release the newly unused
  segment without affecting references to live elements. The policy decides whether retention is
  worthwhile; the default is selected by benchmarks rather than assuming that retaining or always
  releasing is universally best.
- Retained-segment selection accounts for waste rather than always choosing the smallest fit. An
  exact match has a fast path. A close fit within the configured waste threshold uses the smallest
  suitable segment; when every suitable segment would waste materially more, the sequence may use
  the largest suitable retained segment so subsequent appends can consume its remaining capacity.
  The threshold and whether it is absolute, proportional, or size-class based are compile-time
  options selected only after benchmarking realistic element sizes and growth patterns. Candidate
  lookup cannot scan an unbounded retained list; size classes or another bounded constant-time
  directory preserve the append guarantee.
- `trim_capacity()` deallocates only unused retained segments and preserves all pointers,
  references, and iterators to elements. An overload accepting a target capacity may retain enough
  unused segments to keep `capacity()` no lower than that target.
- Relocating compaction is deferred. If implemented, `shrink_to_fit()` follows `std::hive`: it is a
  non-binding request that may relocate live elements and invalidate pointers, references, and
  iterators. The operation exists only if measurements demonstrate value sufficient to justify
  that dangerous invalidation.
- Element construction and destruction follow normal `T` lifetime rules.
- The container exposes dense positions in `[0, size())`.
- `capacity()` is the total number of elements the sequence can hold without acquiring another
  element segment. It includes empty slots in active segments and compatible retained segments.
  `max_size()` is the hard upper bound imposed by representation, options, and block source.
- `clear()` destroys every element but retains reusable segments. `release()` destroys every
  element and returns every backing segment to its source.
- Copy operations are available when `T` and the selected block source satisfy their requirements.
  A copy owns independent storage and does not preserve source element addresses.
- A block-backed move transfers segments and preserves element addresses when the block source can
  be transferred. Inline-storage configurations are immovable when moving their embedded storage
  would invalidate addresses. Swap follows the same conditional rule.
- Segment-local spans are exposed so bulk operations can process contiguous runs without claiming
  that the entire sequence is contiguous.
- `pop_back()` destroys the last element. It invalidates references and iterators to that element
  and the old past-the-end iterator, but nothing referring to earlier elements. `clear()` and
  `release()` invalidate every iterator, pointer, and reference. `trim_capacity()` preserves all
  references and iterators to live elements.
- `pop_back_value()` moves the last element into its result and removes it. It participates only
  when `T` is nothrow move constructible, ensuring that extracting and returning the value cannot
  leave a modified element behind or lose an already removed value. This is an mbo C++20 extension;
  it does not depend on a later standard-library container.
- Indexed access is constant time for every supported options configuration. Iterators model
  `random_access_iterator`; an options configuration that cannot provide constant-time movement
  and distance is not a supported `SegmentedSequence` configuration.
- Bounded configurations detect capacity and arithmetic exhaustion before committing an element.
- `SegmentedSequenceOptions` can select eager release, unbounded retention, or independent retained
  byte-and-count limits. The default is selected only after both reference machines establish a
  useful memory/latency envelope. Retention bookkeeping must remain bounded and must not add an
  unbounded scan to append or pop.
- Thread safety uses external synchronization. Concurrent const access is permitted only while no
  thread mutates the sequence. The implementation adds no internal locks or atomics.
- The implementation is C++20 and supports constant evaluation wherever its selected storage and
  element operations permit it.

The proof implementation names the independent compile-time bounds `retained_segment_limit` and
`retained_byte_limit`. A segment is retained only while both limits permit it. Setting either limit
to zero selects eager release; setting both to `std::numeric_limits<std::size_t>::max()` selects
unbounded retention. These names and their eventual defaults remain experimental until Apple M5
Pro and Zen 5 measurements agree.

Retained segments form an ordered tail, not an unordered spare-block pool. This preserves the STL
meaning of `capacity()`: every counted slot can accept a future element without another element
allocation, and the next append consumes the nearest retained segment. When a limit is exceeded,
the farthest future segment is released first so the remaining retained prefix stays usable. A
general exact/close/largest-fit block pool belongs below the container, where an Arena or future
dynamic growth strategy can consume it without overstating sequence capacity.

## Candidate structure

Each segment owns storage for a policy-selected number of `T` objects and tracks its constructed
prefix. A directory locates segments for indexed access. The directory representation, segment
ownership, and growth allocation are separate concerns and may have independent concepts.

For a compile-time capacity list, cumulative boundaries are also compile-time values. Index mapping
can use unrolled comparisons or another generated decision structure. Uniform power-of-two segments
can instead use shifts and masks where that is faster. Hybrid policies transition between mappings
at a measured threshold.

`SegmentedSequence` and an arena may initially share a private block-allocation primitive. It
becomes public whenever measurements or implementation experience show that doing so materially
improves reuse or customization. The sequence still manages typed objects and dense indices, while
an arena provides aligned byte ranges and may use region lifetime.

## Version-one API surface

The API is C++20-compatible while following the C++26 sequence-container surface wherever C++20
can express the same contract. Later-standard facilities are precedent, not dependencies.

```cpp
template<typename T, auto Options>
class SegmentedSequence {
 public:
  using value_type = T;
  using size_type = std::size_t;

  constexpr bool empty() const noexcept;
  constexpr size_type size() const noexcept;
  constexpr size_type capacity() const noexcept;
  constexpr size_type max_size() const noexcept;
  constexpr void reserve(size_type n);
  constexpr void resize(size_type n);
  constexpr void resize(size_type n, const T& value);

  constexpr T& operator[](size_type pos);
  constexpr const T& operator[](size_type pos) const;
  constexpr T& at(size_type pos);
  constexpr const T& at(size_type pos) const;
  constexpr T& front();
  constexpr const T& front() const;
  constexpr T& back();
  constexpr const T& back() const;

  template<typename... Args>
  constexpr T& emplace_back(Args&&... args);
  template<typename... Args>
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args);
  template<typename... Args>
  constexpr T& unchecked_emplace_back(Args&&... args);
  constexpr T& push_back(const T& value);
  constexpr T& push_back(T&& value);
  constexpr std::optional<std::reference_wrapper<T>> try_push_back(const T& value);
  constexpr std::optional<std::reference_wrapper<T>> try_push_back(T&& value);
  constexpr T& unchecked_push_back(const T& value);
  constexpr T& unchecked_push_back(T&& value);
  template<std::ranges::input_range R>
  constexpr void append_range(R&& range);
  constexpr void pop_back();
  constexpr T pop_back_value() requires std::is_nothrow_move_constructible_v<T>;
  constexpr void clear();
  constexpr void release();
  constexpr void trim_capacity();
  constexpr void trim_capacity(size_type n);

  constexpr segment_range segments() noexcept;
  constexpr const_segment_range segments() const noexcept;
};
```

Following C++26 `std::inplace_vector`, `try_emplace_back()` and `try_push_back()` return an optional
reference to the appended element and an empty result when segment capacity or allocation cannot
be obtained. C++20 expresses the nullable reference directly as
`std::optional<std::reference_wrapper<T>>`; no custom result type is necessary. This representation
remains the API even when a later standard library provides `std::optional<T&>`, avoiding an API
change for a small ergonomic difference. Element-construction exceptions propagate and leave the
sequence unchanged. Ordinary append operations use the configured hard requirement when storage
cannot be obtained.

`push_back()` and `emplace_back()` return a reference to the appended element, matching the newer
`std::inplace_vector` contract. The `unchecked_*` forms have the explicit precondition
`size() < capacity()` and never acquire another segment; they are intended for callers that have
already reserved or otherwise proved capacity. `reserve(n)` ensures `capacity() >= n` without
invalidating references, pointers, or iterators to live elements.

`append_range()` accepts a C++20 input range whose reference type can construct `T`. Iterator-pair
constructors and modifiers provide the conventional C++20 surface. A `std::from_range_t` constructor
is available conditionally in C++23 builds but cannot be part of the unconditional C++20 API because
that standard-library tag does not yet exist. Range insertion is transactional only to the extent
permitted by a single-pass range: allocation or construction failure preserves all elements that
preceded the failing input unless the complete size was known and storage could be preflighted.

`resize()` follows standard sequence semantics and participates only when its requested construction
form is valid. Shrinking destroys the suffix; growing appends default-constructed or copied elements.
Allocation failure before construction leaves the sequence unchanged, and an element-construction
exception provides the standard guarantee appropriate to the selected overload.

The public segment range has mutable and const forms whose elements are respectively `std::span<T>`
and `std::span<const T>`. Each span contains only constructed elements from one active segment.
Retained empty segments are omitted, spans occur in sequence order, and concatenating them is
observationally equivalent to iterating the sequence.

Copy construction uses an independently selected block source following allocator-aware container
rules. Copy assignment, move assignment, and swap honor the standard allocator propagation traits.
When propagation or equal allocation domains permit transferring whole blocks, move and swap
preserve element addresses; otherwise they operate element-wise and apply the documented ordinary
container invalidation rules.

The initial block-source concept is shared with `Arena`. Standard-allocator, PMR, growing, and
caller-owned fixed adapters are required; each exposes its ownership, alignment, maximum-capacity,
and transfer properties through the common contract.

## Required guarantees

| Area                  | Guarantee                                                                       |
| --------------------- | ------------------------------------------------------------------------------- |
| Segment reuse         | Exact/close-fit then largest-fit selection through bounded size classes         |
| Cache eviction        | Cheap hot-path accounting, explicit maintenance, and retained byte/count limits |
| Indexed complexity    | Constant time for every supported options configuration                         |
| Iterator category     | Random access for every supported options configuration                         |
| Iterator identity     | Cross-sequence use is a precondition violation diagnosed in debug builds        |
| Exception guarantee   | Strong guarantee for allocation and element-construction failures               |
| Allocation            | Shared block-source concept with allocator, PMR, growing, and fixed adapters    |
| Ownership             | Independent copies; conditional address-preserving block moves and swaps        |
| Invalidation          | Append/trim preserve; pop invalidates removed element and old end               |
| Capacity              | Hive-style allocation-free capacity and representation/source `max_size()`      |
| Contiguous operations | Public segment-local span range                                                 |
| Constexpr             | Which policies and allocation modes are usable during constant evaluation       |
| Thread safety         | External synchronization; no internal locks or atomics                          |

## Measurements required

- indexed access across uniform, listed, and hybrid segment policies;
- iteration compared with `std::vector`, `std::deque`, and relevant Abseil containers;
- append throughput for trivial, movable, and non-trivial element types;
- allocation count, bytes retained, metadata overhead, and unused tail capacity;
- retained-segment reuse hit rate and `shrink_to_fit()` cost;
- eviction policies under bursty growth, deep pop, and changed future segment sizes;
- small, medium, and very large element sizes and alignments;
- power-of-two mapping thresholds and generated code size;
- compile-time cost and constexpr evaluation limits;
- bounded-capacity success and exhaustion paths;
- exception-enabled and exception-disabled builds;
- cache behavior for the directory and segment transitions.

## Benchmark selections

The retained-segment close-fit threshold (absolute, proportional, or size-class based) is selected by
benchmarks. This is an options default and tuning choice, not an unresolved semantic contract.

## Deferred work

- Determine from concrete use and benchmarks whether the potentially relocating, invalidating
  `shrink_to_fit()` operation established by `std::hive` is justified.
