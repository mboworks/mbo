# Coverage policy

[`coverage_policy.json`](../coverage_policy.json) is the single source of truth for coverage scope,
ratings, and CI enforcement. The short report, retained report page, generated LCOV colours and
legend, and the coverage job all consume it. A presentation-only threshold is not permitted.

## Ratings and enforcement

Every metric has two boundaries:

| Rating | Range                                  |
| ------ | -------------------------------------- |
| low    | Below `minimum`                        |
| medium | At least `minimum`, but below `target` |
| high   | At least `target`                      |

The `enforce` value is either `medium` or `high`. CI fails when a measurement is below its enforced
rating. Thus medium enforcement accepts medium and high; high enforcement accepts only high. The
overview reports `GOOD` when every metric is high, `OK` when enforcement passes but at least one
metric remains medium, and `BAD: L/F/B` when the named metrics do not meet their enforcement rating.
Percentage cells retain their low, medium, or high colour, so a medium cell explains either an `OK`
category or a failure when that metric enforces high. Only `BAD` fails CI.

Start a metric at medium enforcement while improving it toward its target. After it reliably reaches
high, changing `enforce` to `high` prevents a regression into medium. Do not collapse `minimum` and
`target` merely to strengthen enforcement; the two boundaries continue to describe the useful
three-band rating.

## Category overrides

The top-level policy applies to `overall` and is inherited by every overview category. A category
may override any subset of `minimum`, `target`, and `enforce`, independently per metric. This allows
both stricter enforcement for mature modules and lower initial limits for new or unusually
constrained modules. An override that lowers either boundary or relaxes enforcement must include a
non-empty `reason`; policy validation rejects an unexplained weakening.

Category policies affect the overview rating and the CI result. The detailed LCOV source report can
represent only one policy, so its colours and policy matrix deliberately use the global boundaries
and enforcement. Every overview rate cell includes a compact low/medium/high policy strip; its
outlined band is the enforced rating. Category exceptions therefore remain visible in the table as
well as its downloadable JSON data.

The `patch` object uses the same `minimum`, `target`, and `enforce` vocabulary for changed coverable
lines and branches. Functions are not attributed to changed lines and are therefore not patch-gated.

## Published data

Each retained report links its detailed LCOV source view, full `coverage-summary.json`, and workflow
`coverage-meta.json`. The coverage index also links the summary JSON directly. The summary records
measurements and the fully resolved minimum, target, and enforcement values for every overview row,
so consumers do not need to reimplement inheritance.
Coverage publication also refreshes PR lifecycle metadata on close and reopen, selects one
pre-merge or exact-merge-commit post-merge result per PR, and accepts a valid coverage job when
other CI jobs fail. A manual source-run input can replay a retained artifact through the same
serialized publisher without rerunning tests.
