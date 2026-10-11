# `mbo/mope`

- Namespace: `mbo::mope`.
- Scope: template expansion, hierarchical section data, INI loading, CLI, and Bazel generation rules.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers, rules, and targets

| Source                       | Bazel target          | API                               |
| ---------------------------- | --------------------- | --------------------------------- |
| [mope.h](mope.h)             | `//mbo/mope:mope_cc`  | `Template`                        |
| [ini.h](ini.h)               | `//mbo/mope:ini_cc`   | `ReadIniToTemplate`               |
| [mope_main.cc](mope_main.cc) | `//mbo/mope`          | `mope` executable                 |
| [mope.bzl](mope.bzl)         | `//mbo/mope:mope_bzl` | `mope` rule and `mope_test` macro |

## Template API

| Operation                                     | Return                      | Behavior                                                                            |
| --------------------------------------------- | --------------------------- | ----------------------------------------------------------------------------------- |
| `Template::IsValidName(name)`                 | `bool`                      | Accept `[_a-zA-Z]\w*`                                                               |
| `SetValue(name, value, allow_update = false)` | `absl::Status`              | Store a string value; duplicate names fail unless an existing string may be updated |
| `AddSection(name)`                            | `absl::StatusOr<Template*>` | Append a dictionary under the section name                                          |
| `Expand(output)`                              | `absl::Status`              | Expand template text in the supplied `std::string&`                                 |
| `Expand(output, context_data)`                | `absl::Status`              | Expand with additional name/value context supplied as a `ConvertingScan`            |

- Values and section names: owned by `Template`.
- Same name with a different stored type: error; `allow_update` does not change types.
- Repeated `AddSection(name)`: append another occurrence, rather than retrieving an existing one.
- Returned section pointers: borrow section storage; adding further occurrences can invalidate them.
- Expansion: modifies the output in place; an error can leave partial changes.
- Unknown value tag: retained unchanged.
- Unknown section: removed with its contents.

```cpp
#include <string>

#include "absl/status/statusor.h"
#include "mbo/mope/mope.h"
#include "mbo/status/status_macros.h"

absl::StatusOr<std::string> RenderItems() {
  mbo::mope::Template data;
  MBO_ASSIGN_OR_RETURN(auto* first, data.AddSection("item"));
  MBO_RETURN_IF_ERROR(first->SetValue("name", "alpha"));
  MBO_ASSIGN_OR_RETURN(auto* second, data.AddSection("item"));
  MBO_RETURN_IF_ERROR(second->SetValue("name", "beta"));
  std::string output = R"({{#item:', '}}{{name}}{{/item}})";
  MBO_RETURN_IF_ERROR(data.Expand(output));
  return output;  // "alpha, beta"
}
```

## Template syntax

| Form                                  | Meaning                                                                    |
| ------------------------------------- | -------------------------------------------------------------------------- |
| `{{name}}`                            | Substitute a string value or active range/list value                       |
| `{{#section}}...{{/section}}`         | Expand once per section dictionary                                         |
| `{{#section:', '}}...{{/section}}`    | Join section expansions with the quoted separator                          |
| `{{#comment=}}...{{/comment}}`        | Remove the whole section                                                   |
| `{{#i=start;end}}...{{/i}}`           | Inclusive integer range with step one                                      |
| `{{#i=start;end;step;join}}...{{/i}}` | Inclusive range with explicit step and separator                           |
| `{{#item=[a,b,c];join}}...{{/item}}`  | List expansion; `{{item}}` is the current value                            |
| `{{name=value}}`                      | Set or update an expansion-context value; emit no text for the control tag |

- Range start/end/step: integer literals or references to numeric values; zero step is invalid.
- Positive step: stop after the upper endpoint; negative step: stop below the lower endpoint.
- Range/list join: quoted string or value reference.
- Section join: quoted text.
- List elements: quote and byte-escape handling from [ParseStringList](../strings/README.md#quoted-string-parsing).
- Control values: available during expansion; cannot override values stored directly in the template.
- Standalone section/control tags: surrounding indentation and an adjacent line ending are removed.

```text
{{#i=1;5;2;','}}{{i}}{{/i}}
{{#item=['a,b',c];' / '}}{{item}}{{/item}}
```

```text
1,3,5
a,b / c
```

## INI data

- Function: `ReadIniToTemplate(filename, root_template)`.
- Compatibility alias: `ReadIniToTemlate(filename, root_template)`.
- Return type: `absl::Status`.
- Section hierarchy: `.` separates nesting levels.
- Repeated occurrences: suffix a level with distinct `:identifier` values.
- Key/value entries: stored in the corresponding template dictionary.
- INI parser: [mbo/file/ini](../file/README.md#ini-files).

```ini
[person]
name=Alice
[person.contact]
phone=1234
[person.contact:second]
phone=5678
[person:second]
name=Bob
```

## Command line

```sh
bazel run --config=clang //mbo/mope -- \
  --template=example.mope --generate=example.txt --ini=example.ini

bazel run --config=clang //mbo/mope -- \
  --template=example.mope --ini= --set=name=alpha
```

| Flag         | Default         | Meaning                                 |
| ------------ | --------------- | --------------------------------------- |
| `--template` | Empty; required | Input template path                     |
| `--generate` | `-`             | Output path; empty or `-` writes stdout |
| `--ini`      | `-`             | INI input; empty disables INI loading   |
| `--set`      | Empty list      | Comma-separated assignments             |

- `--set=name=value`: root template value.
- `--set=section:name=value`: value in a newly added section.
- `--set=outer:inner:name=value`: nested sections.
- `--set=:name=value`: global expansion context.
- `--set=section:enable`: create a section with an empty `enable` value.
- Exit code: zero on success; one on usage or processing failure.

## Bazel generation

```starlark
load("@mboworks_mbo//mbo/mope:mope.bzl", "mope", "mope_test")

mope(
    name = "generated_header",
    srcs = ["example.h.mope"],
    outs = ["example.h"],
    data = ["example.h.ini"],
    clang_format = True,
)

mope_test(
    name = "example_test",
    srcs = ["example.mope"],
    outs = ["example.golden"],
)
```

| Attribute       | Rule behavior                                                                                                  |
| --------------- | -------------------------------------------------------------------------------------------------------------- |
| `srcs`          | `.mope` or `.tpl` templates                                                                                    |
| `outs`          | Matching outputs; compare names after removing template suffixes and optional `.gen`/`.golden` output suffixes |
| `data`          | INI inputs; match an INI basename to the corresponding template basename                                       |
| `args`          | Additional CLI arguments                                                                                       |
| `clang_format`  | Format generated output; default `False`                                                                       |
| `sort_includes` | Include sorting for formatting actions; default `True`                                                         |

- `mope_test` name: must end in `_test` or `_tests`.
- `mope_test`: generate `.gen` files and compare each against its supplied golden with `diff_test`.
- Formatter selection: `--//mbo/mope:clang_format`; empty selects the configured hermetic formatter;
  an absolute executable path overrides it; `clang-format-auto` enables discovery.
- Failed formatter discovery: copy generated output without formatting and emit a warning.

## Tests

```sh
bazel test --config=clang //mbo/mope/...
```

- [mope_test.cc](mope_test.cc): library behavior and error cases.
- [tests](tests): template, INI, and golden fixtures.
- [CLI argument tests](tests/args/BUILD.bazel): section and global context assignments.
