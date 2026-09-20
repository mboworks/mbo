// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MBO_CONTAINER_INTERNAL_LIMITED_ORDERED_H_
#define MBO_CONTAINER_INTERNAL_LIMITED_ORDERED_H_

#include <algorithm>
#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>  // IWYU pragma: keep
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/log/absl_log.h"  // IWYU pragma: keep
#include "mbo/config/require.h"
#include "mbo/container/limited_options.h"  // IWYU pragma: export
#include "mbo/types/compare.h"              // IWYU pragma: export
#include "mbo/types/traits.h"

#ifdef MBO_FORCE_INLINE
# undef MBO_FORCE_INLINE
#endif
#define MBO_FORCE_INLINE

// __attribute__((always_inline))

#ifdef MBO_ALWAYS_INLINE
# undef MBO_ALWAYS_INLINE
#endif
#define MBO_ALWAYS_INLINE __attribute__((always_inline))

namespace mbo::container::container_internal {

// NOLINTBEGIN(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)
// LimitedSet/LimitedMap storage: a union keeps unused capacity uninitialized,
// and the iterators are pointers into it. This is the implementation.

// NOLINTBEGIN(readability-identifier-naming)

template<typename Key, typename Mapped, typename Value>
concept LimitedOrderedValidImpl =
    std::same_as<std::remove_const_t<Key>, std::remove_const_t<Value>>
    || (mbo::types::IsPair<std::remove_const_t<Value>>
        && std::same_as<std::remove_const_t<Key>, std::remove_const_t<typename Value::first_type>>);

template<typename Key, typename Mapped, typename Value>
concept LimitedOrderedValid =  //
    std::move_constructible<std::remove_const_t<Key>> && std::move_constructible<Mapped>
    && std::move_constructible<std::remove_const_t<Value>> && LimitedOrderedValidImpl<Key, Mapped, Value>;

template<typename Key, typename Mapped, typename Value, auto options, typename Compare = std::less<Key>>
requires(LimitedOrderedValid<Key, Mapped, Value>)
class [[nodiscard]] LimitedOrdered {
 protected:
  using RawValue = std::remove_const_t<Value>;
  static constexpr bool kKeyOnly = std::same_as<Key, Value>;  // true = set, false = map (of pairs).

  // Size and Options management.
  static_assert(IsLimitedOptionsOrSize<decltype(options)>);
  using Options = decltype(MakeLimitedOptions<options>());
  static_assert(std::is_trivially_destructible_v<RawValue> || !Options::Has(LimitedOptionsFlag::kEmptyDestructor));
  static constexpr std::size_t Capacity = Options::kCapacity;

  static constexpr bool kOptimizeIndexOf = !Options::Has(LimitedOptionsFlag::kNoOptimizeIndexOf);
  static constexpr bool kCustomIndexOfBeyondUnroll = Options::Has(LimitedOptionsFlag::kCustomIndexOfBeyondUnroll);

  static constexpr std::size_t kUnrollMaxCapacityLimit = 32;  // The maximum supported in code.
  static constexpr std::size_t kUnrollMaxCapacity = ::mbo::config::kUnrollMaxCapacityDefault;  // MUST MATCH `index_of`.
  static_assert(
      kUnrollMaxCapacity >= 4 && kUnrollMaxCapacity <= kUnrollMaxCapacityLimit,
      "Check documentation for `::mbo::config::kUnrollMaxCapacityDefault`.");

  // Must declare each other as friends so that we can correctly move from other.
  template<typename OK, typename OM, typename OV, auto OtherN, typename Comp>
  requires(LimitedOrderedValid<OK, OM, OV>)
  friend class LimitedOrdered;

  struct None final {};

  union Data {
    constexpr Data() noexcept : none{} {}

    constexpr Data(const Data&) noexcept = default;
    constexpr Data& operator=(const Data&) noexcept = default;
    constexpr Data(Data&&) noexcept = default;
    constexpr Data& operator=(Data&&) noexcept = default;

    constexpr ~Data() noexcept
    requires(std::is_trivially_destructible_v<RawValue>)
    = default;

    constexpr ~Data() noexcept
    requires(!std::is_trivially_destructible_v<RawValue>)
    {}

    RawValue data;
    None none;
  };

  // A throwing constructor never runs `LimitedOrdered`'s destructor. Track the
  // partially constructed container so its already-live elements are destroyed
  // unless the constructor completes and releases the guard.
  class ConstructionGuard final {
   public:
    constexpr explicit ConstructionGuard(LimitedOrdered* target) noexcept : target_(target) {}

    constexpr ~ConstructionGuard() noexcept {
      if (target_ != nullptr) {
        target_->clear();
      }
    }

    ConstructionGuard(const ConstructionGuard&) = delete;
    ConstructionGuard& operator=(const ConstructionGuard&) = delete;
    ConstructionGuard(ConstructionGuard&&) = delete;
    ConstructionGuard& operator=(ConstructionGuard&&) = delete;

    constexpr void Release() noexcept { target_ = nullptr; }

   private:
    LimitedOrdered* target_;
  };

  class RightShiftGuard final {
   public:
    constexpr RightShiftGuard(LimitedOrdered* target, std::size_t old_size) noexcept
        : target_(target), old_size_(old_size), hole_(old_size) {
      target_->size_ = 0;
    }

