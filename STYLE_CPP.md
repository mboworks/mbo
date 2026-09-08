# C++ Style

The C++ coding style for MBO Works repositories. It sits on top of two
machine-enforced config files and adds the human conventions below. When in doubt
the config files win; this document explains and extends them so a contributor (or
an AI assistant) can follow them without reverse-engineering the tooling.

## Toolchain (machine-enforced)

- C++20, compiled with `clang`. Compilation with GCC is kept working on a
  best-effort basis.
- **`clang-format`** with [`.clang-format`](.clang-format) formats all C++ code. Run
  it; do not hand-format against it. CI rejects any reformatting diff.
- **`clang-tidy`** with [`.clang-tidy`](.clang-tidy) (`WarningsAsErrors: '*'`) runs via the
  opt-in `clang-tidy` pre-commit hook (`pre-commit run clang-tidy --all-files --hook-stage manual`,
  which shells out to [`tools/clang_tidy.sh`](tools/clang_tidy.sh)) against a `compile_commands.json`
  you generate with [`./compile_commands-update.sh`](compile_commands-update.sh). It is report-only
  (never `--fix`) and needs a hermetic clang-tidy (>= clang-22 for this C++20 code); it skips cleanly
  when either is missing. It is **not** run by `trunk` (trunk pinned clang-tidy 16, which mis-parses
  the code and auto-applied build-breaking fixes - do not re-add it there). In **CI** the dedicated
  `clang-tidy` job owns it: it builds the compile DB (`compile_commands-update.sh`, hermetic clang)
  and runs this hook, report-only (`continue-on-error`) until the finding sweep lands, then a hard
  gate. A branch lints only the sources it changed; `main` lints the whole tree. The enabled set is
  broad: `abseil-*`, `bugprone-*`, `cppcoreguidelines-*`, `google-*`, `misc-*`, `modernize-*`,
  `performance-*`, `portability-*`, `readability-*`.

### What `.clang-format` decides (do not fight it)

- Google base style, **column limit 120**.
- `AlignAfterOpenBracket: AlwaysBreak` + `BinPackParameters: false`: when arguments
  do not fit, each goes on its own line at one indent (not aligned to the paren).
- `PointerAlignment: Left` (`int* p`), `QualifierAlignment: Left` (`const int`).
- `IntegerLiteralSeparator` decimal every 3: write `1'000'000`.
- `RemoveSemicolon`, `InsertBraces` (always brace bodies), `SeparateDefinitionBlocks`.
- **`InsertTrailingCommas` is deliberately OFF** (it would force every aggregate to
  never-bin-pack). That is the lever behind the trailing-comma rule below: a _manual_
  trailing comma opts a _single_ aggregate into one-element-per-line.

### Naming (enforced by `.clang-tidy readability-identifier-naming`)

- Types / classes / structs / enums / aliases / functions: `CamelCase` (`LimitedMap`).
- Variables / parameters / data members / namespaces: `lower_case` (`flag_count`).
- Private data members: `lower_case` with a trailing `_` (`flags_`).
- constexpr / enum constants / global + static constants: `k` + `CamelCase` (`kMaxDepth`).
- Macros: `UPPER_CASE`, prefixed `MBO_` (`MBO_...`).

## Code organization

- All exported code lives under the top library directory (in mbo: `mbo/`).
- Each subdirectory uses namespace `mbo::{dir}` (and `mbo::{dir}::{sub}`).
  - Internal-only code: `mbo::{dir}::{sub}_internal`. No namespace component is ever
    just `internal`.
  - Exported-but-detail code may use a `detail` sub-namespace (fully qualify it, since
    the bare `detail` name can collide).
- Header guards: `{PATH}_{FILE}_` (path + filename, uppercased, non-alphanumerics ->
  `_`, trailing `_`). A file may not start with a sibling directory's basename + `_`:
  `foo/bar.h` and `foo_bar.h` would both yield `FOO_BAR_H_`.
- Forward-declared symbols must be fully implemented in the same source file (except
  the `Impl`-in-header / implementation-in-`.cc` pattern).
- Macros: avoid them. An **exported** macro (defined in a header for other code to use) is
  prefixed `MBO_` (private impl `MBO_PRIVATE_...`). A macro **local to one translation unit**
  - a `.cc` / test file, or a header helper you `#undef` right after its last use - may stay
    unprefixed (still `UPPER_CASE`); `#undef` a local macro as soon after its last use as
    possible.
- **C-library names: use the `std::` form; the unqualified global alias is optional.** A
  `<cstddef>` / `<cXXX>` header is only required to declare `size_t`, `int64_t`, `memcpy`,
  ... in `namespace std`; whether it _also_ injects the bare global name is
  implementation-defined, so we never rely on it.
  - In **headers**, always fully qualify (`std::size_t`); no namespace-level `using`.
  - In an **implementation file** (`.cc`), a `using std::size_t;` (or other `using std::...`)
    is fine where it reads better - bring the name in explicitly rather than leaning on the
    global alias. Be consistent within a file, but readability outranks consistency.
