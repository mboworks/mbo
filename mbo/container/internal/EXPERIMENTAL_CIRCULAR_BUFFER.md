# Experimental circular buffer

`ExperimentalCircularBuffer<T, Allocator>` is an internal, replaceable, growable circular buffer in
`mbo::container::container_internal`. `SegmentedDeque` uses it for its live segment pointers.
It is not a stable public container API. It also supports owning ordinary values, including
move-only and over-aligned types, with allocator-controlled storage and explicit object lifetimes.

## Representation

The buffer stores an allocator, an allocation pointer, a power-of-two capacity, and unsigned
`begin_` and `end_` indices. Only the logical interval `[begin_, end_)` contains live objects.
Unsigned arithmetic deliberately wraps, including when pushing onto the front of a fresh buffer:

```cpp
size = end_ - begin_;
physical_slot = (begin_ + logical_index) & (capacity_ - 1);
```

Capacity is zero or a power of two. `max_size()` rounds down the smaller of the allocator's limit
and `PTRDIFF_MAX` to a power of two. Consequently the live interval always has an unambiguous
unsigned length and iterator distances fit `difference_type`. A full buffer has `size() ==
capacity()`; an empty buffer has `begin_ == end_`. No slot is sacrificed to distinguish the two.
The zero-capacity state is both empty and full: its next insertion needs an allocation.

Capacity is stored in a private `Capacity` value type. It defaults to zero; its explicit integer
constructor uses `MBO_CONFIG_REQUIRE` to enforce zero or a power of two. The underlying integer
is private, so buffer operations can only replace it with another validated value. Copies and swaps
preserve the invariant. Validation happens before allocation; the new capacity is installed only
after allocation succeeds. The check remains enabled in optimized builds and follows the configured
fatal or throwing requirement policy. `Slot` relies on this invariant without repeating the check
on element access. Zero-capacity storage has no valid slot.

`push_front` constructs at `begin_ - 1` and then decrements `begin_`; `push_back` constructs at
`end_` and then increments `end_`. Pops immediately destroy the removed object and advance the
corresponding boundary. Freed slots are reusable from either end without moving other values.
Full-buffer insertion grows geometrically and preserves every existing value; it never overwrites
the oldest value. Growth constructs a replacement allocation in logical order and releases the old
one. `reserve` creates storage, not elements. `clear` retains storage; `shrink_to_fit` reduces it to
the smallest power of two that fits the live sequence, or releases it when empty.

## Interface and complexity

The STL-style interface includes:

- container, allocator, reference, pointer, size and difference aliases;
- default, allocator, count, value, iterator/sentinel, initializer-list, copy and move constructors;
- copy, move and initializer-list assignment, `assign`, and `assign_range`;
- `empty`, `full`, `size`, `capacity`, `max_size`, and `get_allocator`;
- `operator[]`, `at`, `front`, and `back`;
- `iterator`, `const_iterator`, `reverse_iterator`, and `const_reverse_iterator`, with the
  `begin`/`end`, `cbegin`/`cend`, `rbegin`/`rend`, and `crbegin`/`crend` accessors;
- `emplace_front`, `emplace_back`, `push_front`, `push_back`, `pop_front`, and `pop_back`;
- positional `emplace`, `insert`, `insert_range`, `erase`, `append_range`, and `prepend_range`;
- `resize`, `reserve`, `shrink_to_fit`, `clear`, `swap`, equality and lexicographic ordering.

Indexing, iterator arithmetic, accessors, and endpoint removal take constant time. Endpoint
insertion takes amortized constant time; growing the allocation takes linear time. Iterators model
`std::random_access_iterator`, including mutable-to-const conversion and mixed const comparison.
The range is not contiguous, so there is no `data()` accessor.

Middle insertion and bulk insertion stage incoming values before constructing a replacement ring.
They take linear time and may allocate even when the current capacity would suffice. This keeps
self-referential arguments, overlapping ranges, and single-pass input ranges well-defined.
Middle erasure shifts the suffix by assignment, then destroys the vacated elements. Prefix erasure
only pops the front. Erasure requires move-assignable or copy-assignable elements. Immovable values
can be emplaced into reserved storage, but relocating a nonempty buffer of them is rejected.

## Invalidation, ownership and failure

Buffer iterators contain their owner and logical position. Treat every successful structural
modification, including swap or move, as invalidating them. Existing element references and pointers
survive endpoint insertion without growth and endpoint removal of other values. Growth, shrinking
the allocation, middle insertion, bulk insertion, and assignment relocate or replace elements.
Middle erasure invalidates references from the erased position onward. A no-op `reserve` does not
invalidate anything. These rules concern the internal buffer; the deque's public element iterators
retain their independently documented stability.

Allocator traits control construction, destruction, allocation, deallocation and copy/move/swap
propagation. Copies retain capacity, including empty reservations. Moves transfer storage when
allocators permit it, and otherwise relocate values into the destination allocator's storage.
Swapping unequal nonpropagating allocators is a rejected precondition. Pointer values do not confer
ownership of their pointees. A PMR allocator can use caller-owned arena storage; that storage and
resource must outlive the buffer. No internal synchronization is provided.

Checked access and invalid-operation preconditions use `MBO_CONFIG_REQUIRE`: fatal by default,
or `std::runtime_error` with exceptions enabled and `--//mbo/config:require_throws=true`.
`operator[]`, iterator dereference and iterator arithmetic require valid positions, as with STL
containers. Comparisons and subtraction require iterators from the same buffer.

With exceptions enabled, partial replacements destroy their constructed objects and release their
allocation. Failed endpoint construction does not publish an additional live element. Relocation
uses `std::move_if_noexcept`: copying preserves existing values on failure; a throwing move-only
type, a constructor consuming an aliased rvalue, or an allocator that throws after earlier values
have been moved can leave existing values moved-from. Ownership and the live interval remain valid.
Failed resize destroys its appended values, though successful capacity growth can remain. Erasure
with throwing assignment provides the basic guarantee. Input already consumed from a single-pass
range cannot be restored. The default build remains exception-free.

All operations are `constexpr` where their element and allocator operations permit it. Tests cover
constant evaluation, all iterator families, wraparound, growth, reserved queue reuse, allocator
propagation, PMR, element lifetimes, allocation/construction failure, deque integration, and mixed
operations checked against `std::deque`.