    constexpr ~RightShiftGuard() noexcept {
      if (target_ == nullptr) {
        return;
      }
      for (std::size_t pos = 0; pos < hole_; ++pos) {
        std::destroy_at(&target_->values_[pos].data);
      }
      for (std::size_t pos = hole_ + 1; pos <= old_size_; ++pos) {
        std::destroy_at(&target_->values_[pos].data);
      }
    }

    RightShiftGuard(const RightShiftGuard&) = delete;
    RightShiftGuard& operator=(const RightShiftGuard&) = delete;
    RightShiftGuard(RightShiftGuard&&) = delete;
    RightShiftGuard& operator=(RightShiftGuard&&) = delete;

    constexpr void SetHole(std::size_t hole) noexcept { hole_ = hole; }

    constexpr void Release() noexcept { target_ = nullptr; }

   private:
    LimitedOrdered* target_;
    std::size_t old_size_;
    std::size_t hole_;
  };

  class LeftShiftGuard final {
   public:
    constexpr LeftShiftGuard(LimitedOrdered* target, std::size_t old_size, std::size_t hole) noexcept
        : target_(target), old_size_(old_size), hole_(hole) {
      target_->size_ = 0;
    }

    constexpr ~LeftShiftGuard() noexcept {
      if (target_ == nullptr) {
        return;
      }
      for (std::size_t pos = 0; pos < hole_; ++pos) {
        std::destroy_at(&target_->values_[pos].data);
      }
      for (std::size_t pos = hole_ + 1; pos < old_size_; ++pos) {
        std::destroy_at(&target_->values_[pos].data);
      }
    }

    LeftShiftGuard(const LeftShiftGuard&) = delete;
    LeftShiftGuard& operator=(const LeftShiftGuard&) = delete;
    LeftShiftGuard(LeftShiftGuard&&) = delete;
    LeftShiftGuard& operator=(LeftShiftGuard&&) = delete;

    constexpr void SetHole(std::size_t hole) noexcept { hole_ = hole; }

    constexpr void Release() noexcept { target_ = nullptr; }

   private:
    LimitedOrdered* target_;
    std::size_t old_size_;
    std::size_t hole_;
  };

 public:
  using key_type = Key;
  using value_type = Value;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using key_compare = Compare;
  using reference = std::conditional_t<kKeyOnly, const Value&, Value&>;
  using const_reference = const Value&;
  using pointer = std::conditional_t<kKeyOnly, const Value*, Value*>;
  using const_pointer = const Value*;

  static constexpr size_type npos = static_cast<size_type>(-1);  // Result of `index_of` if not found.

  // Heterogeneous ("transparent") lookup, as `std::set`/`std::map` do it: when the
  // comparator declares `is_transparent`, lookups accept any type the comparator
  // can order against `Key`, instead of forcing the caller to materialise a `Key`.
  // For `LimitedSet<std::string>` that turns `set.find(std::string_view)` from a
  // string construction per call into a plain comparison.
  static constexpr bool kTransparent = requires { typename Compare::is_transparent; };

  // A key that is NOT the container's own key type. Lookup templates are
  // constrained on this so they never shadow the exact-`Key` overloads - those
  // stay the better match and keep working when the comparator is not transparent.
  template<typename K>
  static constexpr bool kIsForeignKey = kTransparent && !std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>>;

  struct ValueCompare {
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    MBO_ALWAYS_INLINE static constexpr const Key& GetKey(const Key& key) noexcept { return key; }

    MBO_ALWAYS_INLINE static constexpr const Key& GetKey(const value_type& val) noexcept
    requires(!kKeyOnly)
    {
      return val.first;
    }

    MBO_ALWAYS_INLINE static constexpr const Key& GetKey(const Data& data) noexcept {
      if constexpr (kKeyOnly) {
        return data.data;
      } else {
        return data.data.first;
      }
    }

    // A transparent lookup key is not a `Key` and holds no key to extract: it IS
    // the thing being compared, so it passes through untouched.
    template<typename K>
    requires(
        kIsForeignKey<K> && !std::same_as<std::remove_cvref_t<K>, RawValue>
        && !std::same_as<std::remove_cvref_t<K>, Data>)
    MBO_ALWAYS_INLINE static constexpr const K& GetKey(const K& key) noexcept {
      // Returning the caller's own reference is the point: the lookup key outlives the
      // comparison, and copying it is precisely what transparent lookup exists to avoid.
      // The suppression sits on the line the check reports - the return, not the signature.
      // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
      return key;
    }

    constexpr ~ValueCompare() noexcept = default;

    constexpr explicit ValueCompare() noexcept = default;

    constexpr explicit ValueCompare(const key_compare& comp) : key_comp(comp) {}

    constexpr ValueCompare(const ValueCompare&) = default;
    constexpr ValueCompare& operator=(const ValueCompare&) = default;
    constexpr ValueCompare(ValueCompare&&) = default;
    constexpr ValueCompare& operator=(ValueCompare&&) = default;

    // A side of the comparison is acceptable if it is the key, a stored value, or -
    // when the comparator is transparent - any type it can order against the key.
    // `std::lower_bound` calls the comparator both ways round, so both sides need it.
    template<typename T>
    static constexpr bool kComparableSide =
        std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<Key>> || std::same_as<std::remove_cvref_t<T>, RawValue>
        || std::same_as<std::remove_cvref_t<T>, Data> || kTransparent;

    template<typename L, typename R>
    requires(kComparableSide<L> && kComparableSide<R>)
    MBO_FORCE_INLINE constexpr bool operator()(const L& lhs, const R& rhs) const {
      return key_comp(GetKey(lhs), GetKey(rhs));
    }

