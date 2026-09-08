# Git and pull-request orchestration rules

These rules apply to every human or automated contributor performing branch, pull-request, CI, or
merge operations in this repository.

The objective is safe, autonomous, dependency-aware progress with the shortest practical critical
path. Required repository protections are constraints, never obstacles to bypass.

When these rules conflict with an ad hoc optimization, these rules win.

## Goal preservation

Before any state-changing action, verify that its immediate effect advances the stated goal. An
action that predictably moves the repository away from the goal is prohibited unless it is required
to resolve a concrete blocker and the user has explicitly authorized that tradeoff.

When an instruction has only two reasonable interpretations, execute it directly or ask for
clarification. Do not invent a third workflow that creates additional work or invalidates completed
validation.

## Pull-request readiness and merging

A pull request is **ready** when it is approved, mergeable, and all required checks have completed
successfully.

When the user says that a pull request is ready or instructs the agent to merge it:

- Merge it without changing its head, base, commits, or branch.
- Do not push, rebase, force-push, retarget, update, or merge another branch into it first.
- Do not restart, invalidate, duplicate, bypass, or otherwise replace successful required checks.

If ordering appears ambiguous, first derive it from the current pull-request dependency graph,
repository rules, and the goal. Ask only when more than one materially different safe outcome
remains and the intended outcome cannot be inferred.

Required checks are specific to the pull-request context recognized by repository rules. Never try
to transplant, reuse, spoof, or substitute checks from another pull request or commit, even when
the Git trees are identical.

## Synchronization before work and validation

Synchronization is a prerequisite, not a publishing step to defer until other work is complete.
The local worktree and the remote pull-request branch are separate states; proving that the local
`HEAD` contains the base does not prove that the pull request is current.

Before starting or resuming work on a pull request:

1. Fetch the current base branch and the pull request's remote head.
2. Read the pull request state from GitHub and determine whether GitHub considers it out of date.
3. Verify separately that the local branch contains the current base and that the remote
   pull-request head contains the current base.
4. If either branch is behind, synchronize locally and push that synchronization immediately.
5. Confirm from GitHub that the pull request is current before doing further implementation work or
   starting validation.

Do not postpone pushing an already-created synchronization commit while waiting for unrelated edits,
formatting, tests, or commits. A clean synchronization push is independently useful and must happen
first. Uncommitted work may remain in the worktree while the existing synchronization commit is
pushed; it is not a reason to leave the remote pull request stale.

Validation is authoritative only when all of the following are true:

- the tested tree contains the current base branch;
- the corresponding commit is pushed as the pull request's remote head;
- GitHub recognizes that head and does not report the branch as out of date;
- no later synchronization, rebase, merge, conflict resolution, or implementation change has
  changed the tested tree.

Local tests on an unpushed or out-of-date pull-request head may be useful while developing, but they
must never be reported as final validation, used to delay synchronization, or counted toward
readiness. If the base advances before completion, stop treating earlier results as authoritative,
synchronize and push first, then rerun the affected validation on the new tree.

Before mutating a pull request that is already ready, verify that the action cannot invalidate its
approval or required checks. If it can, the mutation is prohibited unless it is needed to fix a
demonstrated failure or conflict and the user explicitly authorizes that consequence.

## Stacked pull requests

Never merge a lone child or follow-up pull request into its ready base pull request.

When a ready base has exactly one child, merge the base immediately. Do not wait for the child's
current CI run; merging the child into the base is prohibited. After GitHub retargets the child to
the primary branch, treat the resulting CI run as authoritative and merge the child when it becomes
ready.

Testing the lone child before the base merges may provide early feedback, but it does not replace
the required post-retarget validation and must not delay the ready base.

A base with multiple independent children in the same repository may instead be used as an
integration branch:

1. Test the base and every independent child separately and in parallel.
2. Require every child selected for integration to be approved, mergeable, and fully green.
3. Merge all selected children into the base.
4. Run the complete required CI suite on the resulting combined base.
5. Merge the combined base into the primary branch only when that integration run is fully green.

Use this fan-in procedure only when there are multiple genuinely independent children. It provides
parallel fault isolation while the final base run verifies interactions between their combined
changes.

