# `mbo/types`

`mbo::types` is a type and concept support library. It provides type traits and concepts,
compile-time type selection and search, tuple and aggregate utilities, value and reference wrappers,
and strong identifiers.

The library also provides the very powerful and customizable `Extend` framework that builds on the
support. It uses a CRTP approach to equip any C++ aggregates with comparison, hashing, construction
helpers, and configurable text output.

The library targets C++23, GCC 15+, and Clang 22+.

## Type traits, concepts, and compile-time support

- Namespace: `mbo::types`, except `mbo::StringOrView`. Extenders live in
  `mbo::types::extender` and are also available through `mbo::extender`.
- Build definitions: [BUILD.bazel](BUILD.bazel).
- Each public header below has a target named `//mbo/types:<header-stem>_cc`; for example,
  `tstring.h` uses `//mbo/types:tstring_cc`.

| Header                                 | Facilities                                                                                                       |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| [cases.h](cases.h)                     | Conditional type selection with `Cases`, `IfThen`, and `IfElse`; condition lookup with `CaseIndex`.              |
| [template_search.h](template_search.h) | Compile-time predicate searches over integer ranges.                                                             |
| [traits.h](traits.h)                   | Type classification, constructibility, aggregate decomposition, container capabilities, and comparison concepts. |
| [tuple.h](tuple.h)                     | Tuple recognition and type-list composition with `TupleCat` and `TupleAdd`.                                      |
| [tuple_extras.h](tuple_extras.h)       | Aggregate-to-tuple views, decomposition concepts, and field construction checks.                                 |
| [variant.h](variant.h)                 | Variant recognition, alternative membership, and overloaded visitors.                                            |

### Type traits and concepts

| API family in `traits.h`                                                   | Contract                                                                                                                    |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `ConstructibleFrom<T, Args...>`                                            | Construction check with an additional in-place construction check when `std::constructible_from` fails.                     |
| `ConstructibleInto<From, Into>`                                            | Reversed parameter order for constraining arguments with `ConstructibleInto<T> auto&&`.                                     |
| `DecomposeCountV<T>`, `NotDecomposableV`                                   | Structured-binding field count and the sentinel for unsupported decomposition.                                              |
| `IsAggregate`, `IsDecomposable`, `IsEmptyType`                             | Aggregate, nonempty structured-binding, and empty-type classification.                                                      |
| `IsArithmetic`, `IsFloatingPoint`, `IsIntegral`, `IsScalar`                | Standard numeric/scalar classifications; reference types do not satisfy them.                                               |
| `IsBracesConstructibleV<T, Args...>`                                       | Check construction with braces.                                                                                             |
| `IsCharArray`, `IsInitializerList`, `NotInitializerList`, `NotIsCharArray` | Initializer-list and character-array/pointer classification.                                                                |
| `IsConstructibleWithEmptyBaseAndArgs`                                      | Check aggregate initialization with an empty base followed by field arguments.                                              |
| `IsOptional`, `IsPair`, `IsPairFirstStr`, `IsReferenceWrapper`, `IsVector` | Recognize the corresponding standard types; `IsPairFirstStr` additionally requires a string-view-convertible first element. |
| `IsSameAsAnyOf`, `NotSameAsAnyOf`                                          | Test inclusion in or exclusion from a list of exact types.                                                                  |
| `IsSameAsAnyOfRaw`, `IsSameAsRaw`, `NotSameAsAnyOfRaw`                     | Compare types after removing cv-qualification and references.                                                               |
| `IsSmartPtr`, `IsSmartPtrImpl`                                             | Recognize `shared_ptr`, default-deleter `unique_ptr`, and `weak_ptr`; extend through `IsSmartPtrImpl` specializations.      |
| `ThreeWayComparableTo<L, R, Cat>`                                          | Check `L <=> R` against a comparison category without requiring a common reference or the reverse expression.               |

- `Raw` type comparisons normalize cv/ref qualifiers; exact-type concepts such as `IsPair` and
  `IsOptional` require callers to normalize their input when necessary.
- `IsCharArray` applies `std::decay_t`; both character arrays and `char*`/`const char*` satisfy it.
- `IsDecomposable` requires a nonempty decomposition. `CanCreateTuple` in `tuple_extras.h` also
  accepts empty aggregates.
- Aggregates whose field count needs explicit disambiguation can provide
  `friend consteval std::size_t MboTypesDecomposeCount(const T*)`; the returned count must match the
  actual structured binding.

| Container/iterator API in `traits.h`                                                           | Contract                                                                                                       |
| ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `ContainerConstIteratorValueType`, `ForwardIteratorValueType`                                  | Obtain element types from iterators.                                                                           |
| `ContainerCopyConvertible<In, Out, Func>`                                                      | Check supported traversal and insertion, optionally after transforming input elements with `Func`.             |
| `ContainerHasEmplace`, `ContainerHasEmplaceBack`, `ContainerHasInsert`, `ContainerHasPushBack` | Detect supported insertion forms for a value type.                                                             |
| `ContainerHasForwardIterator`, `ContainerHasInputIterator`                                     | Iterator-category checks with matching mutable/const endpoints.                                                |
| `ContainerIsForwardIteratable`                                                                 | `IsForwardIteratable` plus consistent signed `difference_type`, `cbegin()`, `cend()`, `empty()`, and `size()`. |
| `GetDifferenceType`, `HasDifferenceType`, `HasValueType`                                       | Detect aliases; `GetDifferenceType` falls back to `std::ptrdiff_t`.                                            |
| `IsForwardIteratable`                                                                          | Forward iterators, matching reference aliases, and mutable/const `begin()` and `end()`.                        |
| `IsStringKeyedContainer`                                                                       | Forward-iterable container of pairs with string-view-convertible keys.                                         |

