# `mbo/config`

`mbo::config` provides C++ feature checks, generated build settings, and requirement macros shared
by MBO libraries. It connects Bazel configuration to C++ constants and controls failure behavior
for requirement checks.

- Namespace: `mbo::config`.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                 | Bazel target              | Visibility             | Contents                                                      |
| ---------------------- | ------------------------- | ---------------------- | ------------------------------------------------------------- |
| [config.h](config.h)   | `//mbo/config:config_cc`  | `//mbo` subpackages    | Feature checks, generated constants, `MBO_CONFIG_TRIVIAL_ABI` |
| [require.h](require.h) | `//mbo/config:require_cc` | Repository subpackages | `MBO_CONFIG_REQUIRE`, `MBO_CONFIG_REQUIRE_DEBUG`              |

## Build settings

| Bazel flag                                             | Default | C++ constant                | Effect                                                                                 |
| ------------------------------------------------------ | ------- | --------------------------- | -------------------------------------------------------------------------------------- |
| `--//mbo/config:limited_ordered_max_unroll_capacity=N` | `16`    | `kUnrollMaxCapacityDefault` | Default maximum unrolled capacity for `LimitedOrdered`, `LimitedMap`, and `LimitedSet` |
| `--//mbo/config:require_throws=true`                   | `false` | `kRequireThrows`            | Selects throwing requirement failures when C++ exceptions are enabled                  |

- Dependency-qualified flags: `--@mboworks_mbo//mbo/config:require_throws=true` and
  `--@mboworks_mbo//mbo/config:limited_ordered_max_unroll_capacity=N`.
- `config_gen.h`: generated from [internal/config.h.in](internal/config.h.in) by
  [internal/config.bzl](internal/config.bzl); included through `config.h`.
- Missing generated header: template defaults, with a compiler warning unless `IS_CLANGD` is defined.
- Exceptions disabled: `kRequireThrows == false`, regardless of the Bazel flag.
- Repository compilation default: `-fno-exceptions`.
- Related container options: [container guide](../container/README.md).

## Requirement failures

| Macro                                          | Condition true | Condition false                                                                    |
| ---------------------------------------------- | -------------- | ---------------------------------------------------------------------------------- |
| `MBO_CONFIG_REQUIRE(condition, message)`       | Continue       | `std::runtime_error` when `kRequireThrows` is true; otherwise Abseil fatal logging |
| `MBO_CONFIG_REQUIRE_DEBUG(condition, message)` | Continue       | Same requirement check without `NDEBUG`; disabled with `NDEBUG`                    |

- `message`: string literal compatible with the throwing macro expansion.
- Exception message: source file, line, condition text, and supplied message.
- Fatal-logging form: supports additional `<<` output.
- Exception configuration: requires both compiler exception support and `require_throws=true`.
- A requirement failure inside a `noexcept` function still terminates if it throws.
- Call-site form: standalone statement inside braces; the macros contain conditional statements.

```cpp
#include "mbo/config/require.h"

int Divide(int numerator, int denominator) {
  MBO_CONFIG_REQUIRE(denominator != 0, "Denominator must be nonzero");
  return numerator / denominator;
}
```

## Required language and library features

| Feature-test macro            | Minimum   |
| ----------------------------- | --------- |
| `__cplusplus`                 | `202302L` |
| `__cpp_constexpr`             | `202211L` |
| `__cpp_if_consteval`          | `202106L` |
| `__cpp_lib_expected`          | `202211L` |
| `__cpp_lib_containers_ranges` | `202202L` |
| `__cpp_lib_string_contains`   | `202011L` |
| `__cpp_lib_byteswap`          | `202110L` |

- Unsupported feature level: compilation error from `config.h`.
- `MBO_CONFIG_TRIVIAL_ABI`: `[[clang::trivial_abi]]` when supported by Clang; empty otherwise.

## Tests

```sh
bazel test --config=clang //mbo/config:all
bazel test --config=clang --//mbo/config:require_throws=true \
  //mbo/json:json_require_exceptions_test
```

- [config_test.cc](config_test.cc): configuration constants.
- [require_test.cc](require_test.cc): requirement macros.
- [JSON exception tests](../json/json_require_exceptions_test.cc): exception-enabled consumer target.
