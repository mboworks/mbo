# `mbo/strings`

- Namespace: `mbo::strings`.
- Scope: containment, indentation, numeric formatting, quoted-string parsing, splitting, comment
  removal, and dense string identifiers.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                     | Bazel target                 | API                                                         |
| -------------------------- | ---------------------------- | ----------------------------------------------------------- |
| [contains.h](contains.h)   | `//mbo/strings:contains_cc`  | `Contains`                                                  |
| [indent.h](indent.h)       | `//mbo/strings:indent_cc`    | `DropIndent`, `DropIndentAndSplit`                          |
| [numbers.h](numbers.h)     | `//mbo/strings:numbers_cc`   | `BigNumber`, `BigNumberLen`                                 |
| [parse.h](parse.h)         | `//mbo/strings:parse_cc`     | `ParseOptions`, `ParseString`, `ParseStringList`, `PopChar` |
| [split.h](split.h)         | `//mbo/strings:split_cc`     | `AtLast`                                                    |
| [strip.h](strip.h)         | `//mbo/strings:strip_cc`     | Prefix/suffix and comment removal                           |
| [string_id.h](string_id.h) | `//mbo/strings:string_id_cc` | `StringId`, `StringIdRepresentation`                        |

## Containment and numeric formatting

| Function                          | Behavior                                                                                               |
| --------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `Contains(haystack, string_view)` | C++23 `string_view::contains`; empty needle always matches                                             |
| `Contains(haystack, char)`        | Search for one character, including an embedded NUL                                                    |
| `BigNumber(value)`                | Decimal integral formatting with apostrophes between groups of three digits                            |
| `BigNumberLen(value)`             | Length of the grouped decimal representation, including a minus sign; integral types up to eight bytes |

- `Contains`: `constexpr`, `noexcept`, case-sensitive.
- `BigNumber`: returns an owning `std::string`; no locale-dependent grouping.

```cpp
#include <string>

#include "mbo/strings/contains.h"
#include "mbo/strings/numbers.h"

static_assert(mbo::strings::Contains("alpha", "ph"));
static_assert(mbo::strings::Contains("", ""));

const std::string grouped = mbo::strings::BigNumber(1'234'567);  // "1'234'567"
const unsigned width = mbo::strings::BigNumberLen(-1'234);       // 6
```

## Indentation

| Function                   | Result                                | Ownership                                           |
| -------------------------- | ------------------------------------- | --------------------------------------------------- |
| `DropIndent(text)`         | De-indented text joined with newlines | Owning `std::string`                                |
| `DropIndentAndSplit(text)` | De-indented lines                     | `std::vector<std::string_view>` borrowing the input |

- Leading newline: removed before determining indentation.
- Indentation source: first remaining line after a leading newline; otherwise the second line.
- Removed prefix: exact leading sequence of spaces and tabs from that line.
- Subsequent lines: remove that prefix only when present; no minimum-indent calculation.
- Whitespace-only final line: cleared.
- `DropIndent("")` and `DropIndent("\n")`: return their input unchanged.
- Input backing storage: must outlive views returned by `DropIndentAndSplit`.

## Quoted-string parsing

| Function                         | Result                                     | Input handling                                                                      |
| -------------------------------- | ------------------------------------------ | ----------------------------------------------------------------------------------- |
| `ParseString(options, data)`     | `absl::StatusOr<std::string>`              | Consume a prefix of `std::string_view& data`; leave a stopping delimiter unconsumed |
| `ParseStringList(options, data)` | `absl::StatusOr<std::vector<std::string>>` | Consume list items and separator characters; retain an encountered stop character   |
| `PopChar(data)`                  | First character                            | Require nonempty input and remove one character                                     |

| `ParseOptions` field   | Default       | Effect                                                                      |
| ---------------------- | ------------- | --------------------------------------------------------------------------- |
| `stop_at_any_of`       | Empty         | Stop at any listed unquoted character                                       |
| `stop_at_str`          | Empty         | Stop `ParseString` at an unquoted occurrence of the sequence                |
| `split_at_any_of`      | Empty         | Split list items; stop a single-string parse; list default is comma         |
| `enable_double_quotes` | `true`        | Interpret double quotes                                                     |
| `enable_single_quotes` | `true`        | Interpret single quotes                                                     |
| `remove_quotes`        | `true`        | Remove interpreted quote delimiters                                         |
| `allow_unquoted`       | `true`        | Accept unquoted text; false stops at the first unquoted non-quote character |
| `custom_escapes`       | `(){}[]<>,;&` | Backslash followed by a listed character produces that character            |

