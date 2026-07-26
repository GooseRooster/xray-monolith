---
name: upstream-merge-apply
description: >-
  Apply the accepted ("take") subset of the upstream-merge-triage ledger to
  the merge-upstream branch: draft an execution plan (commit order,
  anticipated conflicts, final commit structure) via Claude Code's plan
  mode, get it approved, then cherry-pick the accepted upstream commits and
  update the ledger. Use when the user says "apply the merge ledger",
  "apply accepted upstream commits", "implement the upstream merge", or
  asks to turn triaged upstream commits into a real merge commit for review.
argument-hint: "[--limit N]"
user-invocable: true
allowed-tools:
  - Read
  - Grep
  - Glob
  - Bash(git *)
  - Bash(python3 *)
  - Write
  - EnterPlanMode
  - ExitPlanMode
  - AskUserQuestion
---

# Upstream merge apply

Takes the `verdict: take` / `applied: false` subset of the shared ledger
(built by `upstream-merge-triage`) and actually lands it on `merge-upstream`
- but only after the plan is drafted and approved through Claude Code's
normal plan-mode flow, never unattended. This is the piece that does the
tedious cherry-pick work the triage skill deliberately doesn't do, while
keeping the same "propose, then confirm" discipline for the git-mutating
step itself.

**Scope**: this skill only ever writes to `merge-upstream`. It never
touches `all-in-one-vs2022-wpo` - landing the result there is a separate,
later, manual step.

**Optional prior step**: `upstream-merge-review` explains the pending
`take` batch in real depth (grouped by subsystem, with a gotcha checklist)
before this skill cherry-picks it. It's advisory, not required - the plan
step below just notes how many pending commits haven't been through it yet.

## Steps

1. **Query the pending set**:
   ```
   python3 .claude/skills/upstream-merge-apply/scripts/apply_helpers.py pending [--limit N]
   ```
   Returns `verdict: take, applied: false` ledger entries, oldest-first
   (cherry-pick order matches upstream's own history order to minimize
   spurious conflicts). If empty, report that and stop - there's nothing to
   apply until `upstream-merge-triage` produces more accepted commits.

2. **Draft the plan** (do not touch git state yet):
   ```
   python3 .claude/skills/upstream-merge-apply/scripts/apply_helpers.py plan [--limit N]
   ```
   This renders the ordered commit list with hot-zone entries (commits that
   were reviewed and still accepted despite touching a file we've
   customized - the most likely conflict points) called out separately.
   Use this output as the basis for the plan file - see
   `references/apply-protocol.md` for exactly what the plan-mode plan
   should contain and how execution proceeds after approval.

3. **Call `EnterPlanMode`** to switch into plan mode (this requires the
   user's explicit consent to even enter - that's the first confirmation
   gate). Write the plan (from step 2's output, plus the execution
   description from `references/apply-protocol.md`) to the plan file, then
   call `ExitPlanMode` to request approval - the second confirmation gate.

4. **On approval, execute** exactly as described in
   `references/apply-protocol.md`: cherry-pick each accepted commit
   individually onto `merge-upstream`, oldest first, stopping at the first
   conflict for manual resolution (never automatic `ours`/`theirs`).

5. **Update the ledger** for every commit actually landed:
   ```
   python3 .claude/skills/upstream-merge-apply/scripts/apply_helpers.py mark-applied <file-with-hashes>
   ```
   Then make the one small trailing "ledger sync" commit (touches only the
   ledger files) described in `references/apply-protocol.md`.

6. **Final report**: how many commits landed vs. were left pending, the
   resulting commit range on `merge-upstream`, and an explicit reminder
   that `all-in-one-vs2022-wpo` has not been touched.

## Additional resources

- `references/apply-protocol.md` - exact plan-file content expectations,
  conflict-handling rules, and the ledger-sync commit format.