- Library flags are prefixed by their path/namespace (e.g. `--mbo_log_timing_min_duration`
  in `mbo/log`). Application entry-point binaries (`glob`, `diff`, `mope`) may use bare flag
  names (`--depth`, `--algorithm`, `--template`).

## Formatting conventions on top of clang-format

clang-format picks a layout per line; these habits steer it toward the readable one.

1. **No comment at the end of a long line.** Put the comment on its own line _above_
   the element. A trailing `// ...` that pushes a line past 120 makes clang-format
   explode the element across several lines.

   ```cpp
   // -exec run in the matched entry's directory
   {.name = "-execdir", .kind = Kind::kAction, .arity = -1},
   ```

2. **Trailing comma on the last field of a complex aggregate** breaks it one field per
   line. Because `InsertTrailingCommas` is off, the comma is your per-aggregate opt-in.
   Use it for long/complex initializers; a short one that reads on a single line stays.

   ```cpp
   const Drop drop{
       .line = line,
       .layer = Source::kProject,
       .safety = Safety::kSecurity,
   };
   ```

   **In a long array of struct literals - a registry-style table such as `kGlobals` or
   `kDescriptors` - put the trailing comma on _every_ element**, even the short ones that
   would fit on a single line, so clang-format expands the whole table uniformly, one
   field per line. A consistent table you scroll through reads better than a mix of
   one-liners and exploded rows packed to save height. This stays a deliberate, per-table
   choice made element by element: `InsertTrailingCommas` is off (we do not always want
   trailing commas), so clang-format never forces it for you.

3. **Force a line break with a comment rather than let clang-format cram a value at the right
   margin.** A long argument - especially a raw string such as a proto `R"pb(...)pb"` - otherwise
   gets packed onto the call line and shoved against the 120 column, unreadable. A trailing
   comment makes clang-format keep the element on its own line. Mark it `// NL` ("new line"):

   ```cpp
   EXPECT_THAT(  // NL
       message,
       EqualsProto(R"pb(name: "n" value: 1)pb"));
   ```

   When there is a relevant reason, keep the prefix and add it: `// NL: <short reason>`, preferred
   over a bare `// NL`. Always keep the `NL` prefix - do not drop to a bare `//` or an unprefixed
   comment - for two reasons: it marks the comment as load-bearing for layout, so a reader knows
   that removing it re-crams the line; and the consistent marker is machine-checkable, so a
   pre-commit rule can verify these lines stay broken.

   Reserve `// clang-format off` / `on` for a genuine table or hand-aligned expression that
   clang-format cannot lay out - `// NL` only _inserts_ breaks, so it cannot keep an over-120
   line whole or stop a reflow (e.g. a one-line-per-case test table, or a multi-clause
   `requires(...)`). Do not use it to hand-place ordinary layout, and keep the `off`/`on` pair
   tight and adjacent so the `on` is never forgotten: everything between the two loses every
   formatting guarantee above.

## Idioms

- **A `cc_library` target is named `<thing>_cc`** (`file_cc`, `diff_options_cc`, `hash_cc`), so a
  label reads as "the C++ library for `<thing>`" and never collides with the directory,
  header, or binary of the same name. `cc_test` keeps its `_test` suffix, and a `cc_binary`
  keeps the plain program name (`mope`, `diff`). Enforced by the `cc-target-naming` pre-commit
  hook ([`tools/check_cc_target_naming.py`](tools/check_cc_target_naming.py)), so the
  convention cannot drift back the way four extras targets did.
- **Every `cc_library` has a test in its own package depending on it.** This is the
  Testing section's "all exported code must be tested" stated as a build rule, and it is
  enforced by the `check-cc-library-tested` pre-commit hook
  ([`tools/check_cc_library_tested.py`](tools/check_cc_library_tested.py)). Two things
  deliberately do **not** count: _transitive_ coverage (a test of a library that merely
  deps yours) and a non-test dependent (a `cc_binary`, a `bashtest`) - neither exercises
  your unit directly. Same-package is part of the rule too: the test belongs next to the
  code it covers. A library with genuinely no testable surface goes into the tool's
  `_ALLOWLIST` **with its reason recorded there** (today's single entry is
  `//mbo/container:limited_set_benchmark_cc`, the benchmark harness, which makes no
  correctness claim a test could check).