- Escapes: simple C++ escapes, byte-valued octal/hexadecimal escapes, and configured custom escapes.
- Numeric forms: octal digits, `\o{...}`, `\x...`, `\x{...}`.
- Unicode and named-character escapes: `Unimplemented` status.
- Invalid escape or unterminated quote: `InvalidArgument` status.
- Whitespace: preserved unless explicitly configured as a separator/stop or inside later stripping.
- Empty list input: empty vector; leading, adjacent, and trailing separators preserve empty items.
- Parsing errors: input may already have been consumed; no rollback guarantee.
- `ParseOptions` string fields: borrowed views.

```cpp
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "mbo/strings/parse.h"

absl::StatusOr<std::vector<std::string>> ParseNames() {
  std::string_view input = R"("alpha,beta",gamma;tail)";
  return mbo::strings::ParseStringList({.stop_at_any_of = ";"}, input);
  // Values: {"alpha,beta", "gamma"}; remaining input: ";tail".
}
```

## Splitting at the final separator

```cpp
#include <string>
#include <utility>

#include "absl/strings/str_split.h"
#include "mbo/strings/split.h"

const std::pair<std::string, std::string> parts = absl::StrSplit("archive.tar.gz", mbo::strings::AtLast('.'));
// parts.first == "archive.tar"; parts.second == "gz".
```

- `AtLast(char)`: delimiter adapter for `absl::StrSplit`; at most two parts.
- Result ownership: determined by the destination type selected for `absl::StrSplit`.

## Prefixes, suffixes, and comments

| Function                              | Behavior                                                                     |
| ------------------------------------- | ---------------------------------------------------------------------------- |
| `ConsumePrefix(string&, prefix)`      | Remove a matching prefix; return whether it matched                          |
| `ConsumeSuffix(string&, suffix)`      | Remove a matching suffix; return whether it matched                          |
| `StripPrefix(string&&, prefix)`       | Return the string with a matching prefix removed                             |
| `StripSuffix(string&&, suffix)`       | Return the string with a matching suffix removed                             |
| `StripLineComments(line, args)`       | Return the prefix before the first literal comment marker as a borrowed view |
| `StripComments(input, args)`          | Apply literal comment removal per line; return owning text                   |
| `StripParsedLineComments(line, args)` | Parse quotes/escapes and stop markers; return `StatusOr<std::string>`        |
| `StripParsedComments(input, args)`    | Apply parsed comment removal per line; return `StatusOr<std::string>`        |

- `StripCommentArgs`: `comment_start`, `strip_trailing_whitespace = true`.
- Literal stripping: no quote awareness; trailing ASCII whitespace removed only when a marker occurs.
- `StripParsedCommentArgs`: `ParseOptions parse`, `strip_trailing_whitespace = true`.
- Parsed stripping: `parse.stop_at_any_of` and `parse.split_at_any_of` select comment characters;
  quote removal and escape decoding follow `parse`.
- Parsed stripping failures: `InvalidArgument` status.

## String identifiers

- `StringId<Representation = std::uint32_t>`: alias of `mbo::types::ConstStrongId` with a string ID tag.
- Supported representations: exactly `std::uint8_t`, `std::uint16_t`, `std::uint32_t`, `std::uint64_t`.
- Zero: valid; maximum representation value: `invalid_value`.
- Default state: invalid.
- Construction: explicit representation value or checked `try_from_ordinal(integer)`.
- Observation: `value()`, `is_valid()`; comparisons and standard/Abseil hashing.
- Copy/move construction and move assignment: supported; copy assignment and arithmetic: disabled.
- Identifier only: no string storage, lookup table, or association between an ID and text.
- Underlying contract: [mbo/types](../types/README.md).

## Tests

```sh
bazel test --config=clang //mbo/strings:all
```

- Header-specific `*_test.cc` files: examples and boundary cases.
- [parse_fuzz_test.cc](parse_fuzz_test.cc): parser fuzz cases.
