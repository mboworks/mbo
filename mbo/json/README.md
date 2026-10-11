# `mbo/json`

`mbo::json` is a write-only JSON generator. It provides owning scalar, array, and object values for
building documents in C++ and serializing object or null roots to strings or streams. It does not
read or parse JSON input.

- Namespace: `mbo::json`.
- Header: [json.h](json.h).
- Bazel target: `//mbo/json:json_cc`.

## Value kinds

| `Json::Kind` | Storage                                                   | Predicates                                                                   |
| ------------ | --------------------------------------------------------- | ---------------------------------------------------------------------------- |
| `kNull`      | `std::nullopt_t`                                          | `IsNull()`                                                                   |
| `kArray`     | Owned `std::vector<Json>`                                 | `IsArray()`                                                                  |
| `kBool`      | `bool`                                                    | `IsBool()`, `IsTrue()`, `IsFalse()`                                          |
| `kNumber`    | `int64_t`, `uint64_t`, or `double`                        | `IsNumber()`, `IsInteger()`, `IsSignedInt()`, `IsUnsignedInt()`, `IsFloat()` |
| `kObject`    | `absl::flat_hash_map<std::string, std::unique_ptr<Json>>` | `IsObject()`                                                                 |
| `kString`    | `std::string`                                             | `IsString()`                                                                 |

- Default construction: null.
- Scalar construction: explicit; null, boolean, supported numeric, and string inputs.
- `ConvertibleToJson<T>`: accepted scalar conversion categories; not arbitrary aggregate conversion.
- `GetKind()`: stored value kind; numeric representations share `kNumber`.
- Copy construction and copy assignment: deep copies of arrays and objects.
- Move construction and move assignment: supported and `noexcept`.
- Explicit boolean conversion: `!IsNull()`, including true for a stored `false`, zero, or empty string.
- Equality and ordering: `Json` values and supported scalar operands.

## Construction and mutation

| Operation                          | Required kind  | Effect                                                                |
| ---------------------------------- | -------------- | --------------------------------------------------------------------- |
| `Reset()`                          | Any            | Replace with null                                                     |
| `MakeArray()`                      | Null or array  | Create an empty array from null; preserve an existing array           |
| `MakeObject()`                     | Null or object | Create an empty object from null; preserve an existing object         |
| `MakeString(value)`                | Null or string | Create a string from null; preserve an existing string                |
| Assignment from a supported scalar | Any            | Replace the stored value                                              |
| `clear()`                          | Any            | Empty arrays/objects without changing kind; reset other kinds to null |
| `size()`                           | Any            | Array element or object property count; zero for other kinds          |
| `empty()`                          | Any            | Container emptiness; true for every scalar and null                   |

### Objects

| Operation                      | Behavior                                                                           |
| ------------------------------ | ---------------------------------------------------------------------------------- |
| Mutable `operator[](property)` | Convert null to object; insert a null value if the property is absent              |
| Const `operator[](property)`   | Require an object and an existing property                                         |
| `at(property)`                 | Require an object and an existing property; no insertion                           |
| `contains(property)`           | Property presence; false for non-objects                                           |
| `emplace(property, value)`     | Convert null to object; insert if absent; return the existing value on a duplicate |
| `erase(property)`              | Return the number erased; zero for non-objects                                     |

### Arrays

| Operation                                  | Behavior                                                                   |
| ------------------------------------------ | -------------------------------------------------------------------------- |
| `push_back(value)`                         | Convert null to array and append                                           |
| `emplace_back(value)`                      | Convert null to array, append, and return the new element                  |
| Mutable `operator[](index)` or `at(index)` | Require null or array, then a valid index; no automatic growth             |
| Const `operator[](index)` or `at(index)`   | Require array and a valid index                                            |
| `resize(count)`                            | Require array; add null values when growing                                |
| `resize(count, value)`                     | Require array; fill new elements with the supplied value                   |
| `pop_back()`                               | Remove the final element; requires a nonempty array                        |
| `pop_back_value()`                         | Check array kind and nonempty size, then remove and return the final value |
| `erase(iterator)` or `erase(first, last)`  | Remove array elements and return the following iterator                    |

- `pop_back_value()`: retains array kind when removing the final element.
- Checked requirements: [MBO_CONFIG_REQUIRE](../config/README.md#requirement-failures).
- `MakeArray`, `MakeObject`, and `MakeString`: no conversion between existing non-null kinds;
  call `Reset()` first or assign a replacement value.

```cpp
#include <string>

#include "mbo/json/json.h"

std::string MakeDocument() {
  mbo::json::Json document;
  document["name"] = "sample";
  document["enabled"] = true;
  document["values"].push_back(3);
  document["values"].push_back(5);
  document["last"] = document["values"].pop_back_value();
  return document.Serialize(mbo::json::Json::SerializeMode::kPretty);
}
```

## Iteration and views

| API                                          | Required kind   | Elements                      |
| -------------------------------------------- | --------------- | ----------------------------- |
| `begin/end`, `cbegin/cend`, reverse variants | Array           | Array values                  |
| `array_values()`                             | Array           | View of array values          |
| `values()`                                   | Array or object | Forward view of `Json` values |
| `property_names()`                           | Object          | Property names                |
| `property_values()`                          | Object          | Property values               |
| `property_pairs()`                           | Object          | Name/value reference pairs    |

- Views: borrow the owning `Json` and its elements.
- Array iteration order: element order.
- Object iteration order: hash-table order; not insertion order.
- Mutations: subject to the underlying vector/hash-table iterator invalidation rules.
- `erase` also accepts the value iterators returned by `values()` for arrays and objects.
- Mutable `values()`: writes `Array Size: N` or `Object Size: N` to `std::cout` when creating the view.

## Serialization

| API or mode                           | Behavior                          |
| ------------------------------------- | --------------------------------- |
| `Serialize(mode, root_options)`       | Return `std::string`              |
| `Stream(ostream, mode, root_options)` | Write to the stream and return it |
| `SerializeMode::kCompact`             | Default compact JSON formatting   |
| `SerializeMode::kLine`                | JSON line formatting              |
| `SerializeMode::kPretty`              | Indented JSON formatting          |

- Root kinds: object or null only; other root kinds trigger a requirement failure.
- Null root output: `{}` followed by a newline with default options.
- Nested values: arrays, objects, strings, booleans, numbers, and null.
- Formatting and root options: [mbo/types Stringify](../types/README.md#stringification).
- Compact object property order: unspecified.

## Tests

```sh
bazel test --config=clang //mbo/json:all
bazel test --config=clang --//mbo/config:require_throws=true \
  //mbo/json:json_require_exceptions_test
```

- [json_test.cc](json_test.cc): values, deep copies, mutation, iteration, and serialization.
- [json_exception_test.cc](json_exception_test.cc): exception-enabled compilation.
- [json_require_exceptions_test.cc](json_require_exceptions_test.cc): checked precondition failures;
  excluded from the normal configuration.
