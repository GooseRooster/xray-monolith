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
   the commit message frames it as a "fix." → emit a `playtest` flag.
2. **Dangling references** - a symbol, config key, or script global that's
   added/renamed/removed here but still referenced elsewhere (same failure
   mode as the `xr_combat_ignore.script` deletion found during the
   2026-07-26 triage session - verified by checking whether upstream's own
   later history ever cleaned up the reference). → emit a
   `dangling_reference` flag.
3. **Second-pass hot-zone check** - does this commit touch something Old
   World has clearly customized that isn't covered by the current hot-zone
   set (`CLAUDE.md` citations + `hotzones.jsonl`)? If so, propose a
   registry addition the same way triage does:
   ```
   python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add "<pattern>" --reason "<why>" --related-commit <hash>
   ```
   Never append silently - list it alongside the batch findings and let the
   confirmation step (below) approve it. → also emit a `hotzone_gap` flag
   (even though the addition itself waits for approval, the flag records
   that this commit is what surfaced the gap).
4. **Intra-batch ordering/dependency gotchas** - does a commit in this
   batch assume another *pending* commit (in this batch or elsewhere in the
   `take` backlog) has already landed? → emit an `ordering_after` flag on
   the *dependent* commit (never on the prerequisite), naming every
   prerequisite hash. This is the one `upstream-merge-apply` actively
   checks against its cherry-pick batch, so get it right: `after` lists the
   full hashes (from the ledger's `hash` field, not `short_hash`) that must
   land before this commit, regardless of which theme they were grouped
   into or whether they've been reviewed yet.

A fifth, non-checklist finding worth flagging when you spot one: a real bug
in the upstream diff itself (not a divergence issue, just a bug) that isn't
a reason to skip the commit but is worth a future fix - `bug_found`.

## Structured review flags

The prose write-up in the report file is for humans; `upstream-merge-apply`
can't parse it. Every checklist hit above must *also* be encoded as a
structured flag passed to `mark-reviewed`, so `apply`'s plan step can act on
it deterministically instead of a human having to re-read every report
before applying. `review_helpers.py mark-reviewed` validates flags against a
fixed vocabulary (`FLAG_TYPES` in the script) and rejects anything
malformed or unrecognized - if validation fails, fix the record and re-run
rather than dropping the flag.

`mark-reviewed`'s input records take an optional `flags` array alongside
`hash`/`notes`:

```json
[
  {
    "hash": "<full hash>",
    "notes": "human-readable note, same as before",
    "flags": [
      {"type": "ordering_after", "after": ["<full hash of prerequisite>"], "note": "why"},
      {"type": "playtest", "note": "what a player would notice and why"},
      {"type": "dangling_reference", "note": "what's dangling and where"},
      {"type": "hotzone_gap", "pattern": "<proposed hotzones.jsonl pattern>", "note": "why"},
      {"type": "bug_found", "note": "what's wrong, not a reason to skip"}
    ]
  }
]
```

Omit `flags` (or pass `[]`) for commits the checklist found nothing on -
`mark-reviewed` always sets `review_flags` (defaulting to `[]`) so every
reviewed entry has the field, and `apply` can check `review_flags` without
a presence check. Every `ordering_after` hash must already exist somewhere
in the ledger (validated at write time) - if the prerequisite commit hasn't
been triaged yet at all, that's a sign review is running ahead of triage
for that area; note it in the report and hold the flag until the
prerequisite exists in the ledger.

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
  additions and any verdict flips proposed to the user. Name the structured
  flag type next to each ("→ `ordering_after`", "→ `playtest`", etc.) so the
  prose and the ledger's `review_flags` stay traceable to each other.
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