### Conditional types and predicate searches

- `Cases<IfThen<condition, T>..., IfElse<Default>>`: the first matching type; `void` when no case
  matches and no default is supplied.
- `IfElse<T>`: always true; place it last.
- `IfTrueThenVoid` and `IfFalseThenVoid`: unconditional `void` fallback and skipped-case helpers.
- `CaseIndex<conditions...>`: one-based index of the first true condition; zero if none matches.

```c++
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "mbo/types/cases.h"
#include "mbo/types/template_search.h"

using Selected = mbo::types::
    Cases<mbo::types::IfThen<false, int>, mbo::types::IfThen<true, std::string_view>, mbo::types::IfElse<void>>;
static_assert(std::same_as<Selected, std::string_view>);
static_assert(mbo::types::CaseIndex<false, true, true> == 2);

template<std::size_t N>
struct Fits : std::bool_constant<(N <= 8)> {};

static_assert(mbo::types::BinarySearchV<Fits, 0, 16> == 8);
```

All searches take `<Predicate, Start, End, NotFound = End>`, search the half-open range
`[Start, End)`, and expose both `Search::value` and the corresponding `SearchV` constant.
`Predicate<N>` supplies a boolean constant `value`.

| Search          | Result and predicate requirement                                                         |
| --------------- | ---------------------------------------------------------------------------------------- |
| `BinarySearch`  | Highest true index for a predicate that is true before its boundary and false afterward. |
| `LinearSearch`  | First true index; no monotonicity requirement.                                           |
| `MaxSearch`     | Last index in the initial consecutive true run; returns `NotFound` if `Start` is false.  |
| `ReverseSearch` | Last true index; no monotonicity requirement.                                            |

### Tuple types and variant visitors

- `IsTuple<T>` and `IsVariant<T>`: recognize exact `std::tuple` and `std::variant` specializations.
- `TupleCat<T, U>`: concatenate two tuple type lists.
- `TupleAdd<T, U>`: append one type; a tuple supplied as `U` remains a nested tuple element.
- `IsVariantMemberType<Variant, T>`: check whether `T` is an alternative.
- `Overloaded{callables...}`: combine call operators for `std::visit`.
- `TupleFieldsConstructible<Fields, Args>`: equal-length tuple types whose corresponding fields
  can each be constructed from the supplied argument type.