Do not classify children as independent when one introduces behavior, APIs, files, configuration,
or assumptions consumed by another. Do not use the fan-in procedure for a single child, because it
adds another CI cycle without providing parallel integration value.

## Large pull-request graph optimization

Count in-scope pull requests separately for each repository. When more than four are open in one
repository, treat that repository's merge orchestration as a graph-planning problem. Build and
validate its complete graph before performing any branch mutation or merge.

A rollout across repositories does not combine their pull-request counts or create a shared
integration branch. For example, one pull request in each of seven repositories remains seven
separate one-pull-request cases. Track real cross-repository dependencies when they exist, but
apply the delegation threshold and integration procedures within each repository.

GitHub base-branch relationships are only one source of dependency information. Inspect the actual
changes and represent at least these relationships:

- **hard dependency**: one pull request consumes an API, behavior, file, target, configuration, or
  assumption introduced by another;
- **ancestry dependency**: a pull request is based on another pull request's branch;
- **content overlap**: pull requests modify the same files or logically coupled code;
- **conflict dependency**: merging one pull request is expected to create or remove a conflict in
  another;
- **validation dependency**: independently valid changes may interact and require a combined CI
  run;
- **workflow dependency**: one pull request changes CI, build, coverage, formatting, enforcement,
  or toolchains applied to another;
- **independence**: neither pull request consumes, overlaps, changes validation for, or otherwise
  affects the other.

Do not infer independence merely because pull requests have different base branches or touch
different files. Shared headers, templates, generated files, repository-wide configuration, build
rules, and workflows can create dependencies without direct textual overlap.

Before acting, partition the graph into:

- ready roots that can merge directly;
- linear dependency chains;
- fan-out groups with multiple independent children;
- mutually interacting groups requiring integration validation;
- conflicted or failing nodes;
- independent components that can progress in parallel.

Choose a merge schedule that satisfies all hard dependencies while optimizing, in order:

1. Preserve correctness and required repository protections.
2. Never invalidate a ready pull request unnecessarily.
3. Minimize the number of required CI reruns.
4. Minimize the critical path to merging the complete graph.
5. Maximize safe parallel validation.
6. Minimize repeated conflict resolution and branch rewriting.
7. Preserve independently reviewable changes when doing so does not materially lengthen the
   critical path.

Model each head-changing action as invalidating that pull request's existing readiness and requiring
a new validation cost. A plan that changes a ready head is therefore more expensive than one that
merges it unchanged. Do not choose a higher-cost plan without a concrete correctness or
critical-path benefit.

For each candidate schedule, reason about:

- which checks remain reusable in their original required context;
- which actions trigger new CI;
- which jobs can run concurrently;
- which merge causes automatic retargeting;
- which conflicts disappear after an ancestor merges;
- which combinations require final integration testing;
- which ready pull requests would become unready;
- the expected number and placement of full CI cycles.

Prefer the schedule with the shortest safe critical path, not merely the smallest number of commands
or branches.

Record the selected schedule and its dependency rationale in the orchestration ledger. Recompute the
schedule after every merge, failed check, new commit, retargeting event, review change, or newly
opened pull request. The plan is a cache of current reasoning, not a fixed sequence to execute after
its assumptions change.

If graph relationships cannot be established confidently, perform additional read-only inspection.
Ask only when a semantic or product dependency cannot be determined from the code, repository
history, pull-request descriptions, tests, or existing instructions.

## Autonomous merge orchestration

For four or fewer in-scope open pull requests in one repository, apply the deterministic rules
directly. For more than four in that repository, first solve and record the dependency-aware merge
schedule described above, then execute it while recomputing after every state transition.

Merge orchestration must remain fully autonomous, including for large pull-request graphs. Do not
pause merely because a pull request is waiting for CI, becomes conflicted, is retargeted, or fails
validation. Continue making safe progress on every other unblocked pull request.

Maintain a current dependency graph for all in-scope pull requests. For each pull request, record:

- pull-request number;
- head and base branches;
- current head SHA;
- parent and child pull requests;
- whether each child is independent of its siblings;
- approval and mergeability state;
- required-check state, including the final aggregate check;
- whether auto-merge is enabled;
- the concrete blocker, if any.

Treat GitHub as the source of truth for pull-request state. Refresh the graph after every merge,
push, rebase, retargeting event, conflict resolution, or completed CI run. Never act from a stale
snapshot.

Apply these rules repeatedly until every in-scope pull request is merged or has a concrete blocker
that cannot be resolved without information or authority not already provided:

1. Merge ready pull requests whose merge cannot invalidate another ready pull request.
2. Merge a ready base with exactly one child immediately; do not wait for the child's obsolete
   pre-retarget CI.
3. After a base merges, refresh GitHub state and allow dependent pull requests to retarget before
   acting on them.
4. Treat post-retarget required checks as authoritative.
5. For a base with multiple genuinely independent children, validate the children in parallel,
   merge all ready selected children into the base, and require a complete integration run on the
   combined base.
6. Resolve conflicts only on pull requests that are not ready. Never alter a ready pull request to
   resolve another pull request's conflict.
7. When one pull request is waiting or blocked, continue with every independent mergeable pull
   request.
8. Enable auto-merge only when the pull request has the correct base and head, is approved,
   mergeable, and all currently required checks are successful.
9. After every merge into the primary branch, inspect the resulting primary-branch CI and repair
   any regression before treating dependent work as complete.
10. Continue until the dependency graph contains no actionable pull request.

A pull request being slow, queued, conflicted, or temporarily red is not a reason to stop processing
the rest of the graph.

## Deterministic conflict handling

For each conflicted pull request:

1. Confirm that it is not already ready.
2. Refresh its base and identify the exact conflicting commits and files.
3. Resolve the conflict on that pull request's own branch.
4. Preserve both changes when they are compatible.
5. Run validation appropriate to the combined changes.
6. Push the resolution, update the pull-request description, and monitor its replacement CI.
7. Recompute dependencies because the new head invalidates all earlier readiness information.

If several pull requests have the same conflict, determine the shared cause before editing any of
them. Prefer one structural resolution that reduces total repeated work. Do not mechanically resolve
the same conflict across many branches when merging or reorganizing the dependency graph can remove
that work safely.

## Failure recovery

Recovery must preserve autonomous progress. Do not improvise shortcuts, reuse checks from another
context, or pause solely to ask what to do when the safe recovery follows from repository state.

If an action accidentally invalidates a ready pull request:

1. Stop making further mutations to that pull request.
2. Refresh its actual head, base, approvals, required checks, and dependency relationships.
3. Cancel only runs that are provably obsolete because their head SHA is no longer reachable from
   the pull request.
4. Never transplant, spoof, substitute, or rely on checks belonging to another pull-request
   context, even when the Git trees are identical.
5. Select the recovery that requires the fewest additional branch mutations and required CI runs
   while preserving repository rules.
6. Allow unavoidable required checks to complete, monitor them, fix genuine failures, and resume
   merging automatically when the pull request becomes ready.
7. Continue processing every independent unblocked pull request while recovery checks run.
8. Record the incident and recovery state in the local orchestration ledger so work survives context
   loss or interruption.

Do not force-push, retarget, merge another branch into the affected branch, or rewrite its history as
a speculative attempt to recover prior check status. Such actions require a demonstrated
repository-state reason and must strictly reduce, not increase, the remaining work.

Ask only when progress requires information, authority, or a product decision that cannot be derived
from the repository, pull-request graph, existing instructions, or prior authorization. Waiting for
CI, resolving an ordinary conflict, fixing a test failure, retargeting after a merge, and continuing
with independent work are not reasons to ask or stop.

## Orchestration ledger

Keep a local, ignored orchestration ledger as a resumable cache, not as a competing source of truth.
It must contain:

- the current objective and scope;
- the last GitHub refresh time;
- the pull-request dependency graph;
- the last observed head SHA and state of every pull request;
- active CI runs;
- completed merges;
- current blockers and the next deterministic action.

Refresh the ledger from GitHub before acting after any interruption. Never assume that its cached
state is still current.
