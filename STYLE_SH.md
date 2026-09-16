# Shell style

Follow the [Google Shell Style Guide](https://google.github.io/styleguide/shellguide.html) unless
this document or repository tooling says otherwise.

## Tooling

- Use Bash. Executable scripts start with `#!/usr/bin/env bash`, then the licence header.
- Format with `shfmt -bn -ci -i=2 -w`; run `shellcheck` and explain necessary local suppressions.
- Use `set -euo pipefail`, quote expansions unless splitting is intentional, and make function
  variables `local`.

The formatter and linter versions are pinned in [`.pre-commit-config.yaml`](.pre-commit-config.yaml).

## Functions return values; they do not mutate callers

A function does not return a result by changing a caller variable, global counter, working
directory, shell option, or trap. Write the result to standard output and capture it:

```sh
make_tree() {
  local root
  root="$(test_tmpdir tree)"
  mkdir -p "${root}/src"
  echo "${root}"
}

root="$(make_tree)"
```

Do not use `printf -v`, `eval`, or global counters as hidden return channels. A function whose
purpose is an external effect may perform that named effect; keep it local and explicit. Send
diagnostics to standard error.

## Tests and temporary files

- Shell tests use `mboworks_bashtest` and its expectation helpers.
- Put test-owned files below `${BASHTEST_TMPDIR}`. Do not add cleanup traps or `rm -rf` calls.
- Allocate retained fixtures with `test_tmpdir name`; use `${TEST_TMPDIR}` only when a tool
  specifically requires Bazel's target-level directory.
- Use bashtest content expectations instead of hand-written `grep` assertions.

## Portability

- Target the Bash available on supported macOS and Linux CI runners.
- Resolve Bazel runfiles through `${TEST_SRCDIR}` and `${TEST_WORKSPACE}`.
- Prefer `[[ ... ]]`, arithmetic `(( ... ))`, and arrays over legacy tests and stringly argument
  assembly.
- Use lower-case names for local variables and functions. Preserve environment and runfile names.
- Avoid GNU-only flags and `mapfile` where scripts must run on stock macOS Bash.
- Read lines with `while IFS= read -r line`; use `jq`, `yq`, or Python for structured data.
- Check required programs early and report missing prerequisites clearly.