- `CanCreateTuple`, `StructToTuple`, `HasUnionMember`, and `HasVariantMember`: aggregate support
  described under [structural reflection and tuples](#structural-reflection-and-tuples).

```c++
#include <concepts>
#include <string>
#include <tuple>
#include <variant>

#include "mbo/types/tuple.h"
#include "mbo/types/variant.h"

using Fields = mbo::types::TupleAdd<std::tuple<int>, std::string>;
static_assert(std::same_as<Fields, std::tuple<int, std::string>>);
using Value = std::variant<int, std::string>;
static_assert(mbo::types::IsVariantMemberType<Value, std::string>);

std::string Describe(const Value& value) {
  return std::visit(
      mbo::types::Overloaded{
          [](int number) { return std::to_string(number); },
          [](const std::string& text) { return text; },
      },
      value);
}
```

## Value utilities

| Header                                           | Main API                                      | Purpose                                                                              |
| ------------------------------------------------ | --------------------------------------------- | ------------------------------------------------------------------------------------ |
| [compare.h](compare.h)                           | `CompareLess`, numeric comparison helpers     | Boolean and three-way comparisons, including mixed numeric types.                    |
| [container_proxy.h](container_proxy.h)           | `ContainerProxy`                              | Container access through an owning wrapper or holder.                                |
| [no_destruct.h](no_destruct.h)                   | `NoDestruct`                                  | In-place construction without running the contained destructor.                      |
| [opaque.h](opaque.h)                             | `OpaqueContainer`, `OpaquePtr`, `OpaqueValue` | Heap ownership with ADL construction/destruction hooks.                              |
| [optional_data_or_ref.h](optional_data_or_ref.h) | `OptionalDataOrConstRef`, `OptionalDataOrRef` | Empty, owned-value, or borrowed-reference storage.                                   |
| [optional_ref.h](optional_ref.h)                 | `OptionalRef`                                 | Optional non-owning reference with write-through assignment and explicit rebinding.  |
| [ref_wrap.h](ref_wrap.h)                         | `RefWrap`                                     | Required non-owning reference with explicit pointer-style access.                    |
| [required.h](required.h)                         | `Required`                                    | Always-engaged storage that can reconstruct non-assignable values.                   |
| [string_or_view.h](string_or_view.h)             | `mbo::StringOrView`                           | Read-only text with owning and borrowed alternatives.                                |
| [strong_id.h](strong_id.h)                       | `ConstStrongId`                               | Nominal unsigned identifiers with an invalid sentinel.                               |
| [strong_ordinal.h](strong_ordinal.h)             | `ConstStrongOrdinal`, `StrongOrdinal`         | Nominal integral ordinals with checked arithmetic.                                   |
| [tstring.h](tstring.h)                           | `operator""_ts`, `tstring`                    | String contents encoded in the type; one-byte objects with static character storage. |
| [typed_view.h](typed_view.h)                     | `TypedView`                                   | Range-view adapter with container-style element aliases.                             |

### `tstring.h`: one-byte typed strings

`tstring<'p', 'o', 'r', 't'>` represents the text `"port"` in its template arguments.
The `_ts` literal constructs that type directly from a string literal.

- Object layout: no non-static data members; `sizeof("port"_ts) == 1`, independently of text length.
- Character storage: a static `constexpr std::array<char, size() + 1>` per specialization, including
  a trailing zero. The one-byte object size excludes this shared storage.
- Type identity: identical character sequences produce the same type; different sequences produce
  different types, even when their lengths match.
- Lifetime: `str()` views and `c_str()` pointers refer to static storage and outlive temporary
  `tstring` objects.
- Construction: import `mbo::types::operator""_ts`, then use `"text"_ts`. The literal uses standard
  class non-type template arguments.
- Mutation: none. Concatenation and template substring operations produce new string types.

```c++
#include <concepts>
#include <string_view>

#include "mbo/types/tstring.h"

using mbo::types::operator""_ts;
using Port = decltype("port"_ts);

static_assert(std::same_as<Port, mbo::types::tstring<'p', 'o', 'r', 't'>>);
static_assert(!std::same_as<Port, decltype("host"_ts)>);
static_assert(sizeof(Port) == 1);

inline constexpr auto kKey = "http."_ts + "port"_ts;
static_assert(sizeof(kKey) == 1);
static_assert(kKey.size() == 9);
static_assert(kKey.substr<5>() == "port"_ts);
static_assert(kKey.find("port"_ts) == 5);
static_assert("a\0b"_ts.size() == 3);
inline constexpr std::string_view kText = kKey.str();

template<auto Key>
struct NamedInt {
  static constexpr auto kName = Key;
  int value;
};

using PortSetting = NamedInt<"port"_ts>;
using HostSetting = NamedInt<"host"_ts>;
static_assert(!std::same_as<PortSetting, HostSetting>);
inline constexpr PortSetting kPort{8'080};
```

`NamedInt` stores only its `int` member; its key is a template argument and a static member.
The key can also select an overload, specialize a template, or name a custom extender.

| Operation                                                | Result                                                                                            |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `back()`, `front()`                                      | Character references; require a nonempty string.                                                  |
| `begin()`/`end()`, `cbegin()`/`cend()`, reverse variants | Read-only iteration over the characters.                                                          |
| `c_str()`                                                | Pointer to the zero-terminated static character array.                                            |
| `compare(view)`                                          | Lexicographic comparison against a `std::string_view`.                                            |
| `contains`, `ends_with`, `find`, `rfind`, `starts_with`  | Search with a `tstring` operand; `find` and `rfind` return `npos` when absent.                    |
| `empty()`, `length()`, `max_size()`, `size()`            | Compile-time size queries; embedded zero characters count toward length.                          |
| `find_first_of`, `find_last_of`                          | Search by a character or a `std::string_view` character set, with an optional start position.     |
| `Hash()`, `StringHash()`                                 | `Hash()`: distinct typed hash; `StringHash()`: MBO string hash. Both support constant evaluation. |
| `is(other)`                                              | Exact `tstring` type-identity test.                                                               |
| `operator std::string_view`                              | Explicit conversion to a view of the characters.                                                  |
| `operator+`                                              | Concatenate two `tstring` values into a new `tstring` type.                                       |
| `operator<<`                                             | Stream the text.                                                                                  |
| `operator==`, `operator<=>`                              | Equality and lexicographic comparison; support string-view-compatible operands.                   |
| `str()`                                                  | View of all characters, excluding the added terminator.                                           |
| `substr(pos, count)`                                     | `std::string_view` result; supports runtime positions.                                            |
| `substr<Pos, Count>()`                                   | New `tstring` type; count defaults to `npos` and is clamped to the remaining length.              |

- Accessors are static `constexpr`; `Port::str()` and `Port{}.str()` access the same storage.
- Both substring forms return an empty result when `pos >= size()`.
- `_ts` retains embedded zero characters; C-string consumers of `c_str()` stop at the first zero.
- `make_tstring<array>()`: construct from a suitable static character array; stops at the first zero.
  `MBO_MAKE_TSTRING(array)` derives the length from the array extent. Prefer `_ts` for literals.
- Hash integration: `AbslHashValue` and a `std::hash<const tstring<...>>` specialization. For a
  `constexpr auto kName = "name"_ts`, use `std::hash<decltype(kName)>`. Typed hashes do not promise
  compatibility with string/string-view hashes, and their numeric values are not a stable format.

### `string_or_view.h`: owned or borrowed text

- Type name: `mbo::StringOrView`.
- `std::string` input: own the string; lvalues are copied and rvalues are moved.
- `std::string_view` or character-array input: borrow the storage. Array construction uses `N - 1`
  characters. The referenced character storage must outlive the wrapper and its copies.
- Runtime C-string pointers: explicitly choose `std::string` for ownership or `std::string_view`
  for borrowing.
- `owns_string()`: inspect the active alternative. Copying preserves ownership/borrowing mode.
- `view()` and implicit `std::string_view` conversion: read the current text.
- Read API: iterators, element access, size queries, `copy`, `substr`/`subview`, comparisons,
  prefix/suffix/containment checks, and the `find`/`rfind`/character-set search families.
- `substr()` and `subview()` return borrowed views, including when the wrapper owns the source.
- Formatting and hashing: stream output, Abseil stringification/hashing, `std::hash`, and
  `std::formatter` when the standard library provides formatting. Comparisons and hashes use text
  contents, independently of ownership.

```c++
#include <string>
#include <string_view>
#include <utility>

#include "mbo/types/string_or_view.h"

void TextOwnership() {
  std::string source = "borrowed";
  mbo::StringOrView borrowed{std::string_view(source)};
  mbo::StringOrView owned{std::string("owned")};
  mbo::StringOrView literal{"static text"};
  std::string copy(borrowed.view());
  owned = std::move(copy);
  (void)literal;
}
```

### `ref_wrap.h`: required references

- `RefWrap<T>`: non-owning wrapper constructed from an existing `T&`; no default/empty state.
- Assignment from a `T&` or another wrapper: rebind. Assignment through `*ref` or `ref.get()`:
  modify the referenced value.
- Access: `get()`, `*`, `->`, and implicit reference conversion. A const wrapper exposes const access.
- Referent lifetime: must exceed the wrapper's use; wrapping a reference does not extend lifetime.
- Pointer arithmetic and indexing: disabled.
- Three-way comparison, Abseil hashing, and Abseil stringification operate on referenced values.

### `optional_ref.h`: optional references

- `OptionalRef<T>`: default construction or `std::nullopt` creates an empty reference.
- Construction from `T&`: borrow the object. Copy/move construction copies the reference;
  wrapper-to-wrapper assignment is deleted.
- Value assignment: write through an engaged reference. `set_ref(value)`: explicitly rebind.
  `reset()`: disengage without changing or destroying the referent.
- `has_value()`/explicit `bool`: engagement. `value()`, `*`, and `->`: access; require engagement.
- Checked access honors `mbo::config::kRequireThrows`; it is `noexcept` only when requirements
  use fatal logging. Write-through assignment also accounts for the referent's assignment exceptions.
- `OptionalRef<const T>`: borrow a const object. Referents must outlive all uses of the wrapper.
- Comparisons: value-based, with empty references ordered before engaged references.
- Abseil hashing includes engagement and value; Abseil stringification emits `std::nullopt` for empty.
- `IsOptionalRef<T>`: recognize this wrapper.

```c++
#include "mbo/types/optional_ref.h"
#include "mbo/types/ref_wrap.h"

void ReferenceAssignment() {
  int first = 1;
  int second = 2;
  mbo::types::RefWrap<int> required(first);
  required = second;  // Rebind to second.
  *required = 3;      // Write second.
  mbo::types::OptionalRef<int> optional(first);
  optional = 4;              // Write first.
  optional.set_ref(second);  // Rebind to second.
  optional.reset();          // Leave second unchanged.
}
```

### `optional_data_or_ref.h`: optional ownership

`OptionalDataOrRef<T, RefT = T>` holds one of three states: empty, an owned `T`, or a borrowed
`RefT&`. `T` and `RefT` must name the same underlying type after removing cv/ref qualifiers and
must not themselves be references. `OptionalDataOrConstRef<T>` selects `RefT = const T`.

| Operation                                           | Effect                                                                                                                   |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Construction from a compatible lvalue               | Borrow the object.                                                                                                       |
| Construction from `T&&`                             | Own a moved-in value.                                                                                                    |
| Default construction, `std::nullopt`                | Empty state.                                                                                                             |
| `as_data(args...)`                                  | Keep an owned value, copy a borrowed value into owned storage, or construct an empty wrapper's value from the arguments. |
| `emplace(args...)`                                  | Own a value constructed from the arguments.                                                                              |
| `get()`                                             | Current value or a static default value for suitable `T`.                                                                |
| `get(defaults)`                                     | Current value or the supplied fallback, by const reference.                                                              |
| `has_value()`, explicit `bool`                      | True for either owned or borrowed storage.                                                                               |
| `HoldsData()`, `HoldsNullopt()`, `HoldsReference()` | Distinguish the three storage states.                                                                                    |
| `reset()`                                           | Empty the wrapper; destroy any previously owned value.                                                                   |
| `set_ref(value)`                                    | Replace the current state with a borrowed reference.                                                                     |
| `value()`, `*`, `->`                                | Access the current value; require engagement.                                                                            |

- Copying owned storage copies `T`; copying borrowed storage preserves the reference.
- Moving owned storage moves `T` and empties the source; moving borrowed storage preserves the
  reference in both wrappers.
- Rvalue value assignment creates owned storage. Supported lvalue value assignment writes through
  an engaged value or constructs owned storage if empty. Use `set_ref()` to request rebinding.
- Failed replacement construction leaves the wrapper empty when exceptions are enabled.
- Checked access through `value()`, `*`, and `->` honors `mbo::config::kRequireThrows`; it is
  `noexcept` only when requirements use fatal logging.
- Borrowed values and `get(defaults)` fallbacks must remain alive while their returned references
  are used. Do not retain a fallback reference obtained from a temporary argument.
- Comparison and Abseil hashing use engagement and value, independently of ownership mode.
- Abseil stringification supports engaged values and `std::nullopt`.
- `IsOptionalDataOrRef<T>` recognizes both mutable-reference and const-reference forms.

```c++
#include <string>

#include "mbo/types/optional_data_or_ref.h"

void OptionalOwnership() {
  std::string source = "original";
  mbo::types::OptionalDataOrRef<std::string> text(source);
  text.value() += " borrowed";  // Modifies source.
  text.as_data() += " owned";   // Copies first; further changes affect owned data.
  text.reset();
  text.emplace("replacement");
}
```

### `required.h`: reconstructible values

- `Required<T>` stores a `T` directly with no empty state or engagement flag.
- Purpose: replace values whose type supports construction but not assignment, including records
  with const members.
- Construction: from a value, a convertible argument, or `std::in_place` plus constructor arguments.
  Default construction follows `T`'s default construction.
- `emplace(args...)`: first construct a replacement, then destroy the old value and move the
  replacement into place. Requires nothrow move construction of `T`.
- If replacement construction throws, the current value remains alive and unchanged.
- Access: `get()`, `*`, `->`, and implicit reference conversion.
- Three-way comparison, Abseil hashing, and Abseil stringification use the stored value.

```c++
#include <utility>

#include "mbo/types/required.h"

struct Record {
  const int number;
};

void ReplaceRecord() {
  mbo::types::Required<Record> record(std::in_place, 7);
  record.emplace(8);  // Reconstructs Record; no Record assignment operator needed.
}
```

### `no_destruct.h`: suppressed destruction

- `NoDestruct<T>` constructs `T` in internal storage and deliberately never calls `T`'s destructor.
- Intended use: static values that must remain available during program shutdown.
- Construction: default, value, forwarded constructor arguments, or an initializer list.
- Copying and moving: disabled. Access: mutable/const `Get()`, `*`, and `->`.
- Constant evaluation: supported when construction of `T` and all its members permits it.
- Abseil hashing and stringification use the contained value.
- Destruction suppression does not extend the wrapper's storage lifetime. Use static storage for
  objects that must survive until process termination; dynamically acquired resources in `T` are
  not released by this wrapper.

```c++
#include <string>

#include "mbo/types/no_destruct.h"

const std::string& DefaultName() {
  static const mbo::types::NoDestruct<std::string> name("anonymous");
  return *name;
}
```

### `opaque.h`: opaque ownership

| API                                  | Ownership and access                                                                                                          |
| ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| `IsOpaquePtr<T>`, `IsOpaqueValue<T>` | Recognize the pointer and value wrappers.                                                                                     |
| `MakeOpaquePtr<T>(args...)`          | Construct `T` through the ADL maker hook and return its owning pointer.                                                       |
| `MBO_TYPES_OPAQUE_HOOKS(T)`          | Define inline ADL maker/deleter hooks after the complete type definition.                                                     |
| `OpaqueContainer<C>`                 | `ContainerProxy` over `OpaqueValue<C>` for direct container operations.                                                       |
| `OpaquePtr<T>`                       | `std::unique_ptr` with an ADL deleter; nullable, move-only, and usable with a forward-declared `T`.                           |
| `OpaqueValue<T>`                     | Heap-owned value; construction allocates `T`, copying deep-copies when `T` is copy-constructible, moving transfers ownership. |

- Place hooks in the type's associated namespace and make them available where creation and
  destruction are instantiated. Forward declaration alone does not supply the hooks.
- `OpaqueValue` exposes mutable/const `*` and `->`, value-based three-way comparison, Abseil
  hashing, and `std::hash` when supported by `T`.
- A newly constructed `OpaqueValue` owns a value. Moving transfers its pointer and leaves the source
  without a pointee; restore ownership before dereferencing, comparing, hashing, or copying it.

```c++
#include <string>

#include "mbo/types/opaque.h"

namespace application {
struct Payload;
using PayloadPtr = mbo::types::OpaquePtr<Payload>;

struct Payload {
  std::string text;
};
MBO_TYPES_OPAQUE_HOOKS(Payload)

PayloadPtr MakePayload() {
  return mbo::types::MakeOpaquePtr<Payload>("example");
}
}  // namespace application
```

### `container_proxy.h`: forwarding container access

- `ContainerProxy<Holder, Container, GetMutable, GetConst>` inherits the holder and accesses its
  container through two member-function pointers.
- Required accessor signatures: `Container& ()` and `const Container& () const`.
- Default accessors: mutable and const `Holder::operator*`. Supply named accessors when the holder
  lacks those exact signatures; `std::unique_ptr` does not match the mutable default signature.
- Exposes `size_type` and `value_type`, iteration, size/capacity queries, supported reserve/resize,
  element access, endpoint insertion/removal, and supported value-returning endpoint pops.
- `operator[]` delegates to the underlying `at()`; its bounds behavior follows that function.
- `pop_front_value()`/`pop_back_value()` are available only when the container provides them, and
  forward its return value and exception specification.
- Each forwarded member has its own constraints. The proxy is not a complete STL container
  interface; for example, `clear()` currently requires a const-callable underlying `clear()`.
- Container lifetime and ownership follow the holder. A pointer-backed holder must contain a
  usable container before any forwarded operation.

```c++
#include <vector>

#include "mbo/types/container_proxy.h"

struct Values {
  std::vector<int> data;

  std::vector<int>& GetData() { return data; }

  const std::vector<int>& GetData() const { return data; }
};

using ValueProxy = mbo::types::ContainerProxy<Values, std::vector<int>, &Values::GetData, &Values::GetData>;

void UseProxy() {
  ValueProxy values;
  values.push_back(7);
  values.at(0) = 8;
}
```

### `typed_view.h`: views with element aliases

- `TypedView<View>` stores a moved-in view and provides `value_type`, `reference`, and
  `difference_type`; this supports consumers such as GoogleTest container matchers.
- Construction: pass a view rvalue, or move an existing view; no default construction.
- `begin()` and `end()` are const; the underlying view must be const-iterable. Views such as
  `filter_view` and `drop_while_view` do not satisfy this requirement.
- Inherits the applicable `std::ranges::view_interface` operations, including size/index/front
  access when the wrapped range supports them.
- Ownership of elements follows the underlying view. Wrapping `std::views::all(lvalue)` continues
  to borrow the original range.

```c++
#include <ranges>
#include <vector>

#include "mbo/types/typed_view.h"

void UseTypedView() {
  const std::vector<int> values{1, 2, 3};
  auto doubled = mbo::types::TypedView(std::views::transform(values, [](int value) { return value * 2; }));
  for (int value : doubled) {
    (void)value;
  }
}
```

### `compare.h`: comparison policies

| API                                | Behavior                                                                                                                                                                                     |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CompareArithmetic(lhs, rhs)`      | Strong ordering for arithmetic values, including floating point.                                                                                                                             |
| `CompareFloat(lhs, rhs)`           | Strong ordering: NaNs compare equivalent to each other and greater than non-NaNs; signed zeros compare equivalent.                                                                           |
| `CompareIntegral(lhs, rhs)`        | Strong ordering for integral values, with signed/unsigned boundary handling.                                                                                                                 |
| `CompareLess<T>`                   | Transparent comparator (`is_transparent = void`): `operator()` returns less-than and `Compare()` returns the three-way result. Heterogeneous operands must be three-way comparable with `T`. |
| `CompareScalar(lhs, rhs)`          | Scalar comparison, including same-type pointers when their comparison is supported.                                                                                                          |
| `IsCompareLess<T>`, `IsStdLess<T>` | Recognize the respective comparator types.                                                                                                                                                   |
| `WeakToStrong(order)`              | Preserve the less/equivalent/greater result in `std::strong_ordering`.                                                                                                                       |

- Mixed floating/integral comparisons convert to `long double`; precision follows that type.
- In the non-floating mixed-type path, a `bool` operand causes both operands to be compared as
  booleans. For example, `CompareIntegral(true, 2)` reports equivalent.
- These numeric helpers do not make every pair of scalar types comparable; pointer/numeric and
  unrelated pointer combinations still require valid underlying operations.

### `strong_ordinal.h` and `strong_id.h`: nominal integral types

A tag separates application domains that share an integer representation. Construction is explicit;
`value()` retrieves the representation. The wrappers provide same-domain comparison, `std::hash`,
and Abseil hashing.

| Type                                                     | Default and mutation policy                                                                                                |
| -------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `ConstStrongId<Tag, Rep = std::uint32_t, Invalid = max>` | Invalid default; unsigned non-boolean representation; no arithmetic or copy assignment; move assignment remains available. |
| `ConstStrongOrdinal<Tag, Rep>`                           | Zero default; all representation values are valid. No setters or copy assignment; move assignment remains available.       |
| `StrongOrdinal<Tag, Rep>`                                | Mutable ordinal with setters, assignment, compound arithmetic, and increment/decrement.                                    |

- Ordinal representations: integral types other than `bool`; specify `Rep` explicitly.
- `try_from_ordinal(value)`: `std::optional` result; reject values outside the representation range.
  The ID form also rejects its invalid sentinel.
- Ordinal `try_add`/`try_subtract`: optional new value. Mutable `try_set`, `try_add_assign`,
  `try_subtract_assign`, `try_increment`, and `try_decrement`: boolean success and no change on failure.
- Arithmetic offsets must be nonnegative. Overflow, underflow, and negative offsets fail the
  checked operation even when the representation is signed.
- Ordinary `+`, `-`, mutable `set`/assignment, compound arithmetic, and increment/decrement enforce
  the same rules through [configured requirement failures](../config/README.md#requirement-failures).
- `ConstStrongId::is_valid()`: compare against `invalid_value`. Direct construction can represent the
  invalid sentinel; use `try_from_ordinal` when it must be rejected.

```c++
#include <cstdint>

#include "mbo/types/strong_id.h"
#include "mbo/types/strong_ordinal.h"

struct RowTag;
struct UserTag;
using Row = mbo::types::StrongOrdinal<RowTag, std::uint32_t>;
using UserId = mbo::types::ConstStrongId<UserTag>;

static_assert(Row{}.value() == 0);
static_assert(!UserId{}.is_valid());
static_assert(!UserId::try_from_ordinal(-1).has_value());
static_assert(UserId::try_from_ordinal(42)->value() == 42);
static_assert(Row(5).try_add(2)->value() == 7);

void AdvanceRow() {
  Row row(5);
  row += 2;
  ++row;
}
```

## Enriching aggregates with `Extend`

| Header                                     | Entry point                                                                                    |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| [extend.h](extend.h)                       | `Extend`, `ExtendNoDefault`, `ExtendNoPrint`, and the extenders exported through `extender.h`. |
| [stringify.h](stringify.h)                 | Recursive stringification, output modes, and field policies.                                   |
| [stringify_ostream.h](stringify_ostream.h) | Stream adapter for opted-in aggregates and global stream-format settings.                      |

The **`Extend` framework** equips C++ aggregates with comparison, Abseil and standard hashing,
tuple access, construction helpers, and configurable text output through a single CRTP base.
Declare the fields once; `Extend` derives these operations from the aggregate itself and follows
changes to its fields. It preserves aggregate initialization, and its default extenders add no
data members or virtual dispatch.

**Reusable, composable extenders** let applications define a behavior once and apply it across
their record types. Use the defaults, select individual behaviors, or combine them with custom
extenders whose dependencies are checked at compile time. The shared `Stringify` layer provides
recursive formatting, automatic field names where supported, and per-field naming, filtering,
conversion, and redaction.

The separate [reflection implementation note](REFLECTION.md) explains how automatic field-name
discovery works, its compiler-specific limitations, and how mbo compares with other reflection
libraries.

Derive an aggregate from `mbo::types::Extend<T>` to install the common CRTP extenders with one
declaration:

```c++
#include <iostream>
#include <string>

#include "mbo/types/extend.h"

struct Name : mbo::types::Extend<Name> {
  std::string first;
  std::string last;
};

Name name{
    .first = {"Ada"},
    .last = {"Lovelace"},
};
std::cout << name << '\n';
```

That one base gives `Name` comparison, Abseil and standard hashing, Abseil stringification, stream
output, `ToString()`, `ToJsonString()`, and construction helpers. These implementations remain in
sync with the aggregate's declared fields instead of duplicating the field list across hand-written
operators. When field-name reflection supports the aggregate, output can use `first` and `last`;
otherwise it remains positional or uses configured names or numeric fallbacks.

`Extend` preserves aggregate initialization, including designated initializers for the declared
fields. It adds neither data members nor virtual dispatch. The empty CRTP base is initialized
implicitly in the example above. Named construction helpers also accept field arguments or tuples:

```c++
#include <string_view>
#include <tuple>

auto name = Name::ConstructFromArgs("Grace", "Hopper");
auto other = Name::ConstructFromTuple(std::tuple{"Barbara", "Liskov"});
auto converted = Name::ConstructFromConversions(std::string_view{"Margaret"}, std::string_view{"Hamilton"});
```

## Extender sets

| Base                              | Installed behavior                                                                     |
| --------------------------------- | -------------------------------------------------------------------------------------- |
| `Extend<T, Extra...>`             | Default behavior plus any custom extenders.                                            |
| `ExtendNoDefault<T, Extender...>` | Only the explicitly selected extenders.                                                |
| `ExtendNoPrint<T, Extra...>`      | Comparison, hashing, and Abseil stringification, without `ToString()` or `operator<<`. |

| Extender                  | Installed behavior                                                             |
| ------------------------- | ------------------------------------------------------------------------------ |
| `extender::AbslHashable`  | `AbslHashValue`; extended types also receive `std::hash`.                      |
| `extender::AbslStringify` | Abseil formatting backed by `Stringify`.                                       |
| `extender::Comparable`    | `<=>`, `==`, and `<`, with the remaining relational operators supplied by C++. |
| `extender::Default`       | `AbslHashable`, `AbslStringify`, `Comparable`, `Printable`, and `Streamable`.  |
| `extender::NoPrint`       | `AbslHashable`, `AbslStringify`, and `Comparable`.                             |
| `extender::Printable`     | `ToJsonString()` and `ToString()`; requires `AbslStringify`.                   |
| `extender::Streamable`    | `operator<<`; requires `AbslStringify`.                                        |

Use `ExtendNoDefault` when a type must expose only a narrow operation set. Use `ExtendNoPrint` when
the comparison and hashing semantics are useful but text output has a hand-written implementation.
Additional extenders can be defined with `MakeExtender`; dependencies between extenders are checked
while the CRTP chain is assembled.

`extender::Comparable` compares fields in declaration order through the aggregate's tuple view. It
defines `<=>`, `==`, and `<` for two values of the extended type; C++ rewriting supplies `!=`, `<=`,
`>`, and `>=`. Member templates also compare an extended value with another decomposable aggregate
or a compatible `std::tuple`. Field types must provide the operations needed by the requested
comparison.

## Structural reflection and tuples

The structural layer determines an aggregate's field count and maps its structured binding to a
tuple of references. It is independent of whether source field names are available.

- `CanCreateTuple<T>`: eligible structured-binding aggregates and empty aggregates.
- `StructToTuple(value)`: tuple of field references; an empty tuple for an empty aggregate.
- `HasUnionMember<T>` and `HasVariantMember<T>`: inspect reflected tuple element types for union
  and `std::variant` members, respectively. These shapes affect constant-evaluated field-name
  discovery; see [REFLECTION.md](REFLECTION.md).

```c++
#include "mbo/types/tuple_extras.h"

Name name = Name::ConstructFromArgs("Katherine", "Johnson");
auto fields = mbo::types::StructToTuple(name);
std::get<1>(fields) = "Coleman Goble Johnson";
```

`StructToTuple` preserves reference and cv-qualification appropriate to its argument. The tuple is
a view of the aggregate, not a detached value. `Extend` uses the same representation for generated
comparison and hashing, so those operations follow field declaration order.

The decomposition machinery supports aggregates only up to its generated maximum arity. Anonymous
structs or unions are not supported as members of an extended type. Other difficult shapes can be
structurally decomposable even when automatic names are unavailable; callers should not treat
tuple support and name support as the same capability.

## Field names

Automatic names are best-effort metadata because C++23 has no standard field-name reflection:

- GCC 14+ extracts constexpr names from field-address expressions and compiler signatures.
- Clang 22+ uses `__builtin_dump_struct`; eligible types are constexpr, with a runtime path for
  additional types.
- unsupported compilers and unsupported field shapes retain positional traversal;
- explicit application names always remain available and can override compiler names.

The detailed matrix for unions, bit-fields, reference members, constexpr evaluation, and external
implementations lives in [REFLECTION.md](REFLECTION.md).

## Stringification

The `Stringify` constructor requires complete default options and checks them through
`MBO_CONFIG_REQUIRE`. Its `noexcept` contract follows `mbo::config::kRequireThrows`; incomplete
defaults throw under the enabled exception policy. The named factories validate their built-in
options at compile time and remain `noexcept`.

`Stringify` recursively handles supported aggregates, strings, characters, pointers, optionals,
variants, tuples, and containers. Ready-made modes provide default, compact or pretty C++, and
compact, line-oriented, or pretty JSON-like output:

```c++
#include "mbo/types/stringify.h"

std::string cpp = mbo::types::Stringify::AsCpp().ToString(name);
std::string pretty = mbo::types::Stringify::AsCppPretty().ToString(name);
std::string json = mbo::types::Stringify::AsJson().ToString(name);
```

JSON modes need keys. They use automatic or explicit field names when available and numeric keys as
a fallback. `ToJsonString()` is a convenience supplied by `extender::Printable`.

Formatting is controlled by sparse `StringifyOptions`. Its groups cover syntax, field inclusion,
key selection and renaming, value escaping and replacement, container and string limits, and
special handling for string-keyed pair containers. Outer and nested values can have different
policies through `StringifyFieldOptions`; `StringifyRootOptions` independently controls the root.

### Custom names and field policy

The simplest stable schema supplies names explicitly:

```c++
struct Credentials : mbo::types::Extend<Credentials> {
  std::string user;
  std::string secret;

  friend constexpr auto MboTypesStringifyFieldNames(const Credentials&) {
    return std::array<std::string_view, 2>{"user", "secret"};
  }
};
```

For per-field behavior, define `MboTypesStringifyOptions`. It receives the object plus
`StringifyFieldInfo`, including the field index, discovered name, and inherited options. Policy can
therefore depend on the type, field, surrounding context, or runtime object state.

| Extension point                         | Purpose                                                                          |
| --------------------------------------- | -------------------------------------------------------------------------------- |
| `MboTypesStringifyConvert`              | Convert a field using its owner, index, and value before rendering.              |
| `MboTypesStringifyDisable`              | Prevent automatic recursive rendering for a type.                                |
| `MboTypesStringifyDoNotPrintFieldNames` | Suppress compiler-provided names while retaining traversal.                      |
| `MboTypesStringifyFieldNames`           | Supply the complete stable name sequence or replace compiler names.              |
| `MboTypesStringifyOptions`              | Choose formatting and policy for each object and field.                          |
| `MboTypesStringifySupport`              | Opt a non-extended aggregate into `Stringify` traversal.                         |
| `MboTypesStringifyValueAccess`          | Present a wrapper as a different underlying value.                               |
| `StringifyOptions::FieldControl`        | Suppress fields, null pointers, empty optionals, or disabled values.             |
| `StringifyOptions::KeyOverrides`        | Rename a field statically or through a callback.                                 |
| `StringifyOptions::ValueOverrides`      | Replace or redact string and non-string values.                                  |
| `StringifyWithFieldNames`               | Adapt a name sequence into field options, optionally verifying discovered names. |

Automatic source identifiers are convenient diagnostics, but explicit names are preferable for a
stable external schema. Formatting JSON-like text does not make `Stringify` a validating serializer;
use a serialization library when schema evolution, parsing, or protocol guarantees are required.

### Stream formatting

- `stringify_ostream.h` adds a global stream insertion operator for types satisfying
  `HasMboTypesStringifySupport`, including aggregates declaring
  `using MboTypesStringifySupport = void`.
- `SetStringifyOstreamOutputMode(mode)` and `SetStringifyOstreamOptions(options)` configure the
  adapter globally, across streams. Configuration access is thread-safe; another thread can change
  it between a setter call and a subsequent output operation.
- `SetStringifyOstreamOptions` accepts an options lvalue; its rvalue overload is deleted.
- The adapter is separate from `extender::Streamable`. For output with a particular formatting policy,
  use a `Stringify` instance's `Stream(out, value)` or `ToString(value)` directly.

The repository's root [README](../../README.md) lists the complete header and Bazel target inventory.
