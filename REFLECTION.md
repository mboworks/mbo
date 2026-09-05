# Aggregate reflection and field metadata

This document describes mbo's aggregate-introspection facilities, how field names feed the rest of
the library, and the compiler backends that provide field names. It also records the relevant
non-standard reflection implementations and their practical limitations.

The implementation described here is reflection in the informal, pre-C++26 sense. C++23 has no
standard facility for enumerating the non-static data members of an arbitrary type or obtaining
their identifiers. Every automatic field-name implementation therefore relies on a compiler
extension or on parsing compiler-generated function signatures.

## Summary

mbo already separates three related capabilities:

1. **Structural decomposition** determines how many fields an aggregate has and produces a tuple of
   references to them. This underpins comparison, hashing, construction, and printing.
2. **Field-name discovery** obtains the source identifiers corresponding to those fields. Clang uses
   `__builtin_dump_struct`; GCC uses field addresses and compiler-signature parsing.
3. **Field policy and presentation** decide whether and how each discovered field is used. This is
   considerably more sophisticated than merely supplying `MboTypesStringifyFieldNames`.

GCC 14 provides constexpr names for ordinary decomposable aggregates. The implementation uses an
undefined external object, obtains the address of each field through structured binding,
passes that address as a non-type template argument, and extracts the member identifier from
`__PRETTY_FUNCTION__` or `std::source_location::function_name()`.

The dedicated `struct_names_gcc.h` backend reuses mbo's existing decomposition machinery. It retains
all manual extension points and reports unsupported field shapes through the existing concepts.

## Current mbo model

### Structural decomposition

[`mbo/types/internal/decompose_count.h`](mbo/types/internal/decompose_count.h) determines whether a
type is decomposable and generates structured-binding implementations for supported arities.
[`mbo/types/tuple_extras.h`](mbo/types/tuple_extras.h) exposes `StructToTuple`, whose result contains
references to the aggregate fields.

This is useful without names. Positional field access is enough to implement:

- equality and ordering;
- Abseil and standard hashing;
- construction from arguments or tuples;
- generic field traversal;
- positional stringification.

The decomposition layer and field-name layer should remain separate. A compiler may support one
without supporting the other, and some field shapes can be decomposed but cannot be represented as
an addressable non-type template argument.

### Clang field-name discovery

[`mbo/types/internal/struct_names_clang.h`](mbo/types/internal/struct_names_clang.h) uses
`__builtin_dump_struct` with a replacement callback. The callback receives the format string and
arguments that Clang would normally print and records top-level field names instead.

The public internal facade in
[`mbo/types/internal/struct_names.h`](mbo/types/internal/struct_names.h) exposes:

- `kStructNameSupport`;
- `SupportsFieldNames<T>`;
- `SupportsFieldNamesConstexpr<T>`;
- `GetFieldNames<T>()`.

For eligible literal, default-constructible types, the names are computed at compile time. A
runtime-initialized specialization supports additional non-literal types and types without a usable
default constructor. Types containing union members are currently excluded.

### GCC field-name discovery

[`mbo/types/internal/struct_names_gcc.h`](mbo/types/internal/struct_names_gcc.h) uses the address of
each member of an undefined external object as a non-type template argument. GCC includes that
address expression in `__PRETTY_FUNCTION__`, from which the backend extracts and stores the member
identifier entirely at compile time. No `T` object is constructed and no field value is read.

The backend validates the parser with a sentinel member and copies each extracted name into compact
constexpr storage. Types whose fields cannot supply the required address expression, such as
reference members and bit-fields, do not satisfy `SupportsFieldNames`.

### Field names are metadata, not the printing policy

Automatic or manually supplied names become inputs to
[`mbo/types/stringify.h`](mbo/types/stringify.h). They do not determine by themselves whether a
field is printed, how it is named in the output, or how its value is rendered.

The effective name source has this precedence:

1. `MboTypesStringifyDoNotPrintFieldNames` suppresses automatic names.
2. `MboTypesStringifyFieldNames(const T&)` supplies or replaces the complete name sequence.
3. The compiler backend supplies names when the type and compiler support them.
4. `StringifyOptions::KeyMode::kNumericFallback` can use field indices when no name is available.