- **Never range-iterate an inline braced-init-list; iterate a NAMED `constexpr std::array`.** A loop
  over `{a, b, c}` hides what the set IS behind the mechanics of visiting it, gives the reader nothing
  to grep for, and puts the data at the point of use so the next case that needs the same set copies it
  instead of sharing it. Build the set with `std::to_array<T>({...})` (a trailing comma on the last
  element, so `clang-format` breaks it one per line - see the Formatting section) and give it a `k`
  name that says what the set is, not what the loop does. `static constexpr` inside a function when it
  is used once; at namespace scope in the anonymous namespace when more than one case needs it.
  Deducing `std::array` from `to_array` also keeps the element type in exactly one place, and an
  explicit type stops a literal from being deduced as `const char*` where `std::string_view` was meant.

  ```cpp
  // POSIX precedence: LC_ALL overrides LC_CTYPE, which overrides LANG.
  static constexpr std::array kLocaleVars = std::to_array<std::string_view>({
      "LC_ALL",
      "LC_CTYPE",
      "LANG",
  });
  for (const std::string_view var : kLocaleVars) {  // not: for (... : {"LC_ALL", "LC_CTYPE", "LANG"})
  ```

  Enforced by the `no-braced-init-list-loop` pre-commit hook, so it cannot drift back. This is about a
  RANGE-FOR over a literal list; passing a braced list as an argument (`ElementsAre`, a `std::vector`
  initializer, an aggregate) is untouched by the rule.

- **Prefer range-based loops.** Use an index only when the index is part of the operation. Do not
  scan backward or repeatedly rescan preceding elements; carry the needed state forward.

- **Comparison functions name their parameters `lhs` and `rhs`** - a sort comparator, an
  `operator==`/`operator<=>`, any two-things-of-one-type predicate. Never `a`/`b` (which
  `readability-identifier-length` rejects at under 2 characters) and not `x`/`y` either: `lhs`/`rhs`
  names the ROLE, so `lhs.name < rhs.name` reads as the ordering it implements.

  ```cpp
  absl::c_sort(topics, [](const HelpTopic& lhs, const HelpTopic& rhs) { return lhs.name < rhs.name; });
  ```

- **A defaulted move constructor and move assignment are explicitly `noexcept`.**
  `Type(Type&&) noexcept = default;` and `Type& operator=(Type&&) noexcept = default;`. A
  defaulted move is only _implicitly_ `noexcept` when every base and member move is, so one
  member that is not silently makes the whole type's move throwing - and the standard library
  then quietly downgrades: `std::vector` reallocation copies instead of moving
  (`move_if_noexcept`), and `std::swap` and friends lose their strong guarantee. Spelling
  `noexcept` out states the intent and turns a violation into a compile error rather than a
  silent performance loss. The same applies to a defaulted destructor's implicit `noexcept`,
  which needs no annotation.