    key_compare key_comp;
  };

  // NOTE: The name is misleading but must adhere to the STL: The comparator only
  // compares the `first` part (the key) of mapped values.
  using value_compare = std::conditional_t<kKeyOnly, Compare, ValueCompare>;

  template<bool IsConst>
  class Iterator final {
   private:
    template<bool>
    friend class Iterator;

    using DataPointer = std::conditional_t<IsConst, const Data*, Data*>;

   public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using difference_type = LimitedOrdered::difference_type;
    using value_type = LimitedOrdered::value_type;
    using pointer = std::conditional_t<IsConst, LimitedOrdered::const_pointer, LimitedOrdered::pointer>;
    using reference = std::conditional_t<IsConst, LimitedOrdered::const_reference, LimitedOrdered::reference>;

    constexpr Iterator() noexcept = default;

    MBO_ALWAYS_INLINE constexpr explicit Iterator(DataPointer pos) noexcept : pos_(pos) {}

    template<bool OtherConst>
    requires(IsConst && !OtherConst)
    // Deliberately implicit: a mutable iterator must convert to const_iterator.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Iterator(const Iterator<OtherConst>& other) noexcept : pos_(other.pos_) {}

    MBO_ALWAYS_INLINE constexpr reference operator*() const noexcept { return pos_->data; }

    MBO_ALWAYS_INLINE constexpr pointer operator->() const noexcept { return &pos_->data; }

    constexpr Iterator& operator++() noexcept {
      ++pos_;
      return *this;
    }

    constexpr Iterator operator++(int) noexcept {
      const Iterator result = *this;
      ++*this;
      return result;
    }

    constexpr Iterator& operator--() noexcept {
      --pos_;
      return *this;
    }

    constexpr Iterator operator--(int) noexcept {
      const Iterator result = *this;
      --*this;
      return result;
    }

    constexpr Iterator& operator+=(difference_type offset) noexcept {
      pos_ += offset;
      return *this;
    }

    constexpr Iterator& operator-=(difference_type offset) noexcept {
      pos_ -= offset;
      return *this;
    }

    constexpr reference operator[](difference_type offset) const noexcept { return pos_[offset].data; }

    friend constexpr Iterator operator+(Iterator iter, difference_type offset) noexcept { return iter += offset; }

    friend constexpr Iterator operator+(difference_type offset, Iterator iter) noexcept { return iter += offset; }

    friend constexpr Iterator operator-(Iterator iter, difference_type offset) noexcept { return iter -= offset; }

    template<bool OtherConst>
    constexpr difference_type operator-(const Iterator<OtherConst>& other) const noexcept {
      return pos_ - other.pos_;
    }

    template<bool OtherConst>
    constexpr bool operator==(const Iterator<OtherConst>& other) const noexcept {
      return pos_ == other.pos_;
    }

    template<bool OtherConst>
    constexpr auto operator<=>(const Iterator<OtherConst>& other) const noexcept {
      return pos_ <=> other.pos_;
    }

   private:
    DataPointer pos_{nullptr};
  };

  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

 private:
  template<typename T>
  struct IsIterator
      : std::bool_constant<
            std::same_as<std::remove_cvref_t<T>, iterator> || std::same_as<std::remove_cvref_t<T>, const_iterator>> {};

 public:
  // Destructor and constructors from same type.

  constexpr ~LimitedOrdered() noexcept
  requires(Options::Has(LimitedOptionsFlag::kEmptyDestructor))
  = default;

  constexpr ~LimitedOrdered() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && std::is_trivially_destructible_v<RawValue>)
  {
    clear();
  }

