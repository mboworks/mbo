# SegmentedSequence design

This document specifies the current `mbo::container::SegmentedSequence` contract. The container is
an append-oriented sequence with stable element addresses, fixed-capacity segments, and constant-time
indexed access.

## Purpose

`SegmentedSequence<T>` avoids relocating existing elements when it grows. It is useful for dense ID
metadata and other append-heavy storage where references must remain valid, without requiring one
contiguous allocation for every element.

## Current contract

- `T` is a complete, cv-unqualified, non-array object type with a non-throwing destructor.
- `SegmentedSequenceOptions::segment_size` is the power-of-two element count in every segment and is
  part of the segment type. `segment_capacity` is the maximum number of segment slots: a finite value
  is a nonzero power of two, while `SIZE_MAX` selects growth up to the private representation limit.
  `segment_reservation` is the initial directory reservation; it is zero or a power of two, cannot
  exceed a finite capacity, and defaults to one.
- A finite `segment_size * segment_capacity` product must fit the iterator and element-count
  representation limits. It is a private validation/growth bound, not a third public capacity API.
  A block source has no static size-limit API; runtime source exhaustion is reported by `try_*` or by
  the configured hard requirement.
- A dense position maps to a directory index and slot index with a shift and mask. Iterators model
  `random_access_iterator`; indexed access and iterator movement are constant time.
- Existing elements, references, and iterators remain valid when the directory or sequence grows.
  Directory reallocation moves only `Segment*` entries. It never moves a segment header or element.
- Mutation is limited to append, suffix removal, and whole-container operations. Arbitrary insertion
  and erasure are intentionally unsupported.
- An emptied tail segment remains allocated for later append. `trim_capacity()` releases empty tail
  segments, `clear()` destroys elements but retains segments, and `release()` destroys elements and
  returns every segment to its source.
- `capacity()` is the number of element slots in currently acquired full segments. Growth stops at
  the hard `segment_capacity` segment bound even when the source and directory allocator could keep
  allocating.
- `segments()` exposes non-contiguous random-access views of constructed element prefixes and omits
  empty tail segments. A segment contains an array of `Slot` union objects rather than a `T[]`, so the
  API deliberately does not expose `data()` or `std::span<T>`.
- `pop_back()` invalidates the removed element and old past-the-end iterator, but nothing referring
  to earlier elements. `clear()` and `release()` invalidate all element references and iterators.
  `trim_capacity()` preserves references and iterators to live elements.
- `pop_back_value()` participates only when `T` is nothrow move constructible. Its conditional
  `noexcept` follows the repository requirement policy because an empty sequence can report a
  requirement error.
- Append, range append, and `resize()` roll back constructed suffix elements and newly acquired
  segments after allocation or construction failure. A successful directory reserve may remain.
  As with standard containers, an aliased argument whose throwing move already modified an existing
  element does not receive a value-level strong guarantee. Consuming an external single-pass range
  also cannot itself be undone.
- Thread safety uses external synchronization. Concurrent const access is permitted only while no
  thread mutates the sequence. The implementation adds no locks or atomics.

## Storage, allocation, and lifetime

Each segment is one complete typed object containing its source `MemoryBlock`, constructed size,
and `std::array<Slot, segment_size>`. Runtime growth requests exactly one raw
block of `sizeof(Segment)` and `alignof(Segment)` from the `BlockSource`, then placement-constructs the
segment in that block. Individual `T` lifetimes begin and end in inactive union slots through
`construct_at` and `destroy_at`. Releasing a segment destroys its typed header before returning the
original block to the source.

The separate flat directory is `std::vector<Segment*>`. Its allocator is the fourth template
parameter, `DirectoryAllocator`, rebound internally to `Segment*`. Consequently, ordinary segment
growth is one source allocation except when the directory also crosses a reserve threshold. The
three options make that behavior explicit:

- `segment_reservation == 0` keeps empty construction allocation-free.
- The default reservation of one requests one pointer slot during construction, so default
  construction is potentially throwing.
- `segment_reservation == segment_capacity` for a finite configuration requests the whole directory
  once; every later segment growth then performs only its source allocation.
- Intermediate reservations grow with power-of-two requests up to the hard capacity. `std::vector`
  may reserve more than requested, so its reported capacity is not promised to equal a request.

The container is allocator-aware for its directory: it defines `allocator_type`, `get_allocator()`,
and leading `allocator_arg_t` constructors. Copy construction applies
`select_on_container_copy_construction`. Copy assignment retains the destination directory allocator.
It is disabled for allocators that request `propagate_on_container_copy_assignment`, because changing
the directory allocator independently of the block-owning source would not be transactional.
Move assignment and swap preserve unequal non-propagating allocators by first building replacement
pointer directories; they never invoke `vector::swap` with unequal non-propagating allocators.
Their participation and `noexcept` specifications also account for the `Source` operations.

Segment storage and directory storage are independent choices. They can use different resources, or
a `PmrBlockSource` and `pmr::polymorphic_allocator` can direct both to the same memory resource.
Fixed and inline block sources can own segment storage, but the pointer directory still uses its
configured allocator.