After that initial discovery, `MboTypesStringifyOptions(const T&, const StringifyFieldInfo&)` may
return policy for every individual field. `StringifyFieldInfo` provides the owning object's current
options, the field index, and the discovered name. Because the object is also passed to the ADL
extension point, policy may depend on runtime object state as well as compile-time type information.

| Control surface                              | Capability                                                                                      |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `MboTypesStringifyDoNotPrintFieldNames`      | Suppress compiler-provided names while retaining positional traversal.                          |
| `MboTypesStringifyFieldNames`                | Supply a complete field-name sequence or override compiler names.                               |
| `StringifyWithFieldNames`                    | Inject names through field options and either verify or overwrite compiler-discovered names.    |
| `MboTypesStringifyOptions`                   | Select full outer and inner formatting policy per object, field index, and field name.          |
| `StringifyOptions::FieldControl`             | Suppress a field, null pointer, empty optional, or disabled value; configure the disabled text. |
| `StringifyOptions::KeyControl`               | Hide keys, use normal keys, or fall back to numeric keys; control key prefix and suffix.        |
| `StringifyOptions::KeyOverrides`             | Rename one field with a string or a callback using the complete `StringifyFieldInfo`.           |
| `StringifyOptions::ValueControl`             | Select escaping, null spellings, container bounds, string bounds, and truncation suffixes.      |
| `StringifyOptions::ValueOverrides`           | Replace or redact string and non-string values.                                                 |
| `StringifyOptions::Format`                   | Control message, field, pointer, optional, aggregate, container, character, and string syntax.  |
| `StringifyFieldOptions`                      | Apply different policy to a containing field and to values nested inside that field.            |
| `StringifyOptions::Special`                  | Treat string-keyed pair containers as objects, order their keys, and rename pair elements.      |
| `MboTypesStringifyConvert(index, object, v)` | Convert a field based on its owning type, index, object, and value before rendering.            |
| `MboTypesStringifyValueAccess`               | Present a wrapper or holder as a different underlying value.                                    |
| `MboTypesStringifyDisable`                   | Stop automatic recursive stringification for a type, with configurable suppression.             |
| `MboTypesStringifySupport`                   | Opt a type into mbo traversal even without the `Extend` CRTP defaults.                          |
| `StringifyRootOptions`                       | Control root prefix, suffix, and indentation independently from field formatting.               |

Consequently, adding GCC field names improves the metadata available to existing policy. It does
not require replacing mbo's formatting model with a serialization framework, nor does it remove
the need for explicit names and field policies when the source identifier is not the desired output
key.

## Existing implementations

The following table distinguishes automatic extraction from the broader metadata systems supplied
by some libraries. “Named union member” means an ordinary aggregate field whose type is a union; it
does not imply discovering or safely reading the active alternative inside that union.

