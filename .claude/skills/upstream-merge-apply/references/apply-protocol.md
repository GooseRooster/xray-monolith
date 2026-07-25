# Apply protocol

## Plan-mode integration

`EnterPlanMode` takes no arguments and requires the user's explicit consent
to enter - that consent is the first safety gate, independent of anything
this skill does. Once in plan mode, write a plan file containing:

1. The ordered cherry-pick list from `apply_helpers.py plan` output
   (commit hashes + subjects, oldest first).
2. The hot-zone call-outs (which accepted commits touch a file we've
   customized, and why they were accepted anyway per the ledger's
   `rationale` field) - these are the commits most likely to conflict, and
   the plan should say so explicitly.
3. The intended final state: each commit cherry-picked individually
   (original message + authorship preserved, no squashing - keeps
   `git blame`/bisect meaningful and keeps the review honest about what
   actually changed), followed by one small trailing "ledger sync" commit.
4. What happens on conflict (see below) and what happens if the user wants
   to stop partway through.

Call `ExitPlanMode` to submit this for approval - the second safety gate.
Only after approval does step 4 below run.

If `EnterPlanMode`/`ExitPlanMode` are ever unavailable in a given context,
fall back to: render the same plan content directly in the chat reply and
get a single explicit go-ahead via `AskUserQuestion` before proceeding. Note
if this fallback was used in the final report.

## Execution (only after plan approval)

Working directly on `merge-upstream` (never on `all-in-one-vs2022-wpo`):

```
git cherry-pick <hash>
```

for each accepted commit, oldest first.

- **On success**: move to the next commit.
- **On the first conflict**: stop immediately. Do not attempt automatic
  `ours`/`theirs` resolution or any other automatic heuristic. Read the
  conflicting hunks, consult the relevant `CLAUDE.md` divergence section
  for design intent (this is exactly the hot-zone case flagged in the
  plan), propose a specific resolution, and get explicit confirmation
  before running `git cherry-pick --continue`. This reuses the same
  judgment-with-confirmation pattern as triage, just applied to conflict
  hunks instead of whole commits.
- **If the user says stop/pause mid-sequence**: leave `merge-upstream` at
  whatever the last successfully-completed cherry-pick was (a valid,
  buildable state), never partway inside a conflicted pick. Run
  `git cherry-pick --abort` first if a conflict is currently open and the
  user wants to stop there.

## Ledger sync commit

After `mark-applied` updates the ledger's `applied`/`applied_date` fields
for the batch that landed, commit just the ledger files:

```
git add .claude/upstream-merge/ledger/
git commit -m "upstream-merge: mark N commits applied through <date>"
```

This keeps the "which commits are actually on this branch" bookkeeping
change clearly separated from the real code changes in the surrounding
history - a reviewer looking at the commit series can tell at a glance
which commits are upstream cherry-picks and which one is pure ledger
bookkeeping.

## What "for review" means

The end state of a run is a reviewable commit series sitting on
`merge-upstream` - built, buildable, and ready for the owner to inspect,
test, or open a PR from. This skill does **not** merge into
`all-in-one-vs2022-wpo`; that remains a deliberate, separate, manual step,
consistent with `CLAUDE.md`'s existing merge-workflow guidance.