- **An initialised array constant uses `std::to_array`, never a hand-counted size.**
  `static constexpr auto kExamples = std::to_array<DocPair>({...});`, not
  `static constexpr std::array<DocPair, 6> kExamples = {...}`. The literal size is a second
  statement of something the initialiser already says, and the two drift: adding an entry and
  forgetting the count is a compile error at best and a silently truncated or
  default-padded array at worst. `to_array` deduces the extent, so there is nothing to keep in
  sync. Keep an explicit extent only where the size is part of the CONTRACT rather than a count
  of what was written - a fixed-size buffer, a spec-mandated width (`std::array<uint64_t, 8>` for
  BLAKE2b's state), or a function parameter type, where the extent IS the type.

- **Pass `absl::Status` by value**, not `const&`: it is a tagged pointer, and
  `.clang-tidy performance-unnecessary-value-param` allowlists it (with `absl::StatusOr`
  and `std::string_view`). `StatusOr<T>`'s cost depends on `T`.
- **Prefer container algorithms** from `absl/algorithm/container.h` (`absl::c_contains`,
  `c_any_of`, `c_find`, `c_equal`, `c_sort`, ...) or C++20 `std::ranges` over hand-rolled
  loops: range-based (no begin/end), well-named, constexpr-friendly. A loop that returns
  on the first match is an `absl::c_any_of` with a lambda; a reserve-then-push copy into a
  `std::vector` is range construction `std::vector<T>(src.begin(), src.end())`. Keep a raw
  loop only when it builds a non-trivial structure no algorithm expresses cleanly.
- **Container choice: prefer the Abseil containers over the bare `std::` ones.** The reason is
  in their favor, not against `std::`: the Abseil variants are faster, support transparent
  (heterogeneous) lookup - find in a `std::string`-keyed map with a `std::string_view`, no
  temporary `std::string` - and `flat_hash_*` stores elements inline for lower memory.
  - **Never `std::unordered_*`** - no `<unordered_map>` / `<unordered_set>` include, and none
    of `unordered_map` / `unordered_set` / `unordered_multimap` / `unordered_multiset`. Use
    `absl::flat_hash_map` / `flat_hash_set`, or the `node_hash_map` / `node_hash_set` variants
    when you need pointer/reference stability. This holds in tests too - the one exception is a
    container-compatibility test that deliberately exercises a `std::unordered_*` input.
  - **Avoid `std::map` / `std::set`** (and the `multi` variants) in library code; prefer
    `absl::btree_map` / `absl::btree_set`, which keep the same ordered interface but are
    cache-friendlier. In tests, `std::map` / `std::set` are fine.
  - Small compile-time tables: `mbo::container::LimitedMap` / `LimitedSet`.
  - A type-detection trait may name a `std::` container type freely - it must, to recognize it.
- **A read-only string parameter is `std::string_view` (by value), not `const std::string&`.**
  This is pretty much always: a `std::string_view` binds to a `std::string`, a string literal,
  a `char*`, or another view with no allocation and no `.c_str()` dance, so the `const&` only
  narrows what callers may pass. Keep `const std::string&` (or `std::string` by value) only
  when the body genuinely needs a `std::string` - it calls `.c_str()` for a C API, stores or
  moves the argument, or passes it to something that itself wants `const std::string&`.
- **A by-value `std::string_view` follows ordinary const-correctness: `const` when you never
  mutate the view, non-`const` only when you do.** The characters it views are already `const`,
  but the view itself is a normal local: `const` documents that you never reslice or reassign it,
  and `misc-const-correctness` (enabled) flags a never-mutated view that is not `const`. Drop the
  `const` exactly when you mutate the view in place - `remove_prefix`, `remove_suffix`, or
  reassignment. (This concerns the view; not `std::string_view::size_type`, which is a `size_t`,
  nor `absl::Span<const std::string_view>`, where `const` is the span's element type.)
  **Corollary: when a loop view needs trimming, iterate a mutable (non-`const`) by-value view and
  mutate it in place - never `const` the loop view and then copy it to a mutable local just to
  mutate the copy.**
- **This applies to LOCALS, never to PARAMETERS. A by-value parameter is never top-level
  `const`** - not `std::string_view`, and not any other simple type (`int`, `bool`,
  `std::size_t`, ...). The top-level `const` is invisible to callers (it is not part of the
  function type, so it does not even change the signature), it is noise in every declaration,
  and on a `std::string_view` it forbids exactly what the type is designed for: re-slicing the
  parameter in place with `remove_prefix` / `remove_suffix` instead of copying it to a mutable
  local first. The characters a view refers to are already `const`; the view itself is meant to
  be movable over them.

  ```cpp
  std::string Write(std::string_view name, std::string_view content);       // yes
  std::string Write(const std::string_view name, const std::string_view c); // no: const by value
  ```

  ```cpp
  const std::string_view name = label.empty() ? id : label;      // read-only: const
  for (std::string_view line : absl::StrSplit(text, '\n')) {      // mutated: non-const, trim in place
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  }
  for (const std::string_view raw : absl::StrSplit(text, '\n')) {  // no: const + copy-to-mutate
    std::string_view line = raw;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  }
  ```

- **Do not overload `const T*` to express "optional" or to conflate omission with value.** A
  raw pointer mixes nullability, the pointed-at value, and ownership/lifetime into one type
  the caller has to second-guess. For an optional reference use
  `std::optional<std::reference_wrapper<T>>` or, preferably, mbo's `mbo::types::OptionalRef<T>`
  (`mbo/types/optional_ref.h`) and its related types (e.g. `OptionalDataOrRef` when it may own
  a value or refer to one).
- **Mark a return value `[[nodiscard]]`** when silently ignoring it is a bug - an
  `absl::Status` / `StatusOr`, a parsed result, a `Consume...` "did it match?" flag, an acquired
  handle. The exception is a function _designed_ for its result to be optionally used: a builder
  method returning `*this` for chaining, or a mutator that also returns the previous value as a
  convenience. We disable `modernize-use-nodiscard` in `.clang-tidy` precisely because it would
  blanket-annotate every const method - apply `[[nodiscard]]` by judgment instead.
- **switch / case**: order case labels alphabetically. Where the cases are a uniform
  mapping (key -> handler or value), prefer a constexpr `mbo::container::LimitedMap`
  lookup over a switch.
- **No em-dashes** anywhere (code, comments, docs, commit messages). Use a spaced
  hyphen `-`.

## Error handling: `absl::Status` and the MBO status macros

### Exception policy

- **mbo's own builds are exception-free.** The repository-wide Bazel configuration
  compiles C++ with `-fno-exceptions`; production mbo code must not depend on
  throwing or catching exceptions for its normal control flow. Use
  `absl::Status` / `absl::StatusOr` and the status macros below for recoverable
  errors. `MBO_CONFIG_REQUIRE` uses fatal logging in this build.
- **Public headers must remain compatible with exception-enabled consumers.** A
  downstream project may compile template code with exceptions enabled, and an
  operation supplied by its type `T` may throw. Do not hide that behind an
  unconditional `noexcept`: derive the exception specification from the invoked
  construction, assignment, comparison, or callable operation. The mbo type must
  retain a valid state while the exception propagates.
- **An exception-enabled test target is a narrow compatibility test.** A test may
  add `copts = ["-fexceptions"]` when it must execute a throwing user operation to
  verify the public-header contract above. Record that reason next to the target;
  do not enable exceptions for ordinary tests or use such a target as evidence
  that mbo production code itself may throw.
- `--//mbo/config:require_throws=true` selects the throwing
  `MBO_CONFIG_REQUIRE` behavior only when the consuming compilation also enables
  exceptions. With `-fno-exceptions`, requirements always use fatal logging.

Propagate errors with the macros from `mbo/status/status_macros.h`
(`@mboworks_mbo//mbo/status:status_macros_cc`), not a hand-written
`if (!x.ok()) return x.status();`.

- **A "value or error" type IS `absl::StatusOr<T>`.** Do not hand-roll a struct that bundles
  a value (or a `std::vector` / `std::optional`) with an `absl::Status` plus an "ok" flag:
  `absl::StatusOr<T>` is exactly that, enforces the not-ok-has-no-value invariant by
  construction, composes with the macros below, and matches every other API.
- **`MBO_RETURN_IF_ERROR(expr)`** evaluates a `Status` or `StatusOr` and returns early if
  it is not OK. It returns a `mbo::status::StatusBuilder`, which converts to the calling
  function's `absl::Status` or `absl::StatusOr<T>`, so it works in both.
- **`MBO_ASSIGN_OR_RETURN(Type var, expr)`** binds the value of a `StatusOr<Type>` to
  `var` (a new declaration - carry the type - or an existing variable) or returns the
  status. After it, `var` is the value (not a `StatusOr`); use `var`, not `*var`.
- **`MBO_MOVE_TO_OR_RETURN(expr, target)`** is the variant whose target may contain commas;
  the expression comes first so structured bindings work:
  `MBO_MOVE_TO_OR_RETURN(MakePair(), auto [a, b]);`.

```cpp
absl::StatusOr<Report> Build(std::string_view path) {
  MBO_ASSIGN_OR_RETURN(const Config config, LoadConfig(path));  // value, or return status
  Report report = Analyze(config);
  MBO_RETURN_IF_ERROR(Persist(report));                         // Status guard
  return report;
}
```

- When you only need the guard but the `StatusOr` value is large and used later through
  `*expr`, guard on the status to avoid copying the value: `MBO_RETURN_IF_ERROR(big.status());`
  then use `*big`.
- **Do not force the macros where they do not fit**: a recovery branch (e.g.
  `absl::IsNotFound(s)` -> return a default value), a function that returns `bool`/`int`
  rather than a status, or a CLI path that prints a message and returns an exit code.
- Pass `absl::Status` by value (see Idioms).

## Output, logging, and `AbslStringify`

- **Never use C strings or `.c_str()`** except to satisfy a C / system API (e.g. building
  `char* argv[]` for `execvp`); comment why at that one boundary. Keep `std::string` /
  `std::string_view` end to end - the round-trip out to a `const char*` and back is
  clutter, not interop.
- **Never write output through C stdio** (`printf` / `fprintf` / `fputs` to the `stdout` /
  `stderr` `FILE*`). Use the C++ streams `std::cout` / `std::cerr` (or the logging library).
  The anti-pattern to delete on sight: `std::fputs(absl::StrCat(...).c_str(), stderr)`.
- Format with Abseil: `absl::StrCat` for plain concatenation, `absl::StrFormat` for a
  formatted string, and **`absl::StreamFormat` to write a formatted line straight to a
  stream with no temporary**: `std::cerr << absl::StreamFormat("wrote %d entries\n", n);`.
  A plain `<<` is fine for a simple string. Choose per line, but **be consistent within a
  single function** (do not mix a `StreamFormat` and a `<<` chain in the same function).
- **Make your types printable with `AbslStringify`, not a hand-rolled `operator<<` /
  `ToString`.** Abseil's string-conversion extension point is a hidden-friend hook:

  ```cpp
  struct Point {
    int x = 0;
    int y = 0;
    template<typename Sink>
    friend void AbslStringify(Sink& sink, const Point& p) {
      absl::Format(&sink, "(%d, %d)", p.x, p.y);
    }
  };
  ```

  One hook makes the type work with `absl::StrCat`, `absl::StrFormat` / `StreamFormat`
  `%v`, `absl::StrJoin`, and Abseil logging - and gives GoogleTest readable failure output.
  `absl::Status` / `StatusOr` already stringify; print them with `%v`.

## Concurrency and thread safety

These are the baseline for any multi-threaded code, not extra credit; threading bugs are
silent and data-dependent, so the annotations and the sanitizer are the standing guard.
Most of this repo is single-threaded today, so this section is the standard to apply _when
you add_ multi-threaded code, not a description of current breadth.

- **Use `absl::Mutex` + `absl::MutexLock`**, not `std::mutex` / `std::lock_guard` or
  atomics-as-synchronization. Construct the lock from a reference: `absl::MutexLock lock(mu_);`
  (the pointer constructor is deprecated).
- **Annotate everything** (`absl/base/thread_annotations.h`): `ABSL_GUARDED_BY(mu_)` on
  every member the mutex protects, and `ABSL_LOCKS_EXCLUDED` /
  `ABSL_EXCLUSIVE_LOCKS_REQUIRED` on methods. A comment must state **exactly which members a
  given mutex guards**, and call out any shared state deliberately left unguarded together
  with the invariant that keeps it safe (e.g. "each index is handed to exactly one worker,
  so distinct elements never alias").
- **If a type has more than one mutex, document their lock-acquisition order** (which is
  taken before which) so the ordering that prevents deadlock is explicit.
- **Enforce the annotations**: compile first-party code with clang's `-Wthread-safety`
  (with `-Werror` an unguarded access becomes a build failure), and add a
  **ThreadSanitizer CI job** that runs the threaded tests so races are also caught at
  runtime. Scope the tsan job to the threaded targets; exclude heavy third-party-linked
  ones so tsan does not rebuild them.

## Protocol Buffers

- **Generated repeated fields and maps are STL-compatible - range-iterate them**, do not
  index with `field_size()` + `field(i)`. `field_size()` is still fine for `reserve()` and
  `== 0` emptiness checks; compare two repeated fields with `absl::c_equal`.
- **Edition 2024**: string accessors default to `string_type = VIEW` and return
  `std::string_view` (so the by-value-`string_view` rule applies to them); singular fields
  have **explicit presence** by default, so `set_x(0)` serializes a `0`. To mean "absent",
  leave the field unset (never call its setter) rather than setting the default.
- **Build test protos from text, not setters**:
  `mbo::proto::ParseTextProtoOrDie(R"pb(field: 1 nested { k: "v" })pb")`
  (`@mboworks_proto//mbo/proto:parse_text_proto_cc`). The `R"pb(...)pb"` raw string is what
  clang-format leaves alone, so the proto stays readable. Do not imperatively `set_` / `add_`
  your way to a fixture.
- **Assert on protos structurally** with `mbo::proto::EqualsProto`
  (`@mboworks_proto//mbo/proto:matchers_cc`), which also accepts a text-proto string:
  `EXPECT_THAT(msg, EqualsProto(R"pb(field: 1)pb"));`. Match a subset with
  `Partially(EqualsProto(...))`. Never compare serialized strings.

## Testing (GoogleTest / GoogleMock)

All exported code must be tested, at every level (unit, integration, and end-to-end where
it applies). A one-shot manual check or a script you ran once is planning input, never a
substitute for a committed test. Tests use GoogleTest + GoogleMock with these conventions.

### Structure

- **Always `TEST_F` with a fixture**, never a bare `TEST`. Even an empty
  `struct FooTest : ::testing::Test {};` is preferred, so shared setup has a home.
- One behaviour per test; name the test for the behaviour it asserts.

### Bazel test sizing

- Every direct Bazel test rule declares `size` explicitly so scheduling and timeout
  expectations are reviewable. Project test macros may provide a documented default.
- Use `size = "small"` for quick unit, golden-file, CLI smoke, fuzz-regression, and
  digest-verification tests. Reserve `medium` or larger sizes for measured runtime needs;
  do not leave a fast test in Bazel's implicit `medium` class.
- `tools/check_test_sizes.py` enforces the direct-rule declaration mechanically.

### Assertions: gmock matchers, never comparison macros

- Assert with **`EXPECT_THAT` / `ASSERT_THAT` + a matcher**, never a GoogleTest comparison macro.
  The prohibited suffixes are `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `STREQ`, `STRNE`, `STRCASEEQ`,
  `STRCASENE`, `FLOAT_EQ`, `DOUBLE_EQ`, and `NEAR`, in both their `EXPECT_` and `ASSERT_` forms.
  Matchers compose and give far better failure messages. **There are no exceptions, including string,
  floating-point, and multi-line text comparisons.** The accepted exception is the boolean
  `EXPECT_TRUE` / `EXPECT_FALSE` (and `ASSERT_TRUE` / `ASSERT_FALSE`), which read fine on their own.
  Within a single test keep one style - do not mix, say, `EXPECT_TRUE(x)` and
  `EXPECT_THAT(y, IsTrue())`.
- **Name matchers unqualified - never the `::testing::` prefix inline.** Bring each matcher in with
  a `using ::testing::Foo;` (or `using ::mbo::testing::Foo;`) in the test file's anonymous namespace
  and use the bare name in the `EXPECT_THAT` / `ASSERT_THAT` expression; a `::testing::Foo(...)`
  written inline in an assertion is the smell to fix by adding the `using`. (Fixture utilities such
  as `::testing::Test` / `::testing::TempDir` are not matchers and keep their qualification.)
- **`Eq` is optional - a readability choice, not a rule.** `EXPECT_THAT(x, value)` auto-wraps a
  bare value in `Eq`, so both forms compile. The value of `EXPECT_THAT` is that the line reads as
  a sentence - `EXPECT_THAT(foo, Eq(25))` is "expect that foo equals 25" - so keep `Eq` where it
  makes the assertion read that way, and drop it where the value already carries the meaning
  (`EXPECT_THAT(name, "foo")`). Inside composite matchers prefer bare elements
  (`ElementsAre(1, 2, 3)`); `Optional` / `Pointee` are the exception and need the inner `Eq` (see
  below). Strings sometimes need `StrEq` (e.g. a `char*` subject, where bare `Eq` compares
  pointers). For booleans, `IsTrue()` / `IsFalse()` usually read better than a bare `true` /
  `false` or `Eq(true)`: `EXPECT_THAT(found, IsTrue())`.
- **Multi-line text: `mbo::testing::EqualsText`, not `EXPECT_EQ`.** For a multi-line string
  (a rendered table, generated `--help`, file contents) prefer
  `EXPECT_THAT(actual, EqualsText(golden))` (`//mbo/testing:matchers_cc`): it compares line by line
  with unified-diff output, so a mismatch points at the offending line instead of dumping the whole
  blob. Text comparison was the last plausible reason to use `EXPECT_EQ` / `ASSERT_EQ`; `EqualsText`
  removes that reason. For non-line-oriented strings use `EXPECT_THAT(actual, expected)` or `StrEq`
  when the subject is a C string.
  - **Write the golden as a `DropIndent`-filtered raw string, not concatenated `"...\n"` literals.**
    `clang-format` shoves adjacent string literals hard against the `EqualsText(` bracket (aligned to
    the open paren, often at column ~35), which is unreadable and drifts with the call length. Instead
    write the expected block as an indented raw string and strip the source indent with
    `WithDropIndent`, which `clang-format` leaves untouched:

    ```cpp
    using ::mbo::testing::EqualsText;
    using ::mbo::testing::WithDropIndent;
    EXPECT_THAT(RenderTable(Format::kAligned, header, rows), WithDropIndent(EqualsText(R"out(
        name    size
        ------  ----
        a.txt   3
        )out")));
    ```

    `mbo::strings::DropIndent` (which `WithDropIndent` applies to the **expected** text only) drops the
    empty first line after `R"out(`, strips the first content line's indent from every line, and clears
    a whitespace-only last line - so the block reads as the literal expected output. The subject stays
    as-is; only de-indent it too (`EXPECT_THAT(DropIndent(actual), WithDropIndent(EqualsText(golden)))`,
    `//mbo/strings:indent_cc`) when the actual is itself an indented literal. Use any raw delimiter
    (`R"out(`, `R"md(`); a raw string also needs no `\n` / `\\` escaping.

  - **Caveat - trailing whitespace.** A raw-string golden cannot carry significant _trailing_ spaces on
    a line: the `trim trailing whitespace` pre-commit hook strips them, silently changing the golden. If
    the expected output has meaningful trailing whitespace (e.g. right-padded columns), fall back to the
    concatenated-literal form for that case and accept the paren alignment.
  - `DropIndentAndSplit` returns the de-indented lines as a `std::vector<std::string_view>` for when you
    would rather match them with `ElementsAre`.
- **Floats / doubles**: never `Eq` / `==`; use `FloatEq` / `DoubleEq` (or `Near`).
- **Optionals**: `EXPECT_THAT(opt, Eq(std::nullopt))` for empty, `Optional(...)` for a
  value. Nested matchers do **not** auto-wrap: `Optional(Eq("x"))` and `Pointee(Eq("x"))`
  need the explicit inner `Eq` (a bare value fails to compile there).
- **Containers**: `ElementsAre(...)` / `UnorderedElementsAre(...)` / `SizeIs(n)` /
  `IsEmpty()` cover size + order + contents in one matcher, instead of an
  `ASSERT_EQ(v.size(), n)` followed by indexed `EXPECT_EQ`s. `ElementsAre` auto-wraps
  each bare element in `Eq`.
  - **For a longer matcher list, prefer `ElementsAreArray({m1, ..., mN,})` with a
    trailing comma over `ElementsAre(m1, ..., mN)`.** `ElementsAre` is variadic, so
    clang-format bin-packs it into an unreadable run; `ElementsAreArray` takes a
    braced init-list, so the manual trailing comma opts it into one-matcher-per-line
    (the same trailing-comma lever as any aggregate, see the Formatting section).
    A short list that reads on one line stays `ElementsAre`.
- **Size and emptiness: match the container, never extract then match.** Use
  `SizeIs` / `IsEmpty` on the container itself, not `.size()` / `.empty()` fed to a
  scalar matcher or `EXPECT_TRUE` - the matcher form prints the container on failure
  while the extracted form throws it away. So:
  - `EXPECT_THAT(v, SizeIs(3))`, not `EXPECT_THAT(v.size(), 3)` / `EXPECT_EQ(v.size(), 3)`.
  - `SizeIs` composes with a matcher for bounds: `EXPECT_THAT(v, SizeIs(Le(90)))`,
    not `EXPECT_THAT(v.size(), Le(90))`.
  - `EXPECT_THAT(v, IsEmpty())` / `EXPECT_THAT(v, Not(IsEmpty()))`, not
    `EXPECT_TRUE(v.empty())` / `EXPECT_FALSE(v.empty())` (this is the one place a
    `.empty()` boolean should still become a matcher).
- **Struct elements**: fold per-field checks into the element matcher with the
  **3-arg, named** `Field("member", &T::member, m)` + `AllOf`, ideally via a small
  `testing::Matcher<T> FooIs(...)` helper, so a mismatch names the field rather than
  reporting "whose given field is...".

  ```cpp
  testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
    return AllOf(Field("flag", &ResolvedFlag::flag, flag), Field("source", &ResolvedFlag::source, source));
  }
  EXPECT_THAT(Resolve(in), ElementsAre(FlagIs("--color", Source::kUser), FlagIs("--sort", Source::kUser)));
  ```

### Status matchers (`mbo/testing`)

- `EXPECT_THAT(status_or, mbo::testing::IsOk())`, or
  `StatusIs(absl::StatusCode::kInvalidArgument)` to match a specific code.
- **`IsOkAndHolds(m)`** matches an OK `StatusOr` whose value matches `m` - prefer it over
  `IsOk()` followed by dereferencing: `EXPECT_THAT(Parse(in), IsOkAndHolds(SizeIs(3)))`.
- Do not `ASSERT_THAT(value, IsOk())` and then dereference the same `StatusOr`; bind it once with
  `MBO_ASSERT_OK_AND_ASSIGN`, or use `IsOkAndHolds` when it is inspected only once.
- `MBO_ASSERT_OK_AND_ASSIGN(const auto value, MakeThing())` asserts OK and binds in one step.
- `MBO_ASSERT_OK_AND_MOVE_TO(MakePair(), auto [a, b])` is the move variant whose target may
  contain commas (so the expression comes first) - the test mirror of `MBO_MOVE_TO_OR_RETURN`,
  for structured bindings and move-only types.

### Protocol-buffer fixtures and matchers

Build proto test data with `mbo::proto::ParseTextProtoOrDie(R"pb(...)pb")` and assert with
`mbo::proto::EqualsProto` / `Partially(EqualsProto(...))` - never imperative setters or
serialized-string comparison. See the Protocol Buffers section.

### Shell / binary-level tests

- Use **mboworks/bashtest** (`bazel_dep(name = "mboworks_bashtest")`), not a hand-rolled
  `sh_test`: `load("@mboworks_bashtest//bashtest:bashtest.bzl", "bashtest")`, then a
  script that `source`s `"${mboworks_bashtest}"`, defines `test::name()` functions using
  the `expect_*` assertions, and ends with `test_runner`.
- **Assert on captured output with bashtest's content matchers (>= 0.5.0), never a
  hand-rolled `grep`.** `expect_output_contains` / `expect_output_not_contains` for a
  literal substring; `expect_matches` / `expect_not_matches` for an ERE. They take the
  pattern / substring **first** and the text **second**, and match via bash's built-in
  `[[ =~ ]]` (no subprocess), so - unlike `printf ... | grep -qE` - they cannot misfire
  on SIGPIPE under `set -o pipefail`. `expect_eq` / `expect_ne` stay for scalar checks
  (exit codes, counts); `expect_contains` / `expect_not_contains` are **array**-membership,
  not substring. Do not reintroduce a `_has`-style `grep -q` wrapper.
  - **Anchoring caveat:** `expect_matches` matches the **whole** text, so `^` / `$` anchor
    the whole output, not a line (unlike `grep`). For a per-line anchor use a real newline:
    `NL=$'\n'`, then `(^|${NL})X` for a line start and `X($|${NL})` for a line end (`\n` is
    not a portable ERE escape, so embed the newline via `${NL}`).
- **For a whole-output-per-case golden, prefer a golden-file test over scraping.** When a
  test wants to lock an entire rendered output (not just probe for substrings), commit one
  expected-output file per case and diff against it with `diff_test`
  ([`//mbo/diff:diff.bzl`](mbo/diff/diff.bzl)), which fails printing the offending diff.
  This is the output analogue of `EqualsText` for C++ and reads far better than a pile of
  `expect_output_contains` probes.
- It runs under macOS bash 3.2: **no `mapfile` / `readarray`** or other bash-4 features.
  Read lines with `while IFS= read -r line; do arr+=("$line"); done <<< "$out"`.
