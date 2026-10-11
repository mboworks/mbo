# File utilities

`mbo::file` provides file reading and writing, metadata and path operations, recursive globbing,
and INI parsing. These components expose separate C++ targets, and the `glob` command-line tool
provides filesystem pattern matching.

| Component | Bazel target                 | Header                    | Purpose                                      |
| --------- | ---------------------------- | ------------------------- | -------------------------------------------- |
| Artefact  | `//mbo/file:artefact_cc`     | `mbo/file/artefact.h`     | In-memory file data, name, and modified time |
| File      | `//mbo/file:file_cc`         | `mbo/file/file.h`         | File contents, metadata, and path helpers    |
| Glob      | `//mbo/file:glob_cc`         | `mbo/file/glob.h`         | Glob translation and recursive traversal     |
| INI       | `//mbo/file/ini:ini_file_cc` | `mbo/file/ini/ini_file.h` | INI parsing and structured access            |

## Artefacts

`Artefact` groups a file's name, contents, and modification time for APIs that need to transport a
file as data rather than operate on an open stream or filesystem entry.

## File and path operations

`file.h` provides status-returning operations for reading and writing complete files, reading a
bounded number of lines, retrieving modification times, checking readability, and manipulating
paths. `JoinPathsRespectAbsolute` resets the accumulated path when it encounters an absolute
component; `JoinPaths` performs ordinary joining. `NormalizePath` and `IsAbsolutePath` provide the
corresponding normalization and classification helpers.

## INI files

`ini/ini_file.h` provides `IniFile`, a small INI reader with section and value access. Parsing errors
are returned as Abseil statuses. The parser is fuzz-tested; its tests also document accepted comment,
section, key, and value forms.

## Glob patterns

The glob implementation is fast, deterministic, locale-independent, and limited to features RE2 can
express efficiently. It takes useful POSIX pattern-matching behavior, adds path globstar and SHGLOB
alternatives, and omits locale and general subexpression negation features that RE2 cannot represent
faithfully.

### Semantic contract

The glob syntax is independent of the process locale. Ranges and named classes have the stable
definitions documented by RE2. Locale-sensitive collation, collating symbols, equivalence classes,
and locale-sensitive case folding are unsupported.

`/` is always the path separator on every platform. Component-local operators never consume it.
Repeated separators are normalized consistently. The historical exact `[/]` spelling remains a
documented mbo compatibility spelling for a separator; no other bracket expression consumes `/`.

| Feature                      | Required behavior                                               |
| ---------------------------- | --------------------------------------------------------------- |
| Literal                      | Match itself, escaped for RE2 as necessary                      |
| Backslash                    | Escape the following glob character                             |
| `?`                          | Match one non-`/` character                                     |
| `*`                          | Match zero or more non-`/` characters                           |
| Complete-component `**`      | Match zero or more complete path components                     |
| Embedded star run            | Behave as one component-local `*`                               |
| Positive bracket expression  | Match one listed non-`/` character                              |
| Negated bracket expression   | Match one non-`/` character not listed                          |
| Named class                  | Use the documented RE2 ASCII class with `/` removed             |
| GLOB braces                  | Remain literal                                                  |
| SHGLOB braces                | Provide nested alternatives, including empty alternatives       |
| General negative extglob     | Unsupported because RE2 has no general subexpression complement |
| Lookahead and lookbehind     | Unsupported                                                     |
| Collating/equivalence syntax | Unsupported                                                     |

Negated bracket expressions are supported. They are not general negative lookahead: `[!a]` becomes
the equivalent of `[^/a]`, while a construct such as `!(foo|bar)` is outside this RE2-native syntax.

### Globstar contract

A run of exactly two or more unescaped stars crosses directories only when it occupies a complete
path component. In other positions it reduces to an ordinary component-local `*`.

`foo/**/bar` matches all of:

```text
foo/bar
foo/a/bar
foo/a/b/bar
foo/a/b/c/bar
```

The following boundary forms have distinct documented behavior:

```text
**
**/bar
foo/**/bar
foo/**
foo**bar
***
foo/***/bar
```

Trailing `foo/**` matches descendants of `foo` but not `foo` itself. Middle `foo/**/bar` allows zero
intervening directory components. Embedded and escaped stars remain component-local, and repeated
separators are normalized.

### Bracket expressions

Bracket expressions support:

- leading `]`;
- leading and trailing literal `-`;
- escaped members;
- multiple ranges;
- slash at either range endpoint;
- ranges crossing slash;
- mixed literals, ranges, and named classes;
- classes that become empty after slash removal;
- ascending, descending, malformed, and unterminated ranges;
- named classes that otherwise include slash, including `ascii`, `graph`, `print`, and `punct`.

Unsupported or malformed syntax returns a precise status instead of being passed to RE2 with changed
meaning. The translator forms a structural ASCII character set, removes `/`, and emits normalized
contiguous RE2 ranges.

### Public API

The pure public translator supports GLOB and SHGLOB without requiring another parser:

```cpp
enum class GlobSyntax {
  kGlob,
  kShGlob,
};

struct Glob2Re2Options {
  GlobSyntax syntax = GlobSyntax::kGlob;
  bool allow_star_star = true;
  bool allow_ranges = true;
  RE2::Options re2_options;
};
```

`Glob2Re2Expression` returns the translated expression. `Glob2Re2` additionally compiles it and
reports RE2 diagnostics as an `absl::Status`. `GlobSplit` separates a filesystem root from its
pattern. `Glob` traverses the filesystem and invokes a callback for each matching entry; `GlobRe2`
does the same with an already compiled expression.

Consumers such as xff can map GLOB and SHGLOB directly while retaining application-specific
preprocessing for negation, anchoring, and directory-only rules.

### Performance considerations

Translation is single-pass apart from nested SHGLOB alternatives. Output is reserved once and
appended monotonically without allocation per input character or bracket member. Matching retains
RE2's linear-time behavior; compile a pattern once when matching it repeatedly.
