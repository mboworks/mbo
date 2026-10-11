# `mbo/testing`

`mbo::testing` provides GoogleTest and GoogleMock helpers for testing text, containers, and Abseil
statuses. It includes text comparisons with unified diffs, membership and transformation matchers,
status assertion macros, and Bazel test runfile lookup.

- Namespace: `mbo::testing`.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                           | Bazel target                    | Contents                                                |
| -------------------------------- | ------------------------------- | ------------------------------------------------------- |
| [matchers.h](matchers.h)         | `//mbo/testing:matchers_cc`     | Text, membership, capacity, and transformation matchers |
| [status.h](status.h)             | `//mbo/testing:status_cc`       | Status matchers and assertion macros                    |
| [runfiles_dir.h](runfiles_dir.h) | `//mbo/testing:runfiles_dir_cc` | Runfile resolution; Bazel `testonly` target             |

## General matchers

| Matcher                                | Accepted subject                       | Match condition                                                   |
| -------------------------------------- | -------------------------------------- | ----------------------------------------------------------------- |
| `IsNullopt()`                          | Optional-like value                    | Equal to `std::nullopt`                                           |
| `CapacityIs(value_or_matcher)`         | Container with `capacity()`            | Capacity matches                                                  |
| `IsElementOf(container)`               | Equality-comparable value              | Equal to at least one element                                     |
| `IsKeyOf(map)`                         | Equality-comparable key                | Equal to at least one map key                                     |
| `IsValueOf(map)`                       | Equality-comparable value              | Equal to at least one mapped value                                |
| `WhenTransformedBy(function, matcher)` | Container                              | Transformed elements match as a `std::vector`, in iteration order |
| `EqualsText(expected)`                 | Text convertible to `std::string_view` | Exact text equality; unified diff on failure                      |
| `WithDropIndent(EqualsText(expected))` | Text convertible to `std::string_view` | Expected text processed by `DropIndent`; actual text unchanged    |

- `IsElementOf`: stores the container by value; initializer-list overload copies into a vector.
- `IsKeyOf` and `IsValueOf`: copy the selected keys or mapped values into vectors.
- Heterogeneous membership: direct equality comparison, with component-wise fallback for `std::pair`.
- `WhenTransformedBy`: stores references to its transformer and matcher; referenced objects must
  remain alive through matching. Temporary arguments are valid for an immediate `EXPECT_THAT`.
- `EqualsText`: owns the expected text.
- Indentation rules: [mbo/strings](../strings/README.md#indentation).

```cpp
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

using mbo::testing::EqualsText;
using mbo::testing::IsElementOf;
using mbo::testing::WhenTransformedBy;
using mbo::testing::WithDropIndent;
using ::testing::ElementsAre;

struct TextTest : ::testing::Test {};

TEST_F(TextTest, TextAndMembership) {
  const std::string actual = "alpha\nbeta\n";
  EXPECT_THAT(actual, WithDropIndent(EqualsText(R"(
    alpha
    beta
  )")));
  const std::vector<std::string> allowed{"alpha", "beta"};
  EXPECT_THAT("alpha", IsElementOf(allowed));
  EXPECT_THAT(allowed, WhenTransformedBy([](const std::string& value) { return value.size(); }, ElementsAre(5, 4)));
}
```

## Status matchers

| Matcher                                      | Match condition                                                                           |
| -------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `IsOk()`                                     | `Status` or `StatusOr<T>` is OK                                                           |
| `IsOkAndHolds(matcher)`                      | `StatusOr<T>` is OK and its value matches                                                 |
| `StatusIs(code_or_matcher)`                  | Status code matches                                                                       |
| `StatusIs(code_or_matcher, message_matcher)` | Code and message match; message ignored for matching OK status                            |
| `StatusHasPayload()`                         | Non-OK status has at least one payload                                                    |
| `StatusHasPayload(type_url)`                 | Non-OK status has that payload URL                                                        |
| `StatusHasPayload(type_url, matcher)`        | Payload content, converted to `std::string`, matches                                      |
| `StatusPayloads(matcher)`                    | Complete URL-to-string payload map matches as `absl::btree_map<std::string, std::string>` |

- Status-bearing subjects: extracted through [GetStatus](../status/README.md#status-extraction).
- `StatusPayloads`: OK statuses match an empty payload map.
- Payload matching: checks content and URLs without changing the status.

## Assertion macros

| Macro                                          | Behavior                                                                          |
| ---------------------------------------------- | --------------------------------------------------------------------------------- |
| `MBO_EXPECT_OK(expression)`                    | Nonfatal OK assertion                                                             |
| `MBO_ASSERT_OK(expression)`                    | Fatal OK assertion                                                                |
| `MBO_ASSERT_OK_AND_ASSIGN(target, expression)` | Assert OK, then move the value into a variable or declaration                     |
| `MBO_ASSERT_OK_AND_MOVE_TO(expression, ...)`   | Assert OK, then move into a declaration supporting commas and structured bindings |

- Short aliases: `EXPECT_OK`, `ASSERT_OK`, `ASSERT_OK_AND_ASSIGN`, when not already defined.
- Assignment macros: one expression evaluation; new declarations remain in the enclosing scope.
- Assignment macros: multiple statements; use braces around conditional or loop bodies.
- Temporary names: based on `__LINE__`; one assignment macro per source line.
- Fatal assertion macros: GoogleTest fatal-assertion restrictions apply to the enclosing function.

```cpp
#include <utility>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

using mbo::testing::IsOkAndHolds;
using ::testing::Eq;
using ::testing::Lt;

struct StatusTest : ::testing::Test {};

TEST_F(StatusTest, BindsReturnedValues) {
  const absl::StatusOr<int> result = 42;
  EXPECT_THAT(result, IsOkAndHolds(42));
  MBO_ASSERT_OK_AND_ASSIGN(const int value, result);
  EXPECT_THAT(value, Eq(42));
  const absl::StatusOr<std::pair<int, int>> bounds = std::pair{2, 5};
  MBO_ASSERT_OK_AND_MOVE_TO(bounds, const auto [first, last]);
  EXPECT_THAT(first, Lt(last));
}
```

## Runfiles

| API                                  | Result                                                           |
| ------------------------------------ | ---------------------------------------------------------------- |
| `RunfilesDir(source)`                | `absl::StatusOr<std::string>` containing a resolved runfile path |
| `RunfilesDir(workspace, source_rel)` | Resolve a workspace-relative runfile path                        |
| `RunfilesDirOrDie(...)`              | Same overloads; fatal check on resolution error                  |

- Single-argument forms: plain path, `//package:file`, or `@repository//package:file`.
- Label conversion: replace the first `:` after `//` with `/`.
- Test environment: `TEST_WORKSPACE`, `TEST_SRCDIR`, and Bazel runfiles metadata.
- External repository names: resolved through `_repo_mapping` when required.
- Inputs: declare files in the test target's `data` attribute.
- Resolution is not a file-content read or a guarantee that the returned file exists.

## Tests

```sh
bazel test --config=clang //mbo/testing:all
```

- [matchers_test.cc](matchers_test.cc), [status_test.cc](status_test.cc),
  [runfiles_dir_test.cc](runfiles_dir_test.cc).
