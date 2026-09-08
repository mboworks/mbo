# Project TODO

This file is the single source of truth for repository fixes and improvements.
Each change is implemented and reviewed in its own pull request. An item is
checked in the pull request that completes it, so merging that pull request
also updates this file to the completed state.

## Correctness

- [x] Make `Required<T>` replacement and generic operations correct.
  - Construct replacement values before destroying the current value so a
    throwing user constructor leaves the wrapper unchanged.
  - Require non-throwing relocation for replacement without assignment or
    nullable storage.
  - Fix hashing and heterogeneous wrapper comparison when instantiated.
  - Derive comparison exception specifications from the wrapped operation.
  - PR: [#362](https://github.com/mboworks/mbo/pull/362).
- [x] Make `NoDestruct<T>` construction and customizations correct.
  - Derive constructor exception specifications from construction of `T`.
  - Hash and stringify the stored value rather than the backing union.
  - Remove the redundant self-reference and its pointer-sized overhead.
  - Instantiate customizations and throwing construction in tests.
  - PR: [#363](https://github.com/mboworks/mbo/pull/363).
- [x] Let allocating `Json` operations propagate failures.
  - Remove unconditional `noexcept` from generic and string construction.
  - Remove unconditional `noexcept` from object property access that may
    allocate or invoke the configured throwing requirement handler.
  - Exercise a throwing consumer string conversion in an exception-enabled
    compatibility test.
  - Keep runtime struct metadata initialization non-throwing by using guarded
    indexing instead of a redundant throwing bounds check.
  - PR: [#364](https://github.com/mboworks/mbo/pull/364).
- [x] Make mutable `Json::at(property)` lookup-only.
  - Preserve insertion semantics in `operator[]`.
  - Require `at()` properties to exist for both mutable and const objects.
  - Verify missing lookup fails without changing the object.
  - PR: [#365](https://github.com/mboworks/mbo/pull/365).
- [x] Make `OptionalDataOrRef` lifetime and exception handling safe.
  - Preserve its null, owned-data, and borrowed-reference states with automatic
    lifetime management.
  - Handle self-move and return `*this` from null assignment.
  - Use operation-dependent exception specifications and leave a valid empty
    state after failed replacement construction.
  - Cover all state transitions, aliasing, and throwing construction.
  - Document mbo's exception-free builds and exception-enabled public-header
    compatibility contract.
  - PR: [#359](https://github.com/mboworks/mbo/pull/359).
- [x] Make single- and double-quote parsing options independent.
  - Honor each quote-enable option without coupling it to the other.
  - Treat disabled quote characters as ordinary unquoted input.
  - Cover all four option combinations with separator-sensitive assertions.
  - PR: [#349](https://github.com/mboworks/mbo/pull/349).
- [x] Correct hexadecimal escape parsing for alphabetic digits.
  - Map lowercase and uppercase `A`-`F` to values 10-15.
  - Cover mixed-case, braced, byte-boundary, and overflow inputs.
  - PR: [#347](https://github.com/mboworks/mbo/pull/347).
- [x] Fix undefined behavior in `mbo/json/json.h` iterator assignment.
  - Replace placement construction over live iterator objects with ordinary
    variant assignment and default same-type special members where possible.
  - Cover copy, move, mutable-to-const, self, array, and object assignments.
  - PR: [#326](https://github.com/mboworks/mbo/pull/326).
- [x] Reject truncated glob ranges without accessing an empty pattern.
  - Cover `[`, `[-`, `[a-`, negative ranges, and trailing escapes.
  - PR: [#327](https://github.com/mboworks/mbo/pull/327).
- [x] Fix main-branch cache cleanup prefix matching.
  - Pass the prefix to `jq` without relying on expansion inside single quotes.
  - Add a regression test with representative cache keys.
  - PR: [#328](https://github.com/mboworks/mbo/pull/328).

## API additions

- [x] Add `mbo::StringOrView`, a read-only owning-or-borrowing string wrapper.
  - Add `mbo/types/string_or_view.h` with default-empty, owning `std::string`,
    borrowing `std::string_view`, and borrowing string-literal construction.
    Avoid ambiguous or accidentally unsafe runtime `const char*` construction.
  - Preserve ownership and represented text across copy and move construction
    and assignment. Owning copies must be independent; borrowed copies must
    retain the same view; moves must be nothrow where promised.
  - Expose only `view()` and `owns_string()` as the core read-only API, and
    explicitly document that borrowed storage must outlive the wrapper.
  - Add comparison against `StringOrView`, `std::string_view`, `std::string`,
    and literals, operating solely on `view()`, plus `AbslStringify` support
    consistent with comparable mbo value wrappers.
  - Add a conventionally named Bazel `cc_library` and same-package test target,
    exporting the short name as `mbo::StringOrView` even though the header is
    under `mbo/types/`.
  - Cover default and distinctly owned/borrowed empty values, lvalue copies,
    rvalue moves, pointer preservation for borrowed copies, independent owned
    copies, copy/move assignment for both representations, embedded NUL bytes,
    comparisons, stringification, compile-time empty/non-empty literals, and
    promised type traits.
  - Keep the representation suitable for a future shared immutable ownership
    option if measurements justify it, but do not implement copy-on-write now.
  - Motivation: let downstream xff field rendering return computed owned text
    or stable registry/context/database views without forcing allocation. A
    later xff dependency-update PR can replace its local `FieldValue` after an
    mbo release contains this type.
  - PR: [#377](https://github.com/mboworks/mbo/pull/377).
- [x] Complete the read-only string interface and ecosystem integration for
      `mbo::StringOrView`.
  - Provide the full non-mutating `std::string_view`-style surface, including
    element access, iterators, size/capacity queries, copying, substrings,
    comparisons, search, and prefix/suffix/containment queries.
  - Preserve the ownership model: operations must not expose mutable storage or
    make a borrowed value appear owned, and returned views retain the documented
    lifetime constraints.
  - Integrate with Abseil formatting and hashing through `AbslStringify` and
    `AbslHashValue`.
  - Integrate with standard-library text output, formatting, and hashing where
    the supported C++ versions provide the necessary customization points.
  - Test parity against `std::string_view`, embedded NUL handling, heterogeneous
    formatting and hashing, constexpr use, and owned versus borrowed values.
  - PR: [#378](https://github.com/mboworks/mbo/pull/378).
- [ ] Provide compiler-independent aggregate field names for `Stringify`.
  - Preserve the existing `MboTypesStringifyFieldNames` extension point while
    making automatic field-name discovery work with supported GCC builds as
    well as Clang.
  - Evaluate GCC-specific facilities and well-defined compile-time extraction
    techniques; switching this feature to a newer language mode is acceptable, but the
    selected language mode alone must not be mistaken for a standard reflection
    facility.
  - Define explicit behavior for unsupported compilers and aggregates whose
    names cannot be discovered; never silently produce different output solely
    because CI selected another supported compiler.
  - Verify identical C++, JSON, nested-aggregate, and explicit-name-override
    output under the supported Clang and GCC configurations.

## File API robustness and portability

- [x] Add strict INI parsing with line-numbered diagnostics.
- Make file reads reject malformed group headers, missing separators, empty
  keys, and duplicate keys.
- Preserve the historical behavior behind an explicit permissive API and a
  compatibility `Parse()` spelling.
- Define full-line comments and retain comment characters within values.
- PR: [#370](https://github.com/mboworks/mbo/pull/370).

- [x] Provide the canonical fast, locale-independent, RE2-native GLOB and
      SHGLOB implementation for mbo and xff.
  - Implement the complete contract and acceptance criteria in
    [`mbo/file/README.md`](mbo/file/README.md) in this single PR.
  - Keep component wildcards and bracket expressions within one path component,
    and make complete-component `**` match zero or more directory levels.
  - Expose the translator publicly so xff can replace its parallel parser in one
    follow-up dependency-update PR without losing GLOB, SHGLOB, or gitignore
    behavior.
  - Prove the adoption path with a local xff override and its complete test suite.
  - PR: [#371](https://github.com/mboworks/mbo/pull/371).

- [x] Make `GetContents` handle failed seeks and non-seekable inputs safely.
  - Check `seekg`/`tellg` before converting the size.
  - Read in binary mode so byte counts remain consistent across platforms.
  - Add failure-path tests where practical.
  - PR: [#329](https://github.com/mboworks/mbo/pull/329).
- [x] Make `GetMaxLines` distinguish EOF from an I/O failure.
  - Return an error when the underlying read fails.
  - Add regression coverage.
  - PR: [#330](https://github.com/mboworks/mbo/pull/330).
- [x] Make `NormalizePath` portable across native path character types.
  - Avoid constructing `std::string_view` directly from `path.c_str()`.
  - Add or document Windows-oriented behavior and tests.
  - PR: [#331](https://github.com/mboworks/mbo/pull/331).

## Build, CI, and release engineering

- [x] Preserve review and branch protection for release version bumps.
  - Never approve the script's own pull request or use an admin merge.
  - Enable ordinary auto-merge and leave maintainer approval explicit.
  - Protect the exact `gh` command sequence with a fake-client integration
    test.
  - PR: [#367](https://github.com/mboworks/mbo/pull/367).
- [x] Publish only the requested release tag.
  - Detect an existing version with an exact ref lookup rather than substring
    matching.
  - Push only the newly created tag instead of every local tag.
  - Cover exact lookup and selective publication with temporary repositories.
  - PR: [#366](https://github.com/mboworks/mbo/pull/366).
- [x] Cover header-only and compilation-rule changes with clang-tidy.
  - Keep changed translation-unit runs focused.
  - Promote project header, generated-header template, and `.bzl` changes to a
    full first-party translation-unit sweep.
  - Derive that sweep from the compilation database and test scope selection.
  - Describe the local and CI checks consistently as enforcing gates.
  - PR: [#368](https://github.com/mboworks/mbo/pull/368).
- [x] Make Bazel test scheduling classes explicit and proportional.
  - Mark quick unit, CLI, golden-file, fuzz-regression, and digest-verification
    tests `small` while retaining `medium` for the measured long-running hash suite.
  - Give project test macros a documented `small` default.
  - Enforce explicit sizing on direct test rules in pre-commit.
  - PR: [#376](https://github.com/mboworks/mbo/pull/376).
- [x] Avoid an unnecessary Bazel configuration switch while generating the
      clang-tidy compilation database.
  - Build the extractor with the same `clang-tidy` configuration used by its
    internal analysis query and the preceding CI build.
  - Retain the extractor's intentional feature-only transition, which disables
    parameter files, layering checks, and header-parsing actions for `aquery`.
  - PR: [#354](https://github.com/mboworks/mbo/pull/354).
- [x] Update the compile-command extractor to deduplicate exec actions before source probing.
  - Avoid misleading missing-generated-source warnings on cold-cache clang-tidy runs.
  - Preserve exec-only generated sources while preferring equivalent target-configuration actions.
  - PR: [#352](https://github.com/mboworks/mbo/pull/352).
- [x] Reduce external dependency warning noise in default builds.
  - Treat external headers consistently as system headers or apply narrowly
    scoped external-warning suppression.
  - Verify project warnings remain errors.
  - PR: [#332](https://github.com/mboworks/mbo/pull/332).
- [x] Document the repository's GitHub Actions versioning policy.
  - Prefer readable version references and require a documented reason for any
    commit-SHA pin.
  - PR: [#333](https://github.com/mboworks/mbo/pull/333).
- [x] Cancel validation runs superseded by a newer commit.
  - Use one concurrency group per pull request or ref.
  - Preserve every `main` and release run as a durable integration record.
  - PR: [#374](https://github.com/mboworks/mbo/pull/374).
- [x] Validate changes in pull-request context, including contributions from forks.
  - Run branch validation for pull requests and pushes to `main`, avoiding a
    duplicate full matrix for same-repository pull requests.
  - Preserve stable pull-request coverage destinations without relying on the
    synthetic merge commit's API association.
  - PR: [#372](https://github.com/mboworks/mbo/pull/372).
- [x] Give every GitHub Actions workflow only the token permissions it needs.
  - Replace blanket `read-all` grants with explicit read scopes.
  - Default the privileged coverage publisher to no access and retain its
    narrow job-level publishing permissions.
  - PR: [#373](https://github.com/mboworks/mbo/pull/373).
- [x] Replace `mktemp -u` in release preparation with a safely created
      temporary resource and cleanup trap.
  - Exercise the release archive preparation path locally.
  - PR: [#334](https://github.com/mboworks/mbo/pull/334).

## Documentation and quality coverage

- [x] Raise line-coverage policy to a 90% minimum and 95% high target.
  - Enforce the high rating for lines while retaining medium enforcement for
    functions and branches.
  - Exercise the remaining `mbo/types` branch alternatives so the module meets
    the 82% branch target without exclusions.
  - PR: [#382](https://github.com/mboworks/mbo/pull/382).
- [x] Keep every persisted GitHub Actions cache below 500 MiB.
  - Stop persisting Bazel's repository cache between jobs; retain it only for
    reuse within a job.
  - Measure each remaining Bazel disk cache before saving and skip the upload
    when its size is at or above the limit.
  - PR: [#382](https://github.com/mboworks/mbo/pull/382).
- [x] Raise `mbo/types` branch coverage to at least 86%.
  - Exercise meaningful branch alternatives directly without production-code changes or coverage
    exclusions.
  - PR: [#383](https://github.com/mboworks/mbo/pull/383).

- [x] Enforce the measured coverage baseline in CI.
  - Reject per-category line, function, or branch regressions exceeding the
    policy-defined tolerance while retaining the absolute and changed-code gates.
  - Apply changed-code thresholds only when the patch contains coverable data
    for that metric; do not fail or label an absent metric as `NO DATA`.
  - Record the measurement scope in the baseline so policy scope changes require
    an explicit, reviewed regeneration.
  - Keep baseline updates as a deliberate `--write-baseline` operation.
  - PR: [#369](https://github.com/mboworks/mbo/pull/369).

- [x] Enforce spelling in C and C++ sources as well as Markdown.
  - Use pre-commit file-type classification rather than a duplicated extension
    expression.
  - Correct existing public-comment findings instead of adding broad ignores.
  - PR: [#375](https://github.com/mboworks/mbo/pull/375).
- [x] Unify coverage ratings, enforcement, and presentation, adopting the
      applicable final state of
      [xff PR #639](https://github.com/mboworks/xff/pull/639),
      [xff PR #641](https://github.com/mboworks/xff/pull/641), and
      [xff PR #642](https://github.com/mboworks/xff/pull/642).
  - Derive the low, medium, and high presentation bands and the independently
    selected enforced rating from one validated policy model.
  - Preserve justified per-category exceptions and expose every effective
    policy, reason, and changed-line policy in the published JSON.
  - Apply metric-specific colours to the detailed LCOV report and render its
    global policy matrix below the report details.
  - Render the compact report as a dense, colour-backed measurement and policy
    matrix with explicit `GOOD`, `OK`, and `BAD` states and JSON links.
  - PR: [#361](https://github.com/mboworks/mbo/pull/361).
- [x] Make `coverage_policy.json` the single source of truth for enforcement
      and LCOV presentation, adopting the applicable final state of
      [xff PR #631](https://github.com/mboworks/xff/pull/631),
      [xff PR #633](https://github.com/mboworks/xff/pull/633), and the
      coverage-related changes from
      [xff PR #634](https://github.com/mboworks/xff/pull/634) and
      [xff PR #636](https://github.com/mboworks/xff/pull/636).
  - Define explicit per-metric enforcement minima and health targets, and
    generate `genhtml`'s medium and high limits from them. Equal minimum and
    target values must form one pass/fail boundary without a yellow band.
  - Pin a verified modern LCOV release that honors metric-specific thresholds,
    accepting only the documented GCC/LLVM producer inconsistencies.
  - Replace the generic `genhtml` legend with a policy-derived, metric-specific
    legend below the summary table. Add depth-independent navigation from every
    LCOV page to the report overview and retained-report index. Keep the compact
    report enforcement-only: values at or above the minimum are `OK`, and values
    below it fail.
  - Use the generated configuration throughout the reusable main, PR, and
    release coverage path, with validation and unit tests for generation, HTML
    augmentation, status rendering, and every metric.
  - Keep category floors and health targets inherited and composable so an
    exception names only the metrics it changes.
  - PR: [#360](https://github.com/mboworks/mbo/pull/360).
- [x] Exercise the hash-internal runtime utilities directly ([PR #356](https://github.com/mboworks/mbo/pull/356)).
  - Cover every supported length for `LoadTail` and `LoadSmall` with
    volatile-derived runtime inputs.
  - Verify both lanes and the folded result of the 128-bit multiplication helpers.
- [x] Adopt the shared MBO Works contributor and agent guidance, C++ and shell
      styles, and machine-enforceable style checks.
  - Adapt the final xff policy set through PR #612 to mbo's human-led workflow.
  - Keep project-specific conventions and omit xff-specific CLI rules.
  - PR: [#350](https://github.com/mboworks/mbo/pull/350).
- [x] Publish durable, indexed coverage reports for main, pull requests, and releases.
  - Group the detailed LCOV tree by the same module categories as the policy report.
  - Retain source/run metadata and prevent stale workflow completions from replacing newer reports.
  - Provide a browsable index with report source, completion time, commit, workflow run, and metrics.
  - PR: [#351](https://github.com/mboworks/mbo/pull/351).
- [x] Correct existing README and contributing-guide spelling errors and add a
      lightweight documentation spell-checking hook.
  - PR: [#335](https://github.com/mboworks/mbo/pull/335).
- [x] Add fuzz targets for glob conversion, string parsing, INI parsing, diff
      inputs/options, and digest checksum-file parsing.
  - [x] Glob conversion. PR: [#336](https://github.com/mboworks/mbo/pull/336).
  - [x] String parsing. PR: [#337](https://github.com/mboworks/mbo/pull/337).
  - [x] INI parsing. PR: [#338](https://github.com/mboworks/mbo/pull/338).
  - [x] Diff inputs/options. PR: [#339](https://github.com/mboworks/mbo/pull/339).
  - [x] Digest checksum-file parsing. PR: [#340](https://github.com/mboworks/mbo/pull/340).
- [x] Add explicit UBSan coverage alongside ASan in CI.
  - PR: [#341](https://github.com/mboworks/mbo/pull/341).
- [x] Add coverage reporting focused on uninstantiated templates and malformed
      input branches.
  - PR: [#343](https://github.com/mboworks/mbo/pull/343).
- [x] Make `mbo/testing` meet the general function-coverage minimum.
  - Count the behaviorally covered `CapacityIs` description bodies once rather
    than once for every compiler-generated container specialization.
  - Remove the category-specific function-coverage override.
  - PR: [#353](https://github.com/mboworks/mbo/pull/353).
- [x] Make `mbo/types` meet the general branch-coverage minimum.
  - Exclude compiler-expanded branch pairs for the behaviorally covered
    `tstring` search fold expressions.
  - Remove the category-specific branch-coverage override.
  - PR: [#355](https://github.com/mboworks/mbo/pull/355).
- [x] Make `mbo/hash` meet the general branch-coverage minimum.
  - Directly test the hash-internal runtime utilities before excluding GCC's
    duplicated records for compile-time and non-native-endian alternatives.
  - Remove the category-specific branch-coverage override.
  - PR: [#357](https://github.com/mboworks/mbo/pull/357).
- [x] Make `mbo/log` meet the general function-coverage minimum.
  - Count behaviorally identical template and generic-lambda specializations
    once per source definition.
  - Exclude only quick-exit functions whose process cannot flush coverage data.
  - Remove the category-specific function-coverage override.
  - PR: [#358](https://github.com/mboworks/mbo/pull/358).
