# Gamedata protocol

## Why this rides on the shared ledger instead of a standalone diff

`upstream-merge-triage` has already decided which upstream commits Old
World's engine wants (`verdict: take`). Re-deriving that from scratch here
would duplicate its judgment and risk disagreeing with it. Instead this
skill filters the same shared ledger
(`.agents/upstream-merge/ledger/upstream-merge-ledger.jsonl`) down to
`take` commits that touch `gamedata/`, and works directly from each
commit's diff (`git show <hash> -- gamedata/`) rather than waiting for
`upstream-merge-apply` to have cherry-picked it onto `merge-upstream` -
the point is knowing what needs a manual port as early as possible, not
gating on this repo's own commit history.

## Privacy rules (read this section fully before running anything)

1. **Only `gamedata_helpers.py set-path` may write the private root path**,
    and only into `.agents/upstream-merge/gamedata-review.local.json`,
   which is gitignored. Never write that path into any other file, never
   echo it into a commit message, never put it in `gamedata-ledger.jsonl`.
2. **`gamedata-ledger.jsonl` may only ever name this repo's own
   `gamedata/...`-relative paths** (already public - it's this repo's own
   bundled/distribution copy) plus the upstream commit hash/subject
   (already public - it's `themrdemonized/xray-monolith`'s own history)
   and a generic disposition/rationale. Never a private-tree-relative path,
   a private-only filename, or an excerpt of private file content.
   `gamedata_helpers.py record` enforces the "no private root path
   substring" half of this mechanically and rejects the write if it finds
   one; the "no private-relative paths / no content excerpts" half isn't
   mechanically checkable (there's no way to tell a private path from an
   engine path by string shape alone), so it's on you to keep `notes`
   fields generic - describe *what kind* of divergence was found ("private
   file already has a custom variant of this cvar default", "file doesn't
   exist in private tree - likely unused") rather than quoting private
   content.
3. **Session/chat output is not the concern** - comparing the two trees
   necessarily means reading and discussing private file content and paths
   during the conversation. That's expected and fine. The boundary is
   specifically: nothing containing the private root path, a private
   relative path, or private content may end up **written into a file this
   repo's git tracks**. The plan-mode plan file is harness-managed scratch
   state, not part of this repo, so it's fine for it to contain whatever
   detail makes the plan concrete.
4. **Never run git commands inside the private tree.** This skill's write
   access there is limited to file content (Read/Write tools on individual
   files) - never `git add`, `git commit`, `git status`, or any other git
   invocation with a `-C <private-root>` or cwd inside that tree. Staging
   and committing the port is the user's own step, done outside this
   session, in their own time and with their own review.

## File classification

For each `gamedata/<rel>` path touched by a pending commit, look at three
things: the commit's pre-image (`git show <hash>^:gamedata/<rel>`, or "did
not exist" if the commit added the file), the commit's post-image (`git
show <hash>:gamedata/<rel>`), and the private tree's current content at the
corresponding path (read via the Read tool, using the root from
`show-path`).

- **Private file doesn't exist at all** → ask about scope (see below)
  rather than assuming either "needs adding" or "not used." Old World's
  private tree not having a file is meaningful signal but not proof either
  way - it could be legitimately unused vanilla/legacy content, or a file
  that's simply never needed touching before now.
- **Private file's content matches the commit's post-image already** →
  `ported` (or `not_applicable` if it arrived there some other way, e.g.
  independently authored) - nothing to do, record and move on.
- **Private file's content matches the commit's pre-image** → clean port
  candidate: applying the commit's exact diff to the private file
  reproduces upstream's intent with no adaptation needed.
- **Private file's content matches neither** → the private tree has
  already diverged (Old World-specific tuning/content in that file). This
  needs a proposed adaptation, not a blind patch - read both diffs, work
  out what the upstream change is actually doing, and propose how to fold
  it into the private file's existing divergence. Present this like
  `upstream-merge-apply` treats a cherry-pick conflict: read the hunks,
  propose a specific resolution, get explicit confirmation before writing.

A commit with multiple `gamedata/` files can land in different buckets per
file - classify file by file, not commit by commit.

## Out-of-scope questions

Batch these with `AskUserQuestion` (group by commit or by theme if several
came up in one session, matching `upstream-merge-review`'s batching
instinct rather than asking one file at a time):

```
question: "<file> doesn't exist in the private tree - <one-line context,
  e.g. R1-only shader variant, vanilla-only weapon config, or 'no obvious
  reason it wouldn't be needed'>. In scope for Old World?"
options:
  - "Yes - port it (I'll classify what that means)"
  - "No - out of scope, mark not_applicable"
  - "Not sure - hold as needs_followup"
```

Resolve every out-of-scope question before entering plan mode - the plan
should reflect settled scope, not open questions the user hasn't answered
yet.

## Plan content (`EnterPlanMode` / `ExitPlanMode`)

The plan file should contain, per pending commit:

1. The commit hash/subject and which `gamedata/` files it touches.
2. Each file's classification (clean port / already ported / needs
   adaptation / out of scope, with the scope answer already folded in).
3. For clean ports: the exact new content being written.
4. For needs-adaptation files: the proposed resolution in enough detail to
   approve without re-deriving it (what upstream changed, what the private
   file's existing divergence is, how the proposed merge reconciles them).
5. Confirmation that no `git` operations will run inside the private tree.

## Execution rules (only after plan approval)

- **Clean ports**: write them in small confirmed batches (an
  `AskUserQuestion` per batch of ~5-10 - "apply these N straightforward
  ports?" - not a separate gate per single file; they were already fully
  specified in the approved plan, so this batch confirmation is about
  catching a last-second "wait, not that one" rather than re-litigating
  each file).
- **Needs-adaptation files**: walk one at a time. Show the proposed
  resolution from the plan, get explicit confirmation, then write - same
  discipline as `upstream-merge-apply`'s conflict handling, just applied to
  a manual merge instead of a cherry-pick conflict.
- **Stop and ask** if a file's private-tree content has changed since step
  3's comparison (re-read before writing if any meaningful time passed in
  the session) - don't write over something that moved out from under the
  plan.
- Never touch git state inside the private tree (see privacy rule 4).

## Recording dispositions

After each file/commit is resolved, build a record:

```json
{
  "hash": "<full hash>",
  "short_hash": "<8-char>",
  "subject": "<commit subject>",
  "engine_files": ["gamedata/configs/..."],
  "disposition": "ported",
  "notes": "private file matched pre-image exactly, applied commit's diff as-is"
}
```

`disposition` is one of `ported` / `adapted` / `not_applicable` / `skipped`
/ `needs_followup`. Pass an array of these to
`gamedata_helpers.py record <file>`. A commit whose files land in different
buckets can be recorded as multiple entries under the same `hash` if that's
clearer, or one entry with a `notes` summary spanning all of them - either
is fine, `record` upserts by `hash` so the latest call wins if you record
the same hash twice.