| Implementation       | GCC names | Constexpr names                  | Direct union                    | Named union member                       | Bit-fields                                                    | Reference members                        | Needs real object | Configurable overrides                                          | Principal mechanism                                     |
| -------------------- | --------- | -------------------------------- | ------------------------------- | ---------------------------------------- | ------------------------------------------------------------- | ---------------------------------------- | ----------------- | --------------------------------------------------------------- | ------------------------------------------------------- |
| **mbo**              | Yes       | Yes on GCC; eligible Clang types | No                              | GCC: opaque outer field; Clang: excluded | GCC address path rejects them; Clang support remains limited  | GCC: rejected; Clang runtime path tested | Clang only        | Extensive ADL names, field policy, conversion and suppression   | GCC signature parsing; Clang dump callback              |
| **Google Gloop**     | No        | No                               | No through its extension        | Possible from a valid object             | Builtin can expose names; `Unpack` may restrict traversal     | Depends on `Unpack`                      | Yes               | No field-name override found                                    | Runtime `__builtin_dump_struct`, cached after first use |
| **Boost.PFR**        | Yes       | Yes                              | Explicitly rejected             | Yes as an opaque outer field             | No automatic access: an address cannot be formed              | Generally unsupported for names          | No                | Global parser configuration; no per-type renaming               | Fake object, address NTTP, signature parsing            |
| **Glaze**            | Yes       | Yes                              | Not by ordinary pure reflection | Generally yes as an outer field          | Automatic address path is limited; metadata is a fallback     | Limited automatically; metadata fallback | No                | Rich `glz::meta`, explicit names, modification and rename hooks | Fake object and signatures, or explicit metadata        |
| **reflect-cpp**      | Yes       | Yes                              | Models tagged unions instead    | Likely as an opaque outer field          | No automatic address extraction                               | Generally unsupported automatically      | No                | Rename/flatten processors, named fields and custom reflectors   | Fake object, address NTTP, function-name parsing        |
| **field-reflection** | Yes       | Yes                              | No                              | Likely as an ordinary outer field        | Type inspection may work; ordinary field access is restricted | Explicitly not field-nameable            | No                | None                                                            | Fake object/address and signature parsing               |
| **qlibs/reflect**    | Yes       | Yes                              | Limited by aggregate visitation | Likely as an opaque outer field          | Limited by automatic aggregate visitation                     | Implementation-dependent                 | No                | Custom visitation can replace automatic visitation              | Signature parsing and aggregate visitation              |

These entries describe the pre-C++26 backends relevant to mbo's GCC 14 baseline. Some projects also
have experimental C++26 standard-reflection backends with different capabilities.

### Google Gloop

