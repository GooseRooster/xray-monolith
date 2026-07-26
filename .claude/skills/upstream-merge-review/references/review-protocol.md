# Review protocol

## Why thematic, not chronological

`upstream-merge-triage` batches oldest-first because that mirrors
cherry-pick order. `upstream-merge-review` doesn't need to preserve order -
it needs to explain. Grouping by touched subsystem means a cluster of 8
related Steam Audio commits gets one coherent "here's what happened to the
reverb pipeline" narrative instead of 8 disconnected one-liners, and lets
you review a whole area of the codebase in one sitting instead of
context-switching between rendering and AI every other commit.

`scripts/review_helpers.py group` does this bucketing via a coarse
touched-file heuristic (see `THEME_RULES` in the script). It is
deliberately *not* the hot-zone detector - it's an organizational aid, and
never changes a verdict or gates anything. Themes come back ordered by
hot-zone-entry count first (the riskiest theme to review first), then by
size.

## Depth proportional to risk

Reading full diffs for 300 commits every session doesn't scale and isn't
where the value is. Within a theme's batch:

- **Hot-zone entries** (`hot_zone: true`) and anything else that looks
  adjacent to a `CLAUDE.md` divergence paragraph even without the flag →
  full `git show <hash>` read, real explanation, run the gotcha checklist
  below explicitly.
- **Everything else in the theme** → skim via `git show --stat <hash>` plus
  the commit message; summarize the batch as a whole (what kind of changes,
  roughly how many, anything that stands out). Only drop into a full diff
  read for an individual commit if something in the stat/message looks
  off - a rename, a deletion, a config default changing, a vague message
  hiding a large diff (the same instinct that flagged the "WIP" commit
  during triage).

This mirrors triage's own hot-zone-hit-routes-to-deeper-look pattern, just
applied one stage later and to explanation depth rather than verdict
confidence.

## The gotcha checklist

For every batch reviewed, explicitly check for and call out:

1. **Gameplay/behavior defaults changing**, not just bugfixes - anything
   that changes balance, tuning, or visuals a player would notice, even if
   the commit message frames it as a "fix."
2. **Dangling references** - a symbol, config key, or script global that's
   added/renamed/removed here but still referenced elsewhere (same failure
   mode as the `xr_combat_ignore.script` deletion found during the
   2026-07-26 triage session - verified by checking whether upstream's own
   later history ever cleaned up the reference).
3. **Second-pass hot-zone check** - does this commit touch something Old
   World has clearly customized that isn't covered by the current hot-zone
   set (`CLAUDE.md` citations + `hotzones.jsonl`)? If so, propose a
   registry addition the same way triage does:
   ```
   python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add "<pattern>" --reason "<why>" --related-commit <hash>
   ```
   Never append silently - list it alongside the batch findings and let the
   confirmation step (below) approve it.
4. **Intra-batch ordering/dependency gotchas** - does a commit in this
   batch assume another *pending* commit (in this batch or elsewhere in the
   `take` backlog) has already landed? Flag it so `upstream-merge-apply`'s
   cherry-pick order doesn't hit an avoidable conflict.

## Report file

Write findings to `.claude/upstream-merge/reviews/<theme-slug>-<YYYY-MM-DD>.md`
(one file per theme per session; re-running review on the same theme later
appends a new dated file rather than overwriting, so old sessions stay
readable) with this shape:

```markdown
# <Theme> - reviewed 2026-07-26

## Summary
2-4 sentences: what this cluster of commits does as a whole, and the
overall risk read (routine / worth a playtest / needs care during apply).

## Hot-zone / deep-read commits
### <short_hash> - <subject>
What it does, in plain terms. Gotchas found (or "none found"). Verdict:
unchanged / revised to X (see below).

## Batch-summarized commits
Table or short list: hash, subject, one-line take.

## Gotchas / follow-ups
- Anything flagged from the checklist above, including proposed hot-zone
  additions and any verdict flips proposed to the user.
```

Render the same content in the chat reply too - don't make the user open
the file to see what happened this session.

## Confirming a batch

One `AskUserQuestion` per theme batch, after the findings are rendered:

```
question: "Reviewed <theme> (N commits). How do you want to proceed?"
options:
  - "All good - mark reviewed as-is"
  - "Flip specific ones (I'll list)"
  - "Hold this batch - go commit-by-commit"
  - "Stop here for now"
```

- **All good** → `mark-reviewed` the whole batch, run any approved
  `hotzone-add` calls from this batch's findings.
- **Flip specific ones** → apply the user's free-text corrections via
  `revise` first, then `mark-reviewed` the whole batch (a flipped verdict
  still counts as reviewed - it's been looked at).
- **Hold - go commit-by-commit** → drop batching for this theme, walk each
  commit individually in plain conversation until the user says resume.
- **Stop here for now** → mark nothing reviewed, don't write the report
  file (or note it as a partial/draft if already written), report progress,
  and let the next session pick the theme back up from `pending`/`group`.

## Relationship to apply

Review is advisory, not a gate - `upstream-merge-apply` will cherry-pick
`take` entries whether or not `reviewed` is true. Its `plan` output surfaces
an informational count of how many pending commits haven't been reviewed
yet, so you can decide per-run whether to review first or just go.