## Constant evaluation

The default standard-allocator configuration supports real C++23 constant evaluation. During
constant evaluation, the implementation allocates a typed `Segment` with `std::allocator<Segment>`;
this avoids forbidden constant-expression casts from raw bytes. Destruction uses the matching typed
deallocation. Runtime evaluation continues to use the configured `BlockSource`.

The test suite statically evaluates construction, growth across multiple segments, indexed access,
copy construction, suffix removal, clearing, and destruction. The typed growth fallback is restricted
to `NewDeleteBlockSource`: using it for another source would bypass that source's exhaustion and
observable state. Only source construction, copying, and destruction required by the selected
operation occur during constant evaluation; segment acquisition does not call the source. A custom
directory allocator must itself be constant-expression capable. `bytes_reserved()` counts
`sizeof(Segment)` for each constant-evaluation segment, matching the runtime meaning of
source-reported reserved bytes.

## API surface

The principal C++23 surface is:

```cpp
template<SegmentedSequenceElement T,
         SegmentedSequenceOptions Options = {},
         mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
         typename DirectoryAllocator = std::allocator<std::byte>>
class SegmentedSequence {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using allocator_type = DirectoryAllocator;

  SegmentedSequence();
  template<typename SourceArg>
    requires std::same_as<std::remove_cvref_t<SourceArg>, Source>
             && std::constructible_from<Source, SourceArg&&>
  explicit SegmentedSequence(SourceArg&& source);
  SegmentedSequence(std::allocator_arg_t, const allocator_type& allocator);
  template<typename SourceArg>
    requires std::same_as<std::remove_cvref_t<SourceArg>, Source>
             && std::constructible_from<Source, SourceArg&&>
  SegmentedSequence(std::allocator_arg_t, const allocator_type& allocator, SourceArg&& source);

  template<std::input_iterator I, std::sentinel_for<I> S>
  SegmentedSequence(I first, S last);
  template<std::ranges::input_range R>
  SegmentedSequence(std::from_range_t, R&& range);
  // Source-taking and allocator-extended iterator/range overloads are also provided.

  allocator_type get_allocator() const;
  bool empty() const noexcept;
  size_type size() const noexcept;
  size_type capacity() const noexcept;
  size_type segment_count() const noexcept;
  size_type bytes_reserved() const noexcept;

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

Iterator-pair and `std::from_range_t` constructors default-construct the source in place, so they
also support immovable `InlineBlockSource`. Constrained forwarding overloads accept a
caller-supplied source in the direct, allocator-extended, iterator-pair, and `from_range` forms.
They copy an lvalue source and move an rvalue source without passing an over-aligned source through
a by-value parameter. Allocator-extended forms control the directory independently. Sized ranges
reserve once; single-pass ranges grow as consumed.

## Implemented guarantees

| Area                  | Current guarantee                                                                |
| --------------------- | -------------------------------------------------------------------------------- |
| Segment growth        | Every segment has the fixed compile-time power-of-two `segment_size`             |
| Segment bound         | `segment_capacity` is a hard maximum number of segment slots                     |
| Segment allocation    | One BlockSource allocation co-locates typed header and element slots             |
| Directory             | Stable pointers; configurable initial reserve, then power-of-two growth requests |
| Segment reuse         | Empty acquired tail segments reused in sequence order                            |
| Indexed complexity    | Constant-time shift/mask mapping                                                 |
| Iterator category     | Random access, including mutable/const interoperability                          |
| Iterator identity     | Cross-sequence comparison and subtraction enforce a precondition in every build  |
| Exception guarantee   | Structural rollback; aliased throwing moves can modify an existing element       |
| Allocation            | Shared new/delete, allocator, PMR, fixed, and inline block sources               |
| Ownership             | Source-specific copies plus allocator-aware directory copy/move/swap             |
| Invalidation          | Append/trim preserve; pop invalidates removed element and old end                |
| Constant evaluation   | Typed standard-allocator path is statically exercised                            |
| Contiguous operations | Random-access segment views; no `T*`/span claim                                  |
| Thread safety         | External synchronization; no internal locks or atomics                           |

## Measurement and deferred work

The companion [measurement plan](measurements/SEGMENTED_SEQUENCE.md) covers indexed access,
iteration, append throughput, allocation counts and bytes, directory-growth boundaries, element
size/alignment, exception configurations, and generated code. Growth-boundary latency must report
tail behavior as well as aggregate throughput because an unbounded directory threshold adds a
second allocation and pointer copy to that append.

The generic heterogeneous-capacity implementation merged in
[pull request 443](https://github.com/mboworks/mbo/pull/443) at
[`12bf51f8fdcceb2c452442a0287f332263a34680`](https://github.com/mboworks/mbo/commit/12bf51f8fdcceb2c452442a0287f332263a34680)
remains historical provenance and a comparison baseline. The current API intentionally chooses the
single flat pointer directory and fixed power-of-two segments. Embedded jump tables, alternative
directories, retained segment budgets, and relocating `shrink_to_fit()` remain future experiments
rather than parallel public modes.
