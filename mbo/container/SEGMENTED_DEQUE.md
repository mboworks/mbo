# SegmentedDeque design

This document specifies `mbo::container::SegmentedDeque`: a double-ended sequence with
fixed-capacity segments, constant-time indexed access, and stable element addresses. It is a
separate public container from [`SegmentedVector`](SEGMENTED_VECTOR.md). Both support random-access
iteration; the deque additionally supports insertion and removal at the front. The old
`SegmentedSequence` name remains historical provenance and is not reused.

## Purpose

`SegmentedDeque<T>` serves queues, sliding windows, and sequences that grow at both ends without
relocating existing elements. Segment storage is supplied through the same `BlockSource` contract
as `SegmentedVector`, independently of the pointer-directory allocator. Empty segments are retained
and reused at either end, including when the source is an arena or monotonic memory resource.

The vector keeps its zero-origin, shift/mask indexing and existing representation. The deque's
additional origin and directory masking costs are measured against the vector and `std::deque`.
No claim that those costs are negligible is part of the contract.

## Current contract

- `T` is a complete, cv-unqualified, non-array object type with a non-throwing destructor.
- `SegmentedOptions::segment_size` is a nonzero power-of-two element count, default 256.
  `segment_capacity` is the maximum number of acquired segments, including empty retained segments.
  It is a nonzero power of two, or `SIZE_MAX` for the private representation bound.
  `segment_reservation` is the initial directory slot count: zero or a power of two, default one,
  and no greater than a finite segment capacity. Reservation alone acquires no element segments.
- Finite capacity products must fit element-count and iterator-difference bounds. Actual source
  exhaustion remains a runtime condition. There is no assumption that a source advertises a bound.
- Indexed access, iterator arithmetic, and distance are constant time. Growth at either end does
  not relocate existing elements. There is no whole-container contiguous-storage promise.
- Mutation supports insertion at either end, prefix/suffix removal, range insertion at either end,
  and whole-container operations. Arbitrary middle insertion and erasure are unsupported.
- Existing element references, pointers, and iterators survive insertion at either end, directory
  growth, reservation, and trimming empty segments. Removal invalidates only the removed elements'
  references and iterators. Every size-changing mutation invalidates the old past-the-end iterator.
  `clear()` and `release()` invalidate all element iterators and references. Move and swap invalidate
  container-owned iterators; references to transferred elements remain valid.
