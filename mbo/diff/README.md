# mbo/diff: Unified-Diffing Utilities

Part of the **MBO** library ecosystem, `mbo/diff` provides lightweight utilities for generating unified diffs, a standalone command-line diffing tool, and Bazel macros designed for integration testing against golden files.

## 1. C++ API Reference: `mbo::diff::Diff`

The C++ library resides under the namespace `mbo::diff`. Like the rest of mbo, it targets C++23
and integrates natively with Google's Abseil library.

### `DiffOptions` Struct

The behavior of the diff engine and formatting output is fully controlled via the `mbo::diff::DiffOptions` struct:

| Field Name               | Type           | Default               | Description                                                                                          |
| :----------------------- | :------------- | :-------------------- | :--------------------------------------------------------------------------------------------------- |
| `unified_lines`          | `size_t`       | `3`                   | The number of context lines to display above and below each diff hunk.                               |
| `ignore_case`            | `bool`         | `false`               | If `true`, performs a case-insensitive comparison of lines.                                          |
| `ignore_spaces`          | `IgnoreSpaces` | `IgnoreSpaces::kNone` | Controls how whitespace is treated. See [Whitespace Configuration](#whitespace-configuration) below. |
| `ignore_blank_lines`     | `bool`         | `false`               | If `true`, runs of empty or whitespace-only lines that are added or removed are ignored.             |
| `normalize_line_endings` | `bool`         | `true`                | Standardizes `\r\n` (Windows) and `\n` (Unix) line endings to `\n` before computing the diff.        |

#### Whitespace Configuration

The `IgnoreSpaces`:

- **`IgnoreSpaces::kNone`**: Strict match. Every whitespace character is treated as significant.
- **`IgnoreSpaces::kTrailing`**: Ignores trailing whitespace at the end of each line.
- **`IgnoreSpaces::kChange`**: Ignores changes in the _amount_ of whitespace (e.g., multiple spaces or tabs are treated as a single space), but requires at least some whitespace if it acts as a delimiter.
- **`IgnoreSpaces::kAll`**: Completely ignores all whitespace characters during the comparison.

### Basic C++ Usage

```cpp
#include "mbo/diff/diff.h"
#include "absl/status/statusor.h"
#include <iostream>
#include <string>

int main() {
  std::string_view original = "Line 1\nLine 2\n\nLine 3\n";
  std::string_view modified = "line 1\nLine 2 changed\nLine 3\n";

  // Configure high-precision diff rules
  mbo::diff::DiffOptions options;
  options.unified_lines = 2;
  options.ignore_case = true;
  options.ignore_blank_lines = true;
  options.ignore_spaces = mbo::diff::IgnoreSpaces::kTrailing;

  absl::StatusOr<std::string> diff_output = mbo::diff::Diff::FormatUnified(
      "src/original.txt", original,
      "src/modified.txt", modified,
      options
  );

  if (diff_output.ok()) {
    if (diff_output->empty()) {
       std::cout << "Files are identical under the current configuration." << std::endl;
    } else {
       std::cout << *diff_output << std::endl;
    }
  } else {
    std::cerr << "Diff failed to execute: " << diff_output.status().message() << std::endl;
  }

  return 0;
}
```

## 2. Command-Line Tool Reference: `unified_diff`

The `unified_diff` binary exposes the underlying C++ diffing configurations via standard command-line flags.

### CLI Parameters & Flags

| Flag | Long Option               | Type   | Default | Description                                       |
| ---- | ------------------------- | ------ | ------- | ------------------------------------------------- |
| `-u` | `--unified`               | `int`  | `3`     | Number of context lines to output around changes. |
| `-i` | `--ignore-case`           | `bool` | `false` | Ignore case differences in file contents.         |
| `-w` | `--ignore-all-space`      | `bool` | `false` | Ignore all white space when comparing lines.      |
| `-b` | `--ignore-space-change`   | `bool` | `false` | Ignore changes in amount of white space.          |
| `-B` | `--ignore-blank-lines`    | `bool` | `false` | Ignore changes whose lines are all blank.         |
| `-Z` | `--ignore-trailing-space` | `bool` | `false` | Ignore white space at line end.                   |
|      | `--strip-trailing-cr`     | `bool` | `true`  | Strip carriage return (`\r`) at the end of lines. |

### CLI Compilation & Usage

Build the tool with Bazel:

```bash
bazel build //mbo/diff:unified_diff
```

Perform a custom, whitespace-insensitive diff with 5 context lines:

```bash
./bazel-bin/mbo/diff/unified_diff \
    --unified=5 \
    --ignore-space-change \
    --ignore-blank-lines \
    path/to/original.txt path/to/modified.txt
```

## 3. Bazel Integration: `diff_test` Macro

The `diff_test` Bazel macro (loaded from `@mbo//mbo/diff:diff.bzl`) acts as a wrapper around the `unified_diff` binary, running comparisons as part of your standard Bazel test suite.

### Macro Arguments Reference

When declaring a `diff_test` target in your `BUILD` file, the following arguments are available:

| Argument              | Type              | Required              | Description                                                             |
| --------------------- | ----------------- | --------------------- | ----------------------------------------------------------------------- |
| `name`                | `string`          | **Yes**               | A unique name for this test target.                                     |
| `file_a`              | `label`           | **Yes**               | The first file to compare (often a generated file/target output).       |
| `file_b`              | `label`           | **Yes**               | The second file to compare (often your expected "golden" file).         |
| `unified`             | `int`             | No (Default: `3`)     | The number of context lines to display on match failure.                |
| `ignore_case`         | `bool`            | No (Default: `false`) | If `true`, enables case-insensitive comparison.                         |
| `ignore_space_change` | `bool`            | No (Default: `false`) | Ignores changes in whitespace spacing amount.                           |
| `ignore_blank_lines`  | `bool`            | No (Default: `false`) | Ignores runs of empty lines.                                            |
| `args`                | `list of strings` | No                    | Extra command-line arguments to pass directly to the underlying binary. |
| `data`                | `list of labels`  | No                    | Additional runfiles required by the test.                               |

### Advanced `BUILD` Example

This configuration dynamically generates an output file, then uses `diff_test` with custom matching rules to ignore formatting differences:

```bazel
load("@mbo//mbo/diff:diff.bzl", "diff_test")

# Generate some configuration or build output
genrule(
    name = "generate_config",
    srcs = ["template.conf"],
    outs = ["generated.conf"],
    cmd = "$(location //tools:config_builder) --input=$< --output=$@",
    tools = ["//tools:config_builder"],
)

# Perform strict comparison but ignore minor whitespace styling and blank lines
diff_test(
    name = "verify_config_generation",
    file_a = ":generated.conf",
    file_b = "//testdata:golden_config.conf",
    ignore_space_change = True,
    ignore_blank_lines = True,
    unified = 5,
)
```

Run the validation test with:

```bash
bazel test //path/to/package:verify_config_generation
```
