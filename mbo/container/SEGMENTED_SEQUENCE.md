# SegmentedSequence design

This document specifies the current `mbo::container::SegmentedSequence` contract and separates it
from representation and policy experiments that remain future work.

## Purpose

`SegmentedSequence<T>` is a reusable append-oriented sequence built from separately allocated,
fixed-capacity segments. It provides stable element addresses while growing and efficient indexed
access without requiring one contiguous allocation. The string interner uses it for dense ID
metadata, but the container is an independent project deliverable.

## Current contract

- `T` is a complete, cv-unqualified, non-array object type with a non-throwing destructor.
- `SegmentedSequenceOptions` supplies up to eight segment capacities. After the listed capacities,
  growth either repeats the final capacity or stops. `maximum_size` supplies an additional bound.
- `max_size()` is the minimum of the options bound, the iterator `difference_type` limit, and the
  largest element count representable in bytes. A block source has no static size-limit API;
  runtime source exhaustion is reported by `try_*` or by the configured hard requirement.
- Existing elements, references, and iterators remain valid when another segment is acquired.
- Dense positions occupy `[0, size())`. Indexed access and iterator movement are constant time.
  Uniform capacities use direct quotient/remainder mapping. Listed capacities use a bounded
  comparison over the compile-time maximum of eight entries.
- Iterators model `random_access_iterator`. Iterator identity is stored in every build so comparing
  or subtracting iterators from different sequences can enforce its precondition consistently.
- Mutation is limited to append, suffix removal, and whole-container operations. Arbitrary
  insertion and erasure are intentionally unsupported.
- An emptied tail segment remains allocated for later append. `trim_capacity()` releases empty
  tail segments, while `clear()` destroys elements but retains all segments and `release()`
  destroys elements and returns every segment to its source.
- `capacity()` is the total capacity of the currently acquired segments. `segments()` exposes only
  their constructed prefixes as spans and omits empty tail segments.
- `pop_back()` invalidates the removed element and the old past-the-end iterator, but nothing
  referring to earlier elements. `clear()` and `release()` invalidate all element references and
  iterators. `trim_capacity()` preserves references and iterators to live elements.
- `pop_back_value()` participates only when `T` is nothrow move constructible. Its conditional
  `noexcept` follows the repository requirement policy because an empty sequence can report a
  requirement error.
- Allocation and element-construction failure restore the sequence's size, segment count, and
  capacity for append, range append, and growth through `resize()`: constructed suffix elements
  are destroyed and segments acquired by the failed operation are returned. As with standard
  sequence containers, this is not a value-level strong guarantee when constructor arguments alias
  existing elements: a throwing move can leave the source element moved from. Consuming an
  external single-pass range also cannot itself be undone.
- Copy construction uses `Source::CopyForContainer()`. `NewDeleteBlockSource` creates a fresh
  source, allocator-backed sources use `select_on_container_copy_construction`, and
  `PmrBlockSource` deliberately preserves its resource. Fixed and inline sources do not provide
  `CopyForContainer()`, so copying a sequence that uses either source is disabled.
- Moving transfers blocks and preserves element addresses when the source is movable. Embedded
  inline storage is immovable. Copy assignment and swap are available only when their explicit
  source requirements are satisfied.
- Thread safety uses external synchronization. Concurrent const access is permitted only while no
  thread mutates the sequence. The implementation adds no locks or atomics.
- The repository and API target C++23. APIs are declared `constexpr` where their operations permit
  it, but the current block sources do not provide a constant-evaluation storage path.

## Storage and lifetime

Each segment owns raw storage from a `BlockSource` and tracks the constructed prefix of its `T[]`
array. Acquisition deliberately restarts a `std::byte[]` lifetime in the returned block; the
standard implicit-object-creation rule then establishes `T[]` storage and valid array provenance
without constructing any elements. Individual `T` objects are constructed and destroyed only as
the sequence grows or shrinks. The same lifetime-establishment path runs when fixed, inline, or
allocator-backed storage is released and later reacquired.

The shared block-source concept supplies `TryAcquire`, `Release`, `max_alignment`, and whether
failure is recoverable. It intentionally does not pretend that every source has a static maximum
capacity. Standard-allocator, PMR, new/delete, caller-owned fixed, and embedded inline adapters are
provided.

## API surface

The principal C++23 surface is:

```cpp
template<SegmentedSequenceElement T,
         SegmentedSequenceOptions Options = {},
         mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
class SegmentedSequence {
 public:
  using value_type = T;
  using size_type = std::size_t;

  SegmentedSequence();
  explicit SegmentedSequence(Source source);

  template<std::input_iterator I, std::sentinel_for<I> S>
  SegmentedSequence(I first, S last);
  template<std::input_iterator I, std::sentinel_for<I> S>
  SegmentedSequence(I first, S last, Source source);
  template<std::ranges::input_range R>
  SegmentedSequence(std::from_range_t, R&& range);
  template<std::ranges::input_range R>
  SegmentedSequence(std::from_range_t, R&& range, Source source);

  bool empty() const noexcept;
  size_type size() const noexcept;
  size_type capacity() const noexcept;
  static size_type max_size() noexcept;
  void reserve(size_type n);
  void resize(size_type n);
  void resize(size_type n, const T& value);

  T& operator[](size_type pos) noexcept;
  const T& operator[](size_type pos) const noexcept;
  T& at(size_type pos);
  const T& at(size_type pos) const;
  T& front();
  const T& front() const;
  T& back();
  const T& back() const;

  template<typename... Args>
  T& emplace_back(Args&&... args);
  template<typename... Args>
  std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args);
  template<typename... Args>
  T& unchecked_emplace_back(Args&&... args);
  T& push_back(const T& value);
  T& push_back(T&& value);
  std::optional<std::reference_wrapper<T>> try_push_back(const T& value);
  std::optional<std::reference_wrapper<T>> try_push_back(T&& value);
  T& unchecked_push_back(const T& value);
  T& unchecked_push_back(T&& value);
  template<std::ranges::input_range R>
  void append_range(R&& range);

  void pop_back();
  T pop_back_value() requires std::is_nothrow_move_constructible_v<T>;
  void clear() noexcept;
  void release() noexcept;
  void trim_capacity() noexcept;
  void trim_capacity(size_type n) noexcept;

  segment_range segments() noexcept;
  const_segment_range segments() const noexcept;
};
```

`try_emplace_back()` and `try_push_back()` return a reference to the appended element or an empty
optional when a recoverable source cannot provide storage. Ordinary append uses the configured hard
requirement for storage exhaustion. The `unchecked_*` forms require `size() < capacity()` and never
acquire a segment. All overloads participate only when `T` is constructible from their arguments.
On a throwing construction, structural state rolls back; an aliased source argument follows normal
container rules and can already have been modified by the failed constructor.

Iterator-pair and `std::from_range_t` constructors default-construct the source in place, so they
also support immovable `InlineBlockSource`. Separate overloads accept a caller-supplied movable
source. Sized ranges reserve once; single-pass ranges grow as consumed. The public segment range
contains `std::span<T>` or `std::span<const T>` values in sequence order, and concatenating them is
observationally equivalent to element iteration.

## Implemented guarantees

| Area                  | Version-one guarantee                                                             |
| --------------------- | --------------------------------------------------------------------------------- |
| Segment growth        | Listed capacities, then repeat-last or bounded stop                               |
| Segment reuse         | Empty acquired tail segments reused in sequence order                             |
| Indexed complexity    | Constant time with uniform fast path or at most eight listed-boundary comparisons |
| Iterator category     | Random access, including mutable/const interoperability                           |
| Iterator identity     | Cross-sequence comparison and subtraction enforce a precondition in every build   |
| Exception guarantee   | Structural rollback; aliased throwing moves can modify an existing element        |
| Allocation            | Shared new/delete, allocator, PMR, fixed, and inline block sources                |
| Ownership             | Source-specific independent copies and conditional block-transferring moves       |
| Invalidation          | Append/trim preserve; pop invalidates removed element and old end                 |
| Capacity              | Allocation-free acquired capacity and representation/options `max_size()`         |
| Contiguous operations | Public segment-local span range                                                   |
| Thread safety         | External synchronization; no internal locks or atomics                            |

## Measurement and deferred work

The companion [measurement plan](measurements/SEGMENTED_SEQUENCE.md) covers indexed access,
iteration, append throughput, allocation and metadata cost, element size/alignment, exception
configurations, and generated code. Those measurements may motivate a later representation or
policy change without weakening the contract above.

Deferred experiments include hybrid index mapping, bounded retained-segment size classes,
retention byte/count budgets and eviction, relocating `shrink_to_fit()`, and a block source that is
usable during constant evaluation. None is part of the version-one API or its current guarantees.