- `pop_front_value()` and `pop_back_value()` implement the shared
  [value-pop contract](README.md#value-returning-pops): one move constructs the return value before
  removal. A failed move preserves size and element lifetimes, but may alter the source value.
  Both participate for move-constructible elements; `noexcept` accounts for the move and requirement
  policy. They retain acquired segment capacity, as do the corresponding void pops.
- Segment ranges and views describe a snapshot of the live segment intervals. Structural mutation
  invalidates them, including an endpoint change inside an existing segment.
- `try_*` insertion returns an empty optional for recoverable segment-source exhaustion or the hard
  segment bound. It does not suppress exceptions from element constructors or directory allocation.
  Ordinary insertion reports storage exhaustion through the configured requirement policy.
- Single insertion constructs the new element before publishing the new origin or size. Failure
  leaves existing values and iterator identities intact, except for an explicitly moved aliased
  argument whose constructor already modified its source. A successfully grown directory may remain.
- Range insertion and growing `resize()` roll back newly constructed elements and acquired capacity
  on failure. The identity of retained empty segments may change. They cannot undo consumption of a single-pass input range or a throwing move
  from an aliased existing element. Prepending preserves input order.
- Thread safety requires external synchronization. Concurrent const operations are permitted only
  while no thread mutates the deque. There are no internal locks or atomics.

## Representation and indexing

The directory uses the public experimental
[`experimental::CircularBuffer`](experimental/CIRCULAR_BUFFER.md) from
`experimental/circular_buffer.h`. It is replaceable without changing the public deque API
or shared options and is not used by `SegmentedVector`.

The circular buffer owns the live segment-pointer range through separate begin and end indices,
with power-of-two allocated capacity. Adding or recycling a live segment pushes or pops the
corresponding buffer end. Only live pointers are constructed; spare segments stay on their separate
intrusive list. A wrapping unsigned element coordinate identifies the deque's first element.
For a valid logical index `i`, the mapping is conceptually:

```cpp
offset = (origin & segment_mask) + i;
segment = directory[offset >> segment_shift];
element_slot = offset & segment_mask;
```

Unsigned wrap is intentional. The circular buffer maps logical indices to its wrapped allocation;
its live interval never overlaps itself. Growing the directory allocates a larger pointer array
and relocates live pointers in logical order; it never moves segment storage. This
event is linear in the live segment count. Endpoint insertion is amortized constant time with respect
to directory management, plus the source's allocation and the element's construction costs.
Preallocating the directory removes directory-growth events within its reserved limit.

An iterator stores its owning container and an unsigned element coordinate. Front insertion changes
the container origin without changing surviving elements' coordinates. Iterator distance and
ordering use positions relative to the current origin, so crossing unsigned zero is well-defined;
the implementation must not subtract unrelated signed absolute coordinates. Cross-container
comparison and subtraction enforce a precondition. Removed iterators never become valid again by
coincidental coordinate reuse.

Each segment contains its original `MemoryBlock`, an intrusive spare-list link, and
`std::array<Slot, segment_size>`. A slot is a union with an explicitly managed `T` lifetime. The
container's origin and size describe the live intervals, including partial first and last segments;
there is no per-element occupancy bitmap. Every segment between the two endpoints is full.
Empty segments contain no live `T` objects and reside on the spare list outside the live directory.

## Capacity, retention, and bounded storage

`capacity()` counts all element slots in acquired segments. `segment_count()` includes live and
spare segments. `bytes_reserved()` sums the source-reported block sizes and excludes directory
storage. Partial endpoint segments mean `capacity() - size()` is not necessarily usable entirely
at either particular end.

`front_capacity()` and `back_capacity()` report how many additional elements can be inserted at
that end without allocating segments or growing the directory. Each includes all spare segments,
so they overlap and must not be added together. For an empty deque, either operation can use all
acquired capacity. An unchecked insertion requires the corresponding directional capacity to be
nonzero; checking only `size() < capacity()` is insufficient.

- `reserve(n)` acquires enough segments for at least `n` total slots, without changing live values.
- `reserve_front(n)` and `reserve_back(n)` reserve room for at least `n` **additional** elements at
  the selected end. They also ensure sufficient directory slots.
- An emptied segment from either end immediately joins the shared spare list. A later insertion
  at either end uses a spare before requesting a new source block.
- `clear()` destroys all elements and retains all segments and directory storage.
- `trim_capacity()` returns every spare segment; its `n` overload retains at least `n` total slots
  when possible. It never releases a live segment or relocates elements.
- `release()` destroys elements and returns every segment. The directory allocation is retained
  until destruction, consistent with `SegmentedVector`.

A fixed-size sliding window may need one extra segment while both endpoints are partial. At the
hard segment bound, insertion can fail even when the opposite endpoint contains unused slots.
The deque never moves live elements to turn that space into usable front or back capacity.

With monotonic storage, returning a block does not necessarily reclaim its bytes. Steady queue
traffic should retain and recycle segments; repeatedly trimming or releasing and rebuilding can
consume further arena space. The caller must keep the resource alive until the deque is destroyed,
and must not reset the resource while the deque owns blocks. `FixedBlockSource` and
`InlineBlockSource` permit only one outstanding block and therefore support one-segment cases;
a multi-segment arena adapter or memory resource is needed for larger bounded deques.

## Allocation, ownership, and constant evaluation

Runtime segment acquisition requests one block of `sizeof(Segment)` and `alignof(Segment)`, validates
the returned block, and placement-constructs the typed segment. Malformed blocks are returned to
the source without constructing elements. Source release follows destruction of all live objects
and the segment header.

The fourth template parameter, `DirectoryAllocator`, is rebound to `Segment*`. The container is
allocator-aware and provides `allocator_type`, `get_allocator()`, and leading `allocator_arg_t`
constructors. Copy construction uses `select_on_container_copy_construction` and the source's
`CopyForContainer()`. Copy assignment retains the destination directory allocator; allocators that
request copy-assignment propagation are unsupported. Move assignment and swap honor allocator
propagation and equality, preparing replacement directories before transferring unequal,
non-propagating storage. Participation and `noexcept` follow the source and allocator capabilities.

A `PmrBlockSource` and a `pmr::polymorphic_allocator` can route both allocations to one memory
resource, or callers can select separate resources. Segment reuse does not allocate additional
pointer metadata.

The default `NewDeleteBlockSource` configuration supports C++23 constant evaluation through typed
`std::allocator<Segment>` allocation and matching deallocation. Runtime calls still use the source.
The typed fallback is unavailable for custom sources because bypassing their exhaustion and state
would change semantics. A custom directory allocator must itself support constant evaluation.

## API surface

```cpp
template<SegmentedElement T,
         SegmentedOptions Options = {},
         mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
         typename DirectoryAllocator = std::allocator<std::byte>>
class SegmentedDeque {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using allocator_type = DirectoryAllocator;

  SegmentedDeque();
  template<typename SourceArg>
  explicit SegmentedDeque(SourceArg&& source);
  SegmentedDeque(std::allocator_arg_t, const allocator_type& allocator);
  template<typename SourceArg>
  SegmentedDeque(std::allocator_arg_t, const allocator_type& allocator, SourceArg&& source);
  template<std::input_iterator I, std::sentinel_for<I> S>
  SegmentedDeque(I first, S last);
  template<std::ranges::input_range R>
  SegmentedDeque(std::from_range_t, R&& range);
  // Source-taking and allocator-extended range constructors are also supported.

  allocator_type get_allocator() const;
  bool empty() const noexcept;
  size_type size() const noexcept;
  size_type capacity() const noexcept;
  size_type front_capacity() const noexcept;
  size_type back_capacity() const noexcept;
  size_type segment_count() const noexcept;
  size_type bytes_reserved() const noexcept;

  void reserve(size_type n);
  void reserve_front(size_type additional);
  void reserve_back(size_type additional);
  void resize(size_type n);
  void resize(size_type n, const T& value);

  T& operator[](size_type pos) noexcept;
  T& at(size_type pos);
  T& front();
  T& back();
  // Const accessors and mutable/const forward/reverse random-access iterators.

  template<typename... Args> T& emplace_front(Args&&... args);
  template<typename... Args> T& emplace_back(Args&&... args);
  // try_emplace_* return optional<reference_wrapper<T>> for recoverable sources.
  // unchecked_emplace_* require directional capacity and never acquire storage.
  // push_*, try_push_*, and unchecked_push_* accept const T& and T&&.
  template<std::ranges::input_range R> void append_range(R&& range);
  template<std::ranges::input_range R> void prepend_range(R&& range);

  void pop_front();
  void pop_back();
  T pop_front_value() requires std::move_constructible<T>;
  T pop_back_value() requires std::move_constructible<T>;
  void clear() noexcept;
  void release() noexcept;
  void trim_capacity() noexcept;
  void trim_capacity(size_type n) noexcept;
  segment_range segments() noexcept;
  const_segment_range segments() const noexcept;
};
```

The value-returning pop operations inherit the requirement policy's conditional `noexcept` for an
empty container. Iterator/range constructors default-construct the source in place when no source
is provided, permitting immovable inline sources. Source-taking constructors use constrained
forwarding, avoiding a by-value copy of over-aligned or immovable source objects.

`prepend_range()` constructs directly for forward or sized ranges, including immovable elements.
An unsized single-pass range must be staged to determine the prefix origin while preserving input
order. That case additionally requires construction from `T&&` and uses a temporary vector with the
rebound directory allocator. Its temporary allocation and move costs are not an allocation-free
reservation guarantee. The whole prefix is committed after construction; self-prepending a range
of existing elements preserves their identities.

## Validation and performance evidence

Correctness validation covers differential operation traces against `std::deque`, partial endpoint
segments, directory wrap/growth, unsigned coordinate wrap, surviving iterator identities, reverse
and segment traversal, immovable and over-aligned elements, allocator propagation, source failures,
constructor exceptions, range rollback, constexpr use, and arena-backed reuse after warm-up.
Normal and exception-policy tests remain separate, with sanitizer/compiler coverage supplied by CI.

Performance analysis measures matched indexed and iterator access against `SegmentedVector` and
`std::deque`, front/back growth and allocation boundaries, steady queue traffic in both directions,
clear/reuse and release/rebuild, and arena-backed segment recycling. Segment sizes and initial
offsets are explicit experimental axes. Results must record allocation counts alongside timings
so memory growth cannot masquerade as faster bounded reuse.

Raw measurement envelopes use `tools/benchmark_artifact.py`, a clean implementation commit, and
nine randomly interleaved repetitions. Reports retain dispersion and source/toolchain/host metadata.
Available-host evidence is diagnostic; choosing a tuned default requires matching Apple M5 Pro and
AMD Zen 5 evidence. The initial 256-element default matches the vector for comparability and is
not a claim of optimal deque tuning. The [measurement report](measurements/SEGMENTED_DEQUE.md) records the first Apple M5 Pro results,
design decisions, evidence limits, workloads, counters, and reproducible commands.