  ~LimitedOrdered() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && !std::is_trivially_destructible_v<RawValue>)
  {
    clear();
  }

  constexpr LimitedOrdered() = default;

  constexpr explicit LimitedOrdered(const Compare& key_comp) : key_comp_(key_comp) {}

  constexpr LimitedOrdered(const LimitedOrdered& other) : key_comp_(other.key_comp_) {
    ConstructionGuard guard(this);
    for (const_reference value : other) {
      Append(value);
    }
    guard.Release();
  }

  constexpr LimitedOrdered& operator=(const LimitedOrdered& other) {
    if (this != &other) {
      clear();
      key_comp_ = other.key_comp_;
      val_comp_ = value_compare(key_comp_);
      for (const_reference value : other) {
        Append(value);
      }
    }
    return *this;
  }

  // Element or comparator moves may throw and must propagate.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  constexpr LimitedOrdered(LimitedOrdered&& other) : key_comp_(std::move(other.key_comp_)) {
    ConstructionGuard guard(this);
    for (std::size_t pos = 0; pos < other.size_; ++pos) {
      Append(std::move(other.values_[pos].data));
    }
    guard.Release();
    other.clear();
  }

  // Element or comparator moves may throw and must propagate.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  constexpr LimitedOrdered& operator=(LimitedOrdered&& other) {
    if (this != &other) {
      clear();
      key_comp_ = std::move(other.key_comp_);
      val_comp_ = value_compare(key_comp_);
      for (std::size_t pos = 0; pos < other.size_; ++pos) {
        Append(std::move(other.values_[pos].data));
      }
      other.clear();
    }
    return *this;
  }

  // Constructors and assignment from other LimitVector/value types.

  template<std::forward_iterator It>
  requires types::ConstructibleFrom<RawValue, mbo::types::ForwardIteratorValueType<It>>
  constexpr LimitedOrdered(It first, It last, const Compare& key_comp = Compare()) : key_comp_(key_comp) {
    ConstructionGuard guard(this);
    if constexpr (Options::Has(LimitedOptionsFlag::kRequireSortedInput)) {
      MBO_CONFIG_REQUIRE(std::is_sorted(first, last, key_comp_), "Flag `kRequireSortedInput` violated.");
    }
    while (first != last) {
      if constexpr (Options::Has(LimitedOptionsFlag::kRequireSortedInput)) {
        Append(*first);
      } else {
        emplace(*first);
      }
      ++first;
    }
    guard.Release();
  }

  constexpr LimitedOrdered(const std::initializer_list<value_type>& list, const Compare& key_comp = Compare())
      : LimitedOrdered(list.begin(), list.end(), key_comp) {}

  template<types::ConstructibleInto<value_type> U>
  requires(!std::same_as<U, value_type>)
  constexpr LimitedOrdered(const std::initializer_list<U>& list, const Compare& key_comp = Compare())
      : LimitedOrdered(list.begin(), list.end(), key_comp) {}

  template<types::ConstructibleInto<value_type> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr LimitedOrdered& operator=(const std::initializer_list<U>& list) {
    LimitedOrdered replacement(list, key_comp_);
    *this = std::move(replacement);
    return *this;
  }

  template<
      types::ConstructibleInto<Key> OK,
      types::ConstructibleInto<Mapped> OM,
      typename OV,
      auto OtherN,
      typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr explicit LimitedOrdered(const LimitedOrdered<OK, OM, OV, OtherN, OtherCompare>& other) {
    ConstructionGuard guard(this);
    for (auto it = other.begin(); it < other.end(); ++it) {
      if constexpr (kKeyOnly) {
        emplace(*it);
      } else {
        emplace(it->first, it->second);
      }
    }
    guard.Release();
  }

  template<
      types::ConstructibleInto<Key> OK,
      types::ConstructibleInto<Mapped> OM,
      typename OV,
      auto OtherN,
      typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr LimitedOrdered& operator=(const LimitedOrdered<OK, OM, OV, OtherN, OtherCompare>& other) {
    clear();
    for (auto it = other.begin(); it < other.end(); ++it) {
      if constexpr (kKeyOnly) {
        emplace(*it);
      } else {
        emplace(it->first, it->second);
      }
    }
    return *this;
  }

  template<
      types::ConstructibleInto<Key> OK,
      types::ConstructibleInto<Mapped> OM,
      typename OV,
      auto OtherN,
      typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // The source is consumed element by element because its stored type differs.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr explicit LimitedOrdered(LimitedOrdered<OK, OM, OV, OtherN, OtherCompare>&& other) {
    ConstructionGuard guard(this);
    for (std::size_t pos = 0; pos < other.size_; ++pos) {
      if constexpr (kKeyOnly) {
        emplace(std::move(other.values_[pos].data));
      } else {
        emplace(other.values_[pos].data.first, std::move(other.values_[pos].data.second));
      }
    }
    guard.Release();
    other.clear();
  }

  template<
      types::ConstructibleInto<Key> OK,
      types::ConstructibleInto<Mapped> OM,
      typename OV,
      auto OtherN,
      typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // The source is consumed element by element because its stored type differs.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr LimitedOrdered& operator=(LimitedOrdered<OK, OM, OV, OtherN, OtherCompare>&& other) {
    clear();
    for (std::size_t pos = 0; pos < other.size_; ++pos) {
      if constexpr (kKeyOnly) {
        emplace(std::move(other.values_[pos].data));
      } else {
        emplace(other.values_[pos].data.first, std::move(other.values_[pos].data.second));
      }
    }
    other.clear();
    return *this;
  }

  // Find and search: lower_bound, upper_bound, equal_range, find, contains, count

  MBO_FORCE_INLINE constexpr iterator lower_bound(const Key& key) {
    return std::lower_bound(begin(), end(), key, val_comp_);
  }

  MBO_FORCE_INLINE constexpr const_iterator lower_bound(const Key& key) const {
    return std::lower_bound(begin(), end(), key, val_comp_);
  }

  MBO_FORCE_INLINE constexpr iterator upper_bound(const Key& key) {
    return std::upper_bound(begin(), end(), key, val_comp_);
  }

  MBO_FORCE_INLINE constexpr const_iterator upper_bound(const Key& key) const {
    return std::upper_bound(begin(), end(), key, val_comp_);
  }

  // Transparent overloads. Each is constrained on `kIsForeignKey`, so it only
  // exists when the comparator opts in via `is_transparent` AND the argument is
  // not already a `Key` - an exact `Key` argument keeps binding to the non-template
  // overload above, which remains the better match.

  template<typename K>
  requires(kIsForeignKey<K>)
  MBO_FORCE_INLINE constexpr iterator lower_bound(const K& key) {
    return std::lower_bound(begin(), end(), key, val_comp_);
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  MBO_FORCE_INLINE constexpr const_iterator lower_bound(const K& key) const {
    return std::lower_bound(begin(), end(), key, val_comp_);
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  MBO_FORCE_INLINE constexpr iterator upper_bound(const K& key) {
    return std::upper_bound(begin(), end(), key, val_comp_);
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  MBO_FORCE_INLINE constexpr const_iterator upper_bound(const K& key) const {
    return std::upper_bound(begin(), end(), key, val_comp_);
  }

  // NOLINTBEGIN(*-magic-numbers,*-macro-usage,*-function-size,readability-function-cognitive-complexity)
  // Templated on the key so a transparent lookup gets the SAME dispatch - including
  // the unrolled fast path below. Routing foreign keys through `lower_bound` instead
  // would make them slower than exact keys on exactly the small containers this
  // class exists for. `K` defaults to `Key`, so an exact-key call is unchanged.
  template<typename K = Key>
  requires(std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>> || kIsForeignKey<K>)
  MBO_ALWAYS_INLINE constexpr std::size_t index_of(const K& key) const
  requires(kOptimizeIndexOf && Capacity <= kUnrollMaxCapacity)
  {
#define MBO_CASE_LIMITED_POS_COMP(POS)                                     \
  static_assert((POS) + 1 <= kUnrollMaxCapacityLimit);                     \
  case ((POS) + 1):                                                        \
    if constexpr ((POS) >= Capacity) {                                     \
      return npos;                                                         \
    } else if constexpr (mbo::types::IsCompareLess<Compare>) {             \
      const auto comp = key_comp_.Compare(key, GetKey(values_[POS].data)); \
      if (comp >= 0) [[unlikely]] {                                        \
        if (comp > 0) [[likely]] {                                         \
          return npos;                                                     \
        } else [[unlikely]] {                                              \
          return (POS);                                                    \
        }                                                                  \
      }                                                                    \
    } else {                                                               \
      if (!key_comp_(key, GetKey(values_[POS].data))) [[unlikely]] {       \
        if (key_comp_(GetKey(values_[POS].data), key)) [[likely]] {        \
          return npos;                                                     \
        } else [[unlikely]] {                                              \
          return POS;                                                      \
        }                                                                  \
      }                                                                    \
    }                                                                      \
    [[fallthrough]]
    switch (size_) {  // LCOV_EXCL_BR_LINE: GCC attributes every generated unrolled case edge to this line.
      MBO_CASE_LIMITED_POS_COMP(31);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(30);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(29);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(28);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(27);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(26);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(25);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(24);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(23);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(22);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(21);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(20);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(19);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(18);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(17);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(16);  // LCOV_EXCL_LINE: above the configured unroll maximum.
      MBO_CASE_LIMITED_POS_COMP(15);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(14);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(13);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(12);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(11);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(10);  // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(9);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(8);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(7);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(6);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(5);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(4);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(3);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(2);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(1);   // LCOV_MERGE_BR_LINE 4,2
      MBO_CASE_LIMITED_POS_COMP(0);   // LCOV_MERGE_BR_LINE 4,2
      default: break;                 // Handles `size_ == 0`.
    }
#undef MBO_CASE_LIMITED_POS_COMP
    return npos;
  }  // NOLINTEND(*-magic-numbers,*-macro-usage,*-function-size,readability-function-cognitive-complexity)

  template<typename K = Key>
  requires(std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>> || kIsForeignKey<K>)
  MBO_ALWAYS_INLINE constexpr std::size_t index_of(const K& key) const
  requires(
      kOptimizeIndexOf && kCustomIndexOfBeyondUnroll
      && mbo::types::IsCompareLess<Compare> && Capacity > kUnrollMaxCapacity)
  {
    std::size_t left = 0;
    std::size_t right = size_;
    while (left < right) [[likely]] {
      const std::size_t pos = left + ((right - left) >> 1U);
      auto cmp = key_comp_.Compare(key, GetKey(values_[pos]));
      if (cmp < 0) [[unlikely]] {
        right = pos;
      } else if (cmp > 0) {
        left = pos + 1;
      } else {
        return pos;  // == 0
      }
    }
    return npos;
  }

  template<typename K = Key>
  requires(std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>> || kIsForeignKey<K>)
  MBO_ALWAYS_INLINE std::size_t index_of(const K& key) const
  requires(
      kOptimizeIndexOf && kCustomIndexOfBeyondUnroll
      && !mbo::types::IsCompareLess<Compare> && Capacity > kUnrollMaxCapacity)
  {
    if (size_ == 0) {
      return npos;
    }
    std::size_t left = 0;
    std::size_t right = size_;
    while (true) {
      const std::size_t diff = (right - left) >> 1U;
      const std::size_t pos = left + diff;
      if (key_comp_(key, GetKey(values_[pos]))) [[likely]] {
        if (diff == 0) [[unlikely]] {
          return npos;
        }
        right = pos;
      } else {
        if (diff == 0) [[unlikely]] {
          if (key_comp_(GetKey(values_[left]), key)) [[likely]] {
            return npos;
          } else [[unlikely]] {
            return left;
          }
        }
        left = pos;
      }
    }
  }

  template<typename K = Key>
  requires(std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>> || kIsForeignKey<K>)
  MBO_ALWAYS_INLINE constexpr std::size_t index_of(const K& key) const
  requires(!kOptimizeIndexOf || (kOptimizeIndexOf && !kCustomIndexOfBeyondUnroll && Capacity > kUnrollMaxCapacity))
  {
    const const_iterator it = lower_bound(key);
    return it == end() || key_comp_(key, GetKey(*it)) ? npos : it - begin();  // LCOV_MERGE_BR_LINE 4: templates.
  }

  MBO_FORCE_INLINE constexpr reference at_index(size_type pos) {
    MBO_CONFIG_REQUIRE(pos < size_, "Out of range");
    return values_[pos].data;
  }

  MBO_FORCE_INLINE constexpr const value_type& at_index(size_type pos) const {
    MBO_CONFIG_REQUIRE(pos < size_, "Out of range");
    return values_[pos].data;
  }

  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr iterator find(const Key& key) {
    if constexpr (kOptimizeIndexOf) {
      const std::size_t pos = index_of(key);
      return pos == npos ? end() : iterator(&values_[pos]);
    } else {  // Not kOptimizeIndexOf
      const iterator it = lower_bound(key);
      return it == end() || key_comp_(key, GetKey(*it)) ? end() : it;
    }
  }

  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr const_iterator find(const Key& key) const {
    if constexpr (kOptimizeIndexOf) {
      const std::size_t pos = index_of(key);
      return pos == npos ? end() : const_iterator(&values_[pos]);
    } else {  // Not kOptimizeIndexOf
      const const_iterator it = lower_bound(key);
      return it == end() || key_comp_(key, GetKey(*it)) ? end() : it;
    }
  }

  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr bool contains(const Key& key) const {
    if constexpr (kOptimizeIndexOf) {
      return index_of(key) != npos;
    } else {
      return std::binary_search(begin(), end(), key, val_comp_);
    }
  }

  // Transparent `find`/`contains`. These mirror the exact-key versions above rather
  // than taking a separate route: `index_of` is templated on the key, so a foreign
  // key gets the same unrolled fast path and is not penalised for being foreign.

  template<typename K>
  requires(kIsForeignKey<K>)
  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr iterator find(const K& key) {
    if constexpr (kOptimizeIndexOf) {
      const std::size_t pos = index_of(key);
      return pos == npos ? end() : iterator(&values_[pos]);
    } else {
      const iterator it = lower_bound(key);
      return it == end() || val_comp_(key, *it) ? end() : it;
    }
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr const_iterator find(const K& key) const {
    if constexpr (kOptimizeIndexOf) {
      const std::size_t pos = index_of(key);
      return pos == npos ? end() : const_iterator(&values_[pos]);
    } else {
      const const_iterator it = lower_bound(key);
      return it == end() || val_comp_(key, *it) ? end() : it;
    }
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  // LCOV_MERGE_BR_LINE 1: GCC emits a function-entry branch for every specialization.
  MBO_FORCE_INLINE constexpr bool contains(const K& key) const {
    if constexpr (kOptimizeIndexOf) {
      return index_of(key) != npos;
    } else {
      return std::binary_search(begin(), end(), key, val_comp_);
    }
  }

  // Performs contains-all-of functionality (not part of STL).
  template<typename Other>
  requires(
      types::ContainerIsForwardIteratable<Other>
      && (std::equality_comparable_with<typename Other::value_type, Key> || kIsForeignKey<typename Other::value_type>))
  constexpr bool contains_all(const Other& other) const {
    for (auto it = other.begin(); it != other.end(); ++it) {
      if (!contains(*it)) {
        return false;
      }
    }
    return true;
  }

  // Performs contains-all-of functionality (not part of STL).
  template<typename U>
  requires(std::equality_comparable_with<Key, U> || kIsForeignKey<U>)
  constexpr bool contains_all(const std::initializer_list<U>& other) const {
    for (auto it = other.begin(); it != other.end(); ++it) {
      if (!contains(*it)) {
        return false;
      }
    }
    return true;
  }

  // Performs contains-any-of functionality (not part of STL).
  template<typename Other = std::initializer_list<Key>>
  requires(
      types::ContainerIsForwardIteratable<Other>
      && (std::equality_comparable_with<typename Other::value_type, Key> || kIsForeignKey<typename Other::value_type>))
  constexpr bool contains_any(const Other& other) const {
    for (auto it = other.begin(); it != other.end(); ++it) {
      if (contains(*it)) {
        return true;
      }
    }
    return false;
  }

  // Performs contains-any-of functionality (not part of STL).
  template<typename U>
  requires(std::equality_comparable_with<Key, U> || kIsForeignKey<U>)
  constexpr bool contains_any(const std::initializer_list<U>& other) const {
    for (auto it = other.begin(); it != other.end(); ++it) {
      if (contains(*it)) {
        return true;
      }
    }
    return false;
  }

  constexpr std::pair<iterator, iterator> equal_range(const Key& key) {
    return std::equal_range(begin(), end(), key, val_comp_);
  }

  constexpr std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
    return std::equal_range(begin(), end(), key, val_comp_);
  }

  constexpr std::size_t count(const Key& key) const {
    const auto [first, last] = equal_range(key);
    return last - first;
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  constexpr std::pair<iterator, iterator> equal_range(const K& key) {
    return std::equal_range(begin(), end(), key, val_comp_);
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  constexpr std::pair<const_iterator, const_iterator> equal_range(const K& key) const {
    return std::equal_range(begin(), end(), key, val_comp_);
  }

  template<typename K>
  requires(kIsForeignKey<K>)
  constexpr std::size_t count(const K& key) const {
    const auto [first, last] = equal_range(key);
    return last - first;
  }

  // Modification: clear, swap, emplace, insert

  constexpr void clear() noexcept {
    while (size_ > 0) {
      std::destroy_at(&values_[--size_].data);
    }
  }

  template<auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity == Capacity)
  constexpr void swap(LimitedOrdered<Key, Mapped, Value, OtherN, Compare>& other) {
    if (static_cast<const void*>(this) == static_cast<const void*>(&other)) {
      return;
    }
    LimitedOrdered temporary(std::move(*this));
    *this = std::move(other);
    other = std::move(temporary);
  }

  template<typename... Args>
  constexpr std::pair<iterator, bool> emplace(Args&&... args) {
    RawValue new_val(std::forward<Args>(args)...);  // NOLINT(misc-const-correctness)
    const iterator dst = lower_bound(GetKey(new_val));
    if (dst != end() && !key_comp_(GetKey(*dst), GetKey(new_val)) && !key_comp_(GetKey(new_val), GetKey(*dst))) {
      return std::make_pair(dst, false);
    }
    const auto index = static_cast<size_type>(dst - begin());
    return std::make_pair(InsertStaged(index, std::move(new_val)), true);
  }

  template<typename It>
  requires IsIterator<It>::value
  constexpr iterator erase(It pos) {
    MBO_CONFIG_REQUIRE(cbegin() <= pos && pos < cend(), "Invalid `pos`.");
    return EraseIndex(static_cast<size_type>(pos - cbegin()));
  }

  constexpr iterator erase(const_iterator first, const_iterator last) {
    MBO_CONFIG_REQUIRE(cbegin() <= first && first <= last && last <= cend(), "Invalid `first` or `last`.");
    const auto index = static_cast<size_type>(first - cbegin());
    const auto count = static_cast<size_type>(last - first);
    for (size_type erased = 0; erased < count; ++erased) {
      EraseIndex(index);
    }
    return begin() + static_cast<difference_type>(index);
  }

  constexpr size_type erase(const Key& key) {
    size_type count = 0;
    while (true) {
      auto pos = find(key);
      if (pos == end()) {
        break;
      }
      erase(pos);
      ++count;
    }
    return count;
  }

  // Erase by a key that is NOT this container's key type.
  //
  // Excluding `Key` gives the two overloads non-overlapping domains: an exact key -
  // lvalue, temporary or value - always binds to the simple overload above, and this
  // one only ever sees a foreign key. Without the constraint `K&&` deduces `Key&` for a
  // non-const lvalue and outranks `const Key&`, so the template silently handled that
  // case too. That was not a bug - it bound `const Key&` without converting, and with a
  // transparent comparator compared directly - which is why the tests pass either way.
  // The constraint is here so the dispatch is stated rather than inferred.
  template<typename K>
  requires(!IsIterator<K>::value && !std::same_as<std::remove_cvref_t<K>, std::remove_cvref_t<Key>>)
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  constexpr size_type erase(K&& key) {
    size_type count = 0;
    if constexpr (kTransparent) {
      // Nothing to convert: `find` compares the foreign key directly, so no `Key` is
      // ever built. `key` is therefore never forwarded - there is no target to move
      // into - which is what the NOLINT above is about.
      while (true) {
        auto pos = find(key);
        if (pos == end()) {
          break;
        }
        erase(pos);
        ++count;
      }
    } else {
      // A `Key` has to be materialised, because the comparator can only compare keys.
      //
      // `key` stays a forwarding reference on purpose: callers may pass an lvalue, a
      // temporary or a value, and the compiler picks what is cheapest - a small type in
      // registers beats a const& indirection.
      //
      // It is forwarded EXACTLY ONCE, into this binding, rather than inside the loop. An
      // object can only be forwarded once, so forwarding per iteration would claim a move
      // of `key` every time round (bugprone-use-after-move), and there is no "last
      // iteration" to single out because the loop runs until `find` fails. Measured with a
      // `Key` having both converting constructors, over three iterations:
      //   * `find(key)` inside the loop:                   3 copy-conversions
      //   * `find(std::forward<K>(key))` inside the loop:   3 move-conversions (and unsound)
      //   * forwarded once into this binding:               1 conversion, a move for an rvalue
      const Key& key_ref = std::forward<K>(key);
      while (true) {
        auto pos = find(key_ref);
        if (pos == end()) {
          break;
        }
        erase(pos);
        ++count;
      }
    }
    return count;
  }

  constexpr std::pair<iterator, bool> insert(const value_type& value) { return emplace(value); }

  constexpr std::pair<iterator, bool> insert(value_type&& value) { return emplace(std::move(value)); }

  template<std::input_iterator InputIt>
  constexpr void insert(InputIt first, InputIt last) {
    LimitedOrdered incoming(key_comp_);
    while (first != last) {
      incoming.emplace(*first++);
    }
    for (size_type pos = 0; pos < incoming.size_; ++pos) {
      emplace(std::move(incoming.values_[pos].data));
    }
  }

  // Map-types only

  template<typename... Args>
  requires(!kKeyOnly)
  constexpr std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    const iterator dst = lower_bound(key);
    if (dst != end() && !key_comp_(dst->first, key) && !key_comp_(key, dst->first)) {
      return std::make_pair(dst, false);
    }
    RawValue new_val(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(std::forward<Args>(args)...));
    const auto index = static_cast<size_type>(dst - begin());
    return std::make_pair(InsertStaged(index, std::move(new_val)), true);
  }

  template<typename... Args>
  requires(!kKeyOnly)
  constexpr std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
    const iterator dst = lower_bound(key);
    if (dst != end() && !key_comp_(dst->first, key) && !key_comp_(key, dst->first)) {
      return std::make_pair(dst, false);
    }
    RawValue new_val(
        std::piecewise_construct, std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(std::forward<Args>(args)...));
    const auto index = static_cast<size_type>(dst - begin());
    return std::make_pair(InsertStaged(index, std::move(new_val)), true);
  }

  template<class V>
  requires(!kKeyOnly)
  constexpr std::pair<iterator, bool> insert_or_assign(const Key& key, V&& value) {
    const iterator dst = lower_bound(key);
    if (dst != end() && !key_comp_(dst->first, key) && !key_comp_(key, dst->first)) {
      dst->second = std::forward<V>(value);
      return std::make_pair(dst, false);
    }
    RawValue new_val(key, std::forward<V>(value));
    const auto index = static_cast<size_type>(dst - begin());
    return std::make_pair(InsertStaged(index, std::move(new_val)), true);
  }

  template<class V>
  requires(!kKeyOnly)
  constexpr std::pair<iterator, bool> insert_or_assign(Key&& key, V&& value) {
    const iterator dst = lower_bound(key);
    if (dst != end() && !key_comp_(dst->first, key) && !key_comp_(key, dst->first)) {
      dst->second = std::forward<V>(value);
      return std::make_pair(dst, false);
    }
    RawValue new_val(std::move(key), std::forward<V>(value));
    const auto index = static_cast<size_type>(dst - begin());
    return std::make_pair(InsertStaged(index, std::move(new_val)), true);
  }

  // Read/write access

  MBO_FORCE_INLINE constexpr std::size_t size() const noexcept { return size_; }

  constexpr std::size_t max_size() const noexcept { return Capacity; }

  constexpr std::size_t capacity() const noexcept { return Capacity; }

  constexpr bool empty() const noexcept { return size_ == 0; }

  // NOLINTBEGIN(readability-container-data-pointer)

  constexpr iterator begin() noexcept { return iterator(&values_[0]); }

  constexpr const_iterator begin() const noexcept { return const_iterator(&values_[0]); }

  constexpr const_iterator cbegin() const noexcept { return const_iterator(&values_[0]); }

  MBO_FORCE_INLINE constexpr iterator end() noexcept { return iterator(&values_[size_]); }

  MBO_FORCE_INLINE constexpr const_iterator end() const noexcept { return const_iterator(&values_[size_]); }

  MBO_FORCE_INLINE constexpr const_iterator cend() const noexcept { return const_iterator(&values_[size_]); }

  constexpr reverse_iterator rbegin() noexcept { return std::make_reverse_iterator(end()); }

  constexpr const_reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }

  constexpr const_reverse_iterator crbegin() const noexcept { return std::make_reverse_iterator(end()); }

  MBO_FORCE_INLINE constexpr reverse_iterator rend() noexcept { return std::make_reverse_iterator(begin()); }

  MBO_FORCE_INLINE constexpr const_reverse_iterator rend() const noexcept {
    return std::make_reverse_iterator(begin());
  }

  MBO_FORCE_INLINE constexpr const_reverse_iterator crend() const noexcept {
    return std::make_reverse_iterator(cbegin());
  }

  // NOLINTEND(readability-container-data-pointer)

  // Observers

  constexpr key_compare key_comp() const { return key_comp_; }

  constexpr value_compare value_comp() const { return val_comp_; }

 protected:
  template<typename U>
  constexpr void Append(U&& value) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `insert` at capacity.");
    std::construct_at(&values_[size_].data, std::forward<U>(value));
    ++size_;
  }

  constexpr iterator InsertStaged(size_type index, RawValue&& value) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `insert` at capacity.");
    const size_type old_size = size_;
    RightShiftGuard guard(this, old_size);
    for (size_type src = old_size; src > index; --src) {
      std::construct_at(&values_[src].data, std::move(values_[src - 1].data));
      std::destroy_at(&values_[src - 1].data);
      guard.SetHole(src - 1);
    }
    std::construct_at(&values_[index].data, std::move(value));
    size_ = old_size + 1;
    guard.Release();
    return begin() + static_cast<difference_type>(index);
  }

  constexpr iterator EraseIndex(size_type index) {
    const size_type old_size = size_;
    std::destroy_at(&values_[index].data);
    LeftShiftGuard guard(this, old_size, index);
    for (size_type src = index + 1; src < old_size; ++src) {
      std::construct_at(&values_[src - 1].data, std::move(values_[src].data));
      std::destroy_at(&values_[src].data);
      guard.SetHole(src);
    }
    size_ = old_size - 1;
    guard.Release();
    return begin() + static_cast<difference_type>(index);
  }

  // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
  static constexpr const Key& GetKey(const Key& key) noexcept { return key; }

  static constexpr const Key& GetKey(const Value& val) noexcept
  requires(!kKeyOnly)
  {
    return val.first;
  }

  MBO_ALWAYS_INLINE static constexpr const Key& GetKey(const Data& data) noexcept {
    if constexpr (kKeyOnly) {
      return data.data;
    } else {
      return data.data.first;
    }
  }

 private:
  std::size_t size_{0};

  // Array would be better but that does not work with ASAN builds.
  // std::array<Data, Capacity == 0 ? 1 : Capacity> values_;
  Data values_[Capacity + 1];  // NOLINT(*-avoid-c-arrays)

  key_compare key_comp_ = {};
  value_compare val_comp_ = value_compare(key_comp_);
};

// NOLINTEND(readability-identifier-naming)

// NOLINTEND(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)

}  // namespace mbo::container::container_internal

#ifdef MBO_FORCE_INLINE
# undef MBO_FORCE_INLINE
#endif

#ifdef MBO_ALWAYS_INLINE
# undef MBO_ALWAYS_INLINE
#endif

#endif  // MBO_CONTAINER_INTERNAL_LIMITED_ORDERED_H_