[Gloop's reflection implementation](https://github.com/abseil/gloop/tree/main/gloop/util/gtl/extend)
is close to mbo's current Clang implementation, but it does not provide the missing GCC technique.
It enables parsing only when `ABSL_HAVE_BUILTIN(__builtin_dump_struct)` is true and explicitly
disables it on macOS and Android.

Gloop calls the builtin on an actual object, parses only records at brace depth one, ignores base
class records, records whether parsing succeeded, and caches the result after the first call. Its
implementation is runtime-only. Useful ideas for mbo are the explicit success state and robust
top-level brace tracking, not GCC support.

Relevant sources:

- [`internal/reflection.h`](https://github.com/abseil/gloop/blob/main/gloop/util/gtl/extend/internal/reflection.h)
- [`internal/reflection.cc`](https://github.com/abseil/gloop/blob/main/gloop/util/gtl/extend/internal/reflection.cc)
- [`reflection_extension.h`](https://github.com/abseil/gloop/blob/main/gloop/util/gtl/extend/reflection_extension.h)

### Boost.PFR

[Boost.PFR's field-name backend](https://github.com/boostorg/pfr/blob/develop/include/boost/pfr/detail/core_name20_static.hpp)
is the most mature direct precedent. It obtains a reference to each field of an undefined external
fake object, takes its address, passes that address as a non-type template argument, and parses the
compiler signature. A sentinel field verifies the configured parser. Extracted substrings are copied
into constexpr character arrays so complete compiler signatures need not remain in the binary.

Boost.PFR explicitly rejects direct union reflection. This is necessary for operations that might
read an inactive member. It does not necessarily require rejecting an ordinary outer field merely
because that field's type is a union.

Relevant sources:

- [`core_name20_static.hpp`](https://github.com/boostorg/pfr/blob/develop/include/boost/pfr/detail/core_name20_static.hpp)
- [`fake_object.hpp`](https://github.com/boostorg/pfr/blob/develop/include/boost/pfr/detail/fake_object.hpp)
- [field-name and union documentation](https://www.boost.org/doc/libs/latest/doc/html/boost_pfr/tutorial.html)

### Glaze

[Glaze](https://github.com/stephenberry/glaze) independently implements automatic names with an
external object and compiler-signature parsing. Its broader reflection model also demonstrates why
source names and presentation names must remain distinct: `glz::meta` can explicitly enumerate
members, attach different names, add computed accessors, or modify automatically reflected metadata.

Glaze therefore resembles mbo at the policy level more than Boost.PFR does, although Glaze primarily
uses that metadata for serialization and mbo uses its field controls for general stringification and
related aggregate operations.

Relevant sources:

- [`get_name.hpp`](https://github.com/stephenberry/glaze/blob/main/include/glaze/reflection/get_name.hpp)
- [Glaze reflection and metadata documentation](https://github.com/stephenberry/glaze#explicit-metadata)

### reflect-cpp

[reflect-cpp](https://github.com/getml/reflect-cpp) also obtains addresses from a fake object and
extracts names from `std::source_location::function_name()` where available. Its structured-binding
field extractors are generated by arity, much like mbo's decomposition helpers. It offers explicit
named-field wrappers, renaming and flattening processors, and custom reflectors for types outside
the automatic subset.

Relevant sources:

- [`get_field_names.hpp`](https://github.com/getml/reflect-cpp/blob/main/include/rfl/internal/get_field_names.hpp)
- [`get_ith_field_from_fake_object.hpp`](https://github.com/getml/reflect-cpp/blob/main/include/rfl/internal/get_ith_field_from_fake_object.hpp)

### Other independent implementations

- [field-reflection](https://github.com/yosh-matsuda/field-reflection) documents its capability
  boundary explicitly: GCC 11 or newer, no bases for indexed field references, and practically no
  reference members for field names.
- [qlibs/reflect](https://github.com/qlibs/reflect) provides C++20 reflection primitives based on
  compiler function-name strings and permits replacing automatic visitation.
- [GCC documents](https://gcc.gnu.org/onlinedocs/gcc/Function-Names.html) that
  `__PRETTY_FUNCTION__` contains the C++ function signature and is usable in constant expressions.

The implementations are independent enough to establish the technique as reproducible, while their
different parsers and workarounds also show that it remains compiler-dependent and requires focused
compatibility tests.

## GCC backend

`mbo/types/internal/struct_names_gcc.h` is selected from `struct_names.h` when compiling with GCC
rather than Clang. The backend reuses `DecomposeCountImpl<T>` and `StructToTuple` instead of
introducing another aggregate-decomposition implementation.

For every field index it:

1. refer to an undefined external fake object of type `T`;
2. obtain field `I` through mbo's structured-binding tuple;
3. take the address with `std::addressof`;
4. pass that address as an `auto` non-type template argument;
5. read `__PRETTY_FUNCTION__` in a `consteval` function;
6. isolate the final member identifier;
7. copy it into compact static constexpr storage.

The undefined object serves as a compile-time expression only. If it is ever needed by the linker,
that indicates an accidental runtime use and should fail rather than silently construct an object.

### Parser design

Compiler-signature formats are not standardized. The parser should therefore:

- derive its relevant prefix and suffix using a private sentinel type and sentinel member;
- verify at compile time that the sentinel extracts exactly the expected identifier;
- avoid fixed offsets where a delimiter-based extraction is reliable;
- copy the final name into compact storage rather than retaining every full signature;
- distinguish “this field shape is unsupported” from “the compiler signature format changed”;
- fail loudly for a backend-format regression instead of returning plausible but incorrect names.

`std::source_location::function_name()` is standard API but its returned spelling is still
implementation-defined. It does not remove the need for compiler-specific parsing. Using
`__PRETTY_FUNCTION__` directly is simpler and matches mbo's explicit GCC backend boundary.

### Capability contract

| Type shape                                 | GCC result                                                 |
| ------------------------------------------ | ---------------------------------------------------------- |
| Ordinary decomposable aggregate            | Constexpr field names.                                     |
| Empty aggregate                            | Supported with an empty span.                              |
| Non-default-constructible aggregate        | Supported; no construction occurs.                         |
| Aggregate with non-literal member types    | Supported at compile time; member values are not read.     |
| Array member                               | Supported with an explicit decomposition-count override.   |
| Named field whose type is a union          | Attempt as an opaque outer field; do not inspect members.  |
| Direct union                               | Unsupported.                                               |
| Bit-field                                  | Unsupported automatically because no address can be taken. |
| Reference member                           | Unsupported unless a safe GCC expression is demonstrated.  |
| Anonymous union or anonymous struct        | Unsupported, consistent with `Extend` restrictions.        |
| Non-empty base classes                     | Follow mbo's existing decomposition restrictions.          |
| Packed or unusually aligned aggregate      | Unsupported until explicitly demonstrated and tested.      |
| Local or internal-linkage type             | Must be tested; fake-object linkage is compiler-sensitive. |
| More fields than mbo's decomposition limit | Unsupported by the shared decomposition layer.             |

GCC cannot infer structured-binding arity unambiguously for every aggregate. In particular,
aggregate initialization flattens built-in arrays, while treating each array as one binding is
required for field access. An ambiguous type can state its exact binding count with a hidden friend:

```cpp
struct WithArray {
  int values[2];
  int tail;

  friend consteval std::size_t MboTypesDecomposeCount(const WithArray*) { return 2; }
};
```

This override is shared by tuple decomposition and GCC field-name discovery. Its value must exactly
match the number of identifiers accepted by a structured binding of the type; it is not the number
of flattened aggregate initializers.

“Expected” and “must be tested” entries are deliberately not promises. They identify the first
spike's validation work.

### Interaction with existing policy

The GCC backend should change only the automatic result returned by `GetFieldNames<T>()` and the two
support concepts. Existing precedence and overrides remain intact:

- explicitly suppressed names remain suppressed;
- `MboTypesStringifyFieldNames` continues to replace automatic names;
- `StringifyWithFieldNames(..., kVerify)` gains useful validation on GCC;
- `StringifyWithFieldNames(..., kOverwrite)` continues to choose presentation names;
- `MboTypesStringifyOptions` receives the GCC-discovered name in `StringifyFieldInfo`;
- dynamic key overrides, field filtering, conversion, redaction, and value-access hooks continue to
  operate after discovery;
- positional behavior remains available for unsupported types.

This separation is important. A C++ source identifier is useful default metadata, not necessarily a
stable wire name, JSON key, privacy policy, or user-facing label.

## Validation plan

The implementation should first be developed as a small backend spike and then tested through the
public behavior already exercised by `struct_names_test.cc` and `stringify_test.cc`.

| Area                      | Required cases                                                                                            |
| ------------------------- | --------------------------------------------------------------------------------------------------------- |
| Basic extraction          | Empty, one-field, multi-field, nested aggregate, arrays, cv-qualified input, and templated types.         |
| Construction independence | Deleted default constructor, non-trivial destructor, non-literal member, move-only member.                |
| Identity and linkage      | Namespace-scope, anonymous-namespace, nested, templated, and local types.                                 |
| Awkward fields            | Reference members, bit-fields, packed fields, union-valued fields, anonymous unions, and direct unions.   |
| Inheritance               | Empty CRTP bases used by `Extend`, multiple empty bases, and rejected non-empty bases.                    |
| Parser integrity          | Sentinel success, punctuation in template spellings, long qualified types, and no full-signature leakage. |
| mbo policy                | Manual replacement, suppression, verify/overwrite modes, dynamic key overrides, and numeric fallback.     |
| Toolchains                | GCC 14 in every supported optimization/sanitizer mode; Clang behavior unchanged.                          |
| Diagnostics               | Unsupported shapes fail concepts cleanly; parser-format changes produce a targeted assertion.             |

The GCC tests must be compile-time assertions wherever the contract promises constexpr names. A
runtime stringification test alone would not prove the intended capability.

## Future standard reflection

C++26 static reflection provides direct metadata operations such as enumerating non-static data
members and obtaining their identifiers. Once mbo's supported compiler baseline provides a stable
implementation, a standard backend can replace both dump-struct callbacks and compiler-signature
parsing while preserving mbo's public internal facade and higher-level field policy.

That migration should not collapse discovery and policy. Even with standard reflection, mbo will
still need explicit presentation names, suppression, conversion, redaction, value access, and
per-field formatting. Standard reflection improves the provenance and supported shape of the
metadata; it does not replace those controls.
