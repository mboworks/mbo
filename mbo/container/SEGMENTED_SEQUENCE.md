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

- Segment capacities are selected by a constexpr-compatible policy type.
- A policy may provide a compile-time list of segment capacities.
- After that list, a policy may stop at fixed capacity, repeat the final capacity, or delegate to
  another growth policy.
- Uniform power-of-two segments have a specialized index-mapping fast path up to a benchmarked
  threshold.
- Only strategies and tuning parameters proven relevant by benchmarks become public API.
- Mutation is limited to append, pop, and whole-container operations. Arbitrary insertion and
  erasure are intentionally unsupported; containers such as C++26 `std::hive` address different
  stable-erasure requirements.
- Existing elements, references, and iterators remain valid when another segment is appended.
- When popping empties a segment, that segment is retained in an unused-segment pool for later
  compatible growth. `shrink_to_fit()` releases retained unused segments using STL naming.
- Reuse and eviction are policy-controlled. A compatible retained segment may be reused; unusable
  or sufficiently long-unused segments may be released according to benchmark-proven limits.
- `shrink_to_fit()` never relocates live elements and therefore preserves their addresses. A future
  explicitly invalidating compaction operation or overload is possible only if a demonstrated use
  outweighs the loss of the container's central stability guarantee.
- Element construction and destruction follow normal `T` lifetime rules.
- The container exposes dense positions in `[0, size())`.
- Indexed access complexity and iterator category may depend on the selected segment policy; strict
  O(1) random access is not required of every supported policy.
- Bounded configurations detect capacity and arithmetic exhaustion before committing an element.
- The implementation is C++20 and supports constant evaluation wherever its selected storage and
  element operations permit it.

## Candidate structure

Each segment owns storage for a policy-selected number of `T` objects and tracks its constructed
prefix. A directory locates segments for indexed access. The directory representation, segment
ownership, and growth allocation are separate concerns and may have independent concepts.

For a compile-time capacity list, cumulative boundaries are also compile-time values. Index mapping
can use unrolled comparisons or another generated decision structure. Uniform power-of-two segments
can instead use shifts and masks where that is faster. Hybrid policies transition between mappings
at a measured threshold.

`SegmentedSequence` and an arena may share a private block-allocation primitive. They do not share a
public contract by default: the sequence manages typed objects and dense indices, while an arena
provides aligned byte ranges and may use region lifetime.

## Candidate API surface

The exact API is not settled. The minimum useful append-oriented surface is:

```cpp
template<typename T, auto Policy>
class SegmentedSequence {
 public:
  using value_type = T;
  using size_type = std::size_t;

  constexpr bool empty() const noexcept;
  constexpr size_type size() const noexcept;
  constexpr size_type capacity() const noexcept;

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
  constexpr void push_back(const T& value);
  constexpr void push_back(T&& value);
  constexpr void pop_back();
  constexpr void clear();
  constexpr void shrink_to_fit();
};
```

Failure-reporting variants are required for bounded and no-exception configurations; their names
and result types remain open.

## Required guarantees to settle

| Area                  | Decision required                                                                |
| --------------------- | -------------------------------------------------------------------------------- |
| Segment reuse         | Compatibility classes and selection among retained unused segments               |
| Cache eviction        | Retained bytes/count/age limits and handling of unusable segments                |
| Indexed complexity    | Complexity exposed by each policy and the required upper bound                   |
| Iterator category     | Policy-dependent category or one common category for the public container        |
| Iterator identity     | Whether comparisons across different containers are guarded or preconditioned    |
| Exception guarantee   | Strong guarantee for allocation and element-construction failures                |
| Allocation            | Allocator, PMR adapter, block-source concept, caller-owned segments, or a subset |
| Ownership             | Copyability, move guarantees, swap behavior, and allocator propagation           |
| Invalidation          | Exact rules for append, pop, clear, move, swap, and destruction                  |
| Capacity              | Meaning for unbounded policies and maximum-size calculation                      |
| Contiguous operations | Whether segment-local spans/ranges are exposed                                   |
| Constexpr             | Which policies and allocation modes are usable during constant evaluation        |

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

## Open questions

1. How does a growth request select a compatible retained segment: exact capacity, smallest fit,
   policy identity, or another size-class rule?
2. What block-source and ownership models are required in version one?
3. Does `capacity()` report currently active and reusable slots, the policy maximum, or are
   multiple operations needed?
4. Does `clear()` retain all segments for reuse while `shrink_to_fit()` releases them?
5. Are copy construction and copy assignment required, and what allocation policy does a copy use?
6. Are segment-local spans useful enough to expose publicly?
7. Which failure API is primary when growth or element construction cannot complete?
8. Does the iterator category vary with policy, or does the class expose the strongest category
   supported by every policy?
9. Is a separate explicitly invalidating full-compaction operation ever needed, or is stable
   `shrink_to_fit()` sufficient?
