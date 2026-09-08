# Agent and contributor rules - mbo

These rules apply to every contributor, human or automated. The repository is human-led: optimize
changes and explanations for human maintainers, and use automation to enforce mechanical rules.
[`STYLE_CPP.md`](STYLE_CPP.md) and [`STYLE_SH.md`](STYLE_SH.md) are canonical for their languages;
[`CONTRIBUTING.md`](CONTRIBUTING.md) describes the contribution flow.

## Git and pull-request operations

[`GIT_RULES.md`](GIT_RULES.md) is canonical for branch management, pull-request readiness, merge
ordering, dependency-graph planning, CI monitoring, and failure recovery.

Before performing any state-changing Git or GitHub operation, read `GIT_RULES.md` completely and
follow it. State-changing operations include commits, pushes, rebases, branch rewrites, retargeting,
merges, auto-merge changes, CI reruns, and CI cancellation.

Count in-scope pull requests separately for each repository. Do not add together pull requests
from different repositories when applying this threshold.

For four or fewer in-scope pull requests in one repository, the primary agent may apply
`GIT_RULES.md` directly.

For more than four in-scope pull requests in one repository, delegate that repository's merge
orchestration to one dedicated sub-agent.
That sub-agent must:

- read `GIT_RULES.md` completely before acting;
- inspect the complete pull-request graph;
- maintain the ignored orchestration ledger required by `GIT_RULES.md`;
- have exclusive ownership of state-changing Git and GitHub operations for the graph;
- continue autonomously until the graph reaches a terminal state;
- report blockers and completed merges to the primary agent.

While the merge-orchestration sub-agent is active, the primary agent and all other sub-agents must
not mutate branches, pull requests, CI runs, or merge state within its scope. They may perform
read-only analysis or work on explicitly disjoint tasks.

The primary agent remains responsible for ensuring that the orchestrator's scope is correct and
that no competing agent performs overlapping mutations.

Build and test with `bazel test //...`. Sanitizer configurations and the supported Bazel/compiler
matrix are exercised by CI. Run `pre-commit run --all-files` before proposing repository-wide
mechanical changes, or `pre-commit run --files FILE...` for a focused change.

## Pull request descriptions

Every pull request description has two layers, in this order:

1. a short, human-readable explanation of the outcome and why it matters;
2. a literal `## AG;DR` heading followed by the implementation details, reasoning, validation,
   portability notes, and known limitations needed by reviewers and future maintainers.

Update the description whenever a commit is added so the detailed section describes the complete
current change. Keep the human-readable section stable unless the outcome or motivation changes.

## Tests

- Use a `struct` fixture and `TEST_F`; do not add bare `TEST` cases.
- Use `EXPECT_THAT` / `ASSERT_THAT` and matchers. GoogleTest comparison macros are forbidden.
- Use `mbo::testing::EqualsText` for multi-line text and the mbo status matchers/macros for
  `absl::Status` and `absl::StatusOr`.
- Name typed and parameterized cases from their values or types; do not leave numbered suites.

See [`STYLE_CPP.md`](STYLE_CPP.md) for the complete rules and examples.

## Documentation

Keep GitHub-flavored Markdown tables vertically aligned: each `|` in a column lines up in the raw
file and delimiter alignment markers are preserved. Run
[`tools/align_markdown_tables.py`](tools/align_markdown_tables.py); pre-commit runs it for Markdown.

User-visible behavior, options, output, and build requirements are documented and tested in the
same change. Generated documentation must continue to derive from its existing source of truth.
