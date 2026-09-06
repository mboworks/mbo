# `mbo/types`

`mbo/types` combines small value and metaprogramming utilities with an aggregate framework that can
derive construction, tuple access, comparison, hashing, and configurable text output from a type's
fields. The framework targets C++23, GCC 14+, and Clang 22+.

This guide describes the public model and common usage. The separate
[reflection implementation note](REFLECTION.md) explains how automatic field-name discovery works,
its compiler-specific limitations, and how mbo compares with other reflection libraries.

## Quick start with `Extend`

Derive an aggregate from `mbo::types::Extend<T>` to install the default CRTP extenders:

```c++
#include <iostream>
#include <string>

#include "mbo/types/extend.h"

struct Name : mbo::types::Extend<Name> {
  std::string first;
  std::string last;
};

Name name = Name::ConstructFromArgs("Ada", "Lovelace");
std::cout << name << '\n';
```

The type receives comparison, Abseil and standard hashing, Abseil stringification, stream output,
`ToString()`, `ToJsonString()`, and construction helpers. When field-name reflection supports the
aggregate, output can use `first` and `last`; otherwise it remains positional or uses configured
names or numeric fallbacks.

`Extend` remains an aggregate base. It does not add data members or virtual dispatch, but the empty
CRTP base participates in aggregate initialization. The named construction helpers avoid callers
having to spell that base explicitly:

```c++
auto name = Name::ConstructFromArgs("Grace", "Hopper");
auto other = Name::ConstructFromTuple(std::tuple{"Barbara", "Liskov"});
auto converted = Name::ConstructFromConversions("Margaret", "Hamilton");
```

## Extender sets

| Base                              | Installed behavior                                                                     |
| --------------------------------- | -------------------------------------------------------------------------------------- |
| `Extend<T, Extra...>`             | Default behavior plus any custom extenders.                                            |
| `ExtendNoPrint<T, Extra...>`      | Comparison, hashing, and Abseil stringification, without `ToString()` or `operator<<`. |
| `ExtendNoDefault<T, Extender...>` | Only the explicitly selected extenders.                                                |
| `extender::Default`               | `AbslStringify`, `AbslHashable`, `Comparable`, `Printable`, and `Streamable`.          |
| `extender::NoPrint`               | `AbslStringify`, `AbslHashable`, and `Comparable`.                                     |
| `extender::Comparable`            | Tuple-based equality and ordering.                                                     |
| `extender::AbslHashable`          | `AbslHashValue`; extended types also receive `std::hash`.                              |
| `extender::AbslStringify`         | Abseil formatting backed by `Stringify`.                                               |
| `extender::Printable`             | `ToString()` and `ToJsonString()`; requires `AbslStringify`.                           |
| `extender::Streamable`            | `operator<<`; requires `AbslStringify`.                                                |

Use `ExtendNoDefault` when a type must expose only a narrow operation set. Use `ExtendNoPrint` when
the comparison and hashing semantics are useful but text output has a hand-written implementation.
Additional extenders can be defined with `MakeExtender`; dependencies between extenders are checked
while the CRTP chain is assembled.

## Structural reflection and tuples

The structural layer determines an aggregate's field count and maps its structured binding to a
tuple of references. It is independent of whether source field names are available.

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
| `MboTypesStringifyFieldNames`           | Supply the complete stable name sequence or replace compiler names.              |
| `MboTypesStringifyDoNotPrintFieldNames` | Suppress compiler-provided names while retaining traversal.                      |
| `MboTypesStringifyOptions`              | Choose formatting and policy for each object and field.                          |
| `StringifyWithFieldNames`               | Adapt a name sequence into field options, optionally verifying discovered names. |
| `StringifyOptions::FieldControl`        | Suppress fields, null pointers, empty optionals, or disabled values.             |
| `StringifyOptions::KeyOverrides`        | Rename a field statically or through a callback.                                 |
| `StringifyOptions::ValueOverrides`      | Replace or redact string and non-string values.                                  |
| `MboTypesStringifyConvert`              | Convert a field using its owner, index, and value before rendering.              |
| `MboTypesStringifyValueAccess`          | Present a wrapper as a different underlying value.                               |
| `MboTypesStringifyDisable`              | Prevent automatic recursive rendering for a type.                                |
| `MboTypesStringifySupport`              | Opt a non-extended aggregate into `Stringify` traversal.                         |

Automatic source identifiers are convenient diagnostics, but explicit names are preferable for a
stable external schema. Formatting JSON-like text does not make `Stringify` a validating serializer;
use a serialization library when schema evolution, parsing, or protocol guarantees are required.

## Other value utilities

The package also provides utilities that do not depend on `Extend`:

| Utility                        | Purpose                                                                              |
| ------------------------------ | ------------------------------------------------------------------------------------ |
| `StringOrView`                 | Read-only text that either owns a string or borrows a string view.                   |
| `OptionalRef`                  | Optional non-owning reference.                                                       |
| `OptionalDataOrRef`            | Optional value that can own data or borrow a reference.                              |
| `Required`                     | Always-engaged value wrapper with configurable requirement failure behavior.         |
| `NoDestruct`                   | Static-lifetime storage that deliberately does not invoke the contained destructor.  |
| `OpaquePtr` / `OpaqueValue`    | Ownership wrappers suitable for forward-declared implementation types.               |
| `ContainerProxy`               | Container access forwarding for wrapped values and pointers.                         |
| `RefWrap`                      | Reference wrapper with `*` and `->`.                                                 |
| `TypedView`                    | View wrapper that supplies container-style type aliases.                             |
| `CompareArithmetic` and family | Strong/weak comparison helpers for mixed arithmetic and scalar types.                |
| `tstring`                      | Compile-time string-literal type used by the extender machinery and other templates. |

The repository's root [README](../../README.md) lists the complete header and Bazel target inventory.
