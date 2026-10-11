# `mbo/status`

`mbo::status` provides helpers for error handling with `absl::Status` and `absl::StatusOr`.
It includes status extraction, a builder for messages and payloads, and macros that propagate
errors while assigning or moving successful results.

- Namespace: `mbo::status`.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                               | Bazel target                     | API                                                                    |
| ------------------------------------ | -------------------------------- | ---------------------------------------------------------------------- |
| [status.h](status.h)                 | `//mbo/status:status_cc`         | `GetStatus`                                                            |
| [status_builder.h](status_builder.h) | `//mbo/status:status_builder_cc` | `StatusBuilder`                                                        |
| [status_macros.h](status_macros.h)   | `//mbo/status:status_macros_cc`  | `MBO_RETURN_IF_ERROR`, `MBO_ASSIGN_OR_RETURN`, `MBO_MOVE_TO_OR_RETURN` |

## Status extraction

| Input to `GetStatus`                                | Result                         |
| --------------------------------------------------- | ------------------------------ |
| `absl::Status`                                      | Status value                   |
| `const absl::StatusOr<T>&`                          | Contained status               |
| `absl::StatusOr<T>&&`                               | Status from the moved argument |
| Type constructible or convertible to `absl::Status` | Constructed `absl::Status`     |

- Return type: `absl::Status`.
- `StatusOr<T>` extraction: no access to the contained `T`.

## `StatusBuilder`

| Operation                                   | Behavior                                                                 |
| ------------------------------------------- | ------------------------------------------------------------------------ |
| Default construction                        | OK status                                                                |
| Construction from `Status` or `StatusOr<T>` | Preserve the input status code and payloads                              |
| `ok()`                                      | Whether the builder represents OK                                        |
| `operator<<`                                | Collect additional message text; no effect for OK                        |
| `SetAppend()` or `<< StatusBuilder::Append` | Insert the original message once at the current stream position          |
| `SetPrepend()`                              | Fluent marker; does not insert the original message or reset append mode |
| `SetPayload(type_url, payload)`             | Set or replace a payload on a non-OK status                              |
| Conversion to `absl::Status`                | Construct the resulting status with its message and payloads             |

- Copy construction and copy assignment: deleted.
- Move construction and move assignment: supported.
- Default message order: original message, then collected text.
- Prefix construction: stream the prefix, insert `Append`, then stream any suffix.
- Lvalue conversion: explicit; rvalue conversion: implicit.
- OK input: remains OK; streamed text and payload additions have no effect.

```cpp
#include <utility>

#include "absl/status/status.h"
#include "mbo/status/status_builder.h"

absl::Status AddContext(absl::Status status) {
  using mbo::status::StatusBuilder;
  return StatusBuilder(std::move(status)) << "Reading configuration: " << StatusBuilder::Append << " [startup]";
}
```

## Propagation macros

| Macro                                      | Success                                                               | Error                                                                  |
| ------------------------------------------ | --------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `MBO_RETURN_IF_ERROR(expression)`          | Continue                                                              | Return a `StatusBuilder` for the error; supports trailing `<<` context |
| `MBO_ASSIGN_OR_RETURN(target, expression)` | Move the `StatusOr<T>` value into an existing variable or declaration | Return the original status                                             |
| `MBO_MOVE_TO_OR_RETURN(expression, ...)`   | Move the value into a declaration, including a structured binding     | Return the original status                                             |

- Expression evaluation: once per macro invocation.
- Assignment macros: multiple statements; enclosing braces required in conditional or loop bodies.
- Newly declared variables: remain in the enclosing scope.
- Assignment macro temporary names: based on `__LINE__`; one invocation per source line.
- Enclosing function: return type must accept the propagated error.
- `MBO_MOVE_TO_OR_RETURN`: expression first, declaration second.

```cpp
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"

absl::Status Validate();
absl::StatusOr<std::string> ReadName();
absl::StatusOr<std::pair<int, int>> ReadBounds();

absl::StatusOr<std::string> LoadName() {
  MBO_RETURN_IF_ERROR(Validate()) << " while loading a name";
  MBO_ASSIGN_OR_RETURN(std::string name, ReadName());
  MBO_MOVE_TO_OR_RETURN(ReadBounds(), const auto [first, last]);
  if (first > last) {
    return absl::InvalidArgumentError("Invalid bounds");
  }
  return name;
}
```

## Tests

```sh
bazel test --config=clang //mbo/status:all
```

- [status_test.cc](status_test.cc): extraction and conversion.
- [status_builder_test.cc](status_builder_test.cc): message ordering, moves, payloads, and OK handling.
- [status_macros_test.cc](status_macros_test.cc): propagation and assignment.
- Status matchers and assertion macros: [mbo/testing](../testing/README.md).
