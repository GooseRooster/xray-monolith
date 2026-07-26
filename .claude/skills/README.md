# Upstream merge skills

Four Claude Code skills for pulling fixes/features from
`themrdemonized/xray-monolith` (the `upstream` remote) into this fork
without re-litigating, every session, which of the hundreds of upstream
commits conflict with Old World's deliberate engine divergence (documented
in `CLAUDE.md`) - plus a fourth for the adjacent problem of upstream
changes to bundled/distribution `gamedata/` that need a manual port into
the private, actively-developed Old World gamedata tree.

- **`upstream-merge-triage`** - read-only analysis. Proposes take/skip/review
  verdicts for new upstream commits and logs every decision to a shared
  ledger. Never touches git state beyond `git fetch`.
- **`upstream-merge-review`** - read-only (aside from `git show`/
  `git diff-tree`) deep dive. Explains the accepted `take` backlog in real
  depth, grouped by subsystem theme rather than commit order, with a
  gotcha checklist (behavior changes, dangling references, missed hot-zone
  overlaps, intra-batch ordering dependencies). Advisory - sits between
  triage and apply, doesn't gate it, but records its findings as
  structured flags (not just prose) so apply can act on them.
- **`upstream-merge-apply`** - execution. Takes the ledger's accepted
  (`take`) commits, drafts a plan - checking review's ordering-dependency
  flags against the actual batch along the way - gets it approved through
  Claude Code's real plan-mode flow, then cherry-picks them onto
  `merge-upstream`. Never touches `all-in-one-vs2022-wpo`.
- **`upstream-merge-gamedata`** - compares `take` commits that touch this
  repo's bundled/distribution `gamedata/` against your private gamedata
  tree, asks about anything that looks out of scope for the mod, drafts a
  merge plan, and - once approved - ports the in-scope changes into the
  private tree. The private tree's location lives in a gitignored local
  pointer file, never in anything this repo tracks - see its own
  `references/gamedata-protocol.md` for the full privacy rule set before
  using it.

The first three share state under `.claude/upstream-merge/` (see below), so
any teammate running any of them sees the same history of decisions.
`upstream-merge-gamedata` reads that same shared ledger but keeps its own
findings in a separate, still-shared file (`gamedata-ledger.jsonl`) that's
deliberately scrubbed of anything private - only its one-line pointer file
is local-only.

## Invoking them

From a Claude Code session in this repo:

```
/upstream-merge-triage
/upstream-merge-triage --since <ref>
/upstream-merge-review
/upstream-merge-review --theme "AI / Combat"
/upstream-merge-apply
/upstream-merge-apply --limit 10
/upstream-merge-gamedata
/upstream-merge-gamedata --limit 10
```

Triage will fetch `upstream`, classify anything new, auto-log the obvious
low-risk ones, and walk you through the rest in batches (usually 1-2
rounds of questions once the initial backlog is cleared - see
`upstream-merge-triage/references/batching-protocol.md` if you want the
exact mechanics). Review will group the accepted backlog by subsystem and
walk through one theme per session, writing a report you can reread later.
Apply will show you a plan-mode plan of exactly what it's about to
cherry-pick before it touches anything, flagging any ordering-dependency
violation review surfaced. Gamedata will (after a one-time
`set-path` setup) walk the `take` backlog's gamedata-touching commits,
compare each against your private tree, and show a plan-mode plan before
writing anything outside this repo.

Typical flow: run triage periodically as `upstream` moves, let confirmed
`take` verdicts accumulate, optionally run review on a theme or two to
actually understand what's piled up, then run apply when you're ready to
land a batch. Run gamedata whenever you want to catch up the private tree
on distribution gamedata changes - independent of whether apply has landed
those commits on `merge-upstream` yet.

## Shared state (`.claude/upstream-merge/`)

| Path | What it is |
|---|---|
| `ledger/upstream-merge-ledger.jsonl` | One JSON line per triaged upstream commit: verdict, rationale, hot-zone info, whether it's been applied yet, and (optional) whether/when `upstream-merge-review` looked at it. Append-only from triage; `applied`/`applied_date` flipped in place by apply; `reviewed`/`reviewed_date`/`review_notes`/`review_flags` flipped in place by review. |
| `ledger/upstream-merge-ledger.meta.json` | Small state record: how far triage has fully processed (`last_synced_upstream_hash`), entry count, etc. |
| `hotzones.jsonl` | Persistent, hand-curated list of files/globs we've deliberately customized, beyond what's derivable from commit hashes cited in `CLAUDE.md`. Grows over time as triage (and review) sessions discover more. |
| `.cache/hotzone.json` | Recomputed automatically whenever `CLAUDE.md` or `hotzones.jsonl` changes - not something you need to touch or clear by hand. |
| `reviews/<theme-slug>-<date>.md` | Written by `upstream-merge-review`: one dated report per subsystem theme per session, explaining what that batch of commits changes and any gotchas found. |
| `gamedata-ledger.jsonl` | Written by `upstream-merge-gamedata`: one JSON line per dispositioned gamedata-touching commit (`ported`/`adapted`/`not_applicable`/`skipped`/`needs_followup`), naming only this repo's own `gamedata/...` paths - never anything private. |
| `gamedata-review.local.json` | **Not shared** - gitignored, machine-local. Written only by `gamedata_helpers.py set-path`. Holds the absolute path to your private gamedata tree; nothing else in this repo may ever contain it. |

Full field-level schemas: `upstream-merge-triage/references/ledger-schema.md`,
`upstream-merge-triage/references/hotzones-schema.md`,
`upstream-merge-review/references/review-protocol.md` (structured review
flags), and `upstream-merge-gamedata/references/gamedata-protocol.md`.

## Running the scripts yourself

All four skills are backed by small stdlib-only Python scripts that do
nothing git-mutating except `append`/`hotzone-add`/`revise`/`mark-reviewed`/
`record` (ledger/registry writes) and apply's actual `git cherry-pick`
(which the skill runs directly, not the script). You can run them straight
from a terminal any time you want to poke at state without going through
Claude at all.

**See what's pending, without deciding anything:**

```
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py pending
```

Prints JSON: `auto_take` (already-decided, low-risk commits) and
`needs_review` (everything still needing a human/model judgment call), plus
a `total_new` count. Add `--since <ref>` to check from a specific commit
instead of the last-synced marker.

**Check the current hot-zone set:**

```
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone
```

Prints the full derived file list plus the registry patterns folded in.
Add `--refresh` to force recomputation (normally automatic on any
`CLAUDE.md`/`hotzones.jsonl` change).

**Add something to the hot-zone registry by hand** (e.g. you noticed a file
matters without going through a triage session):

```
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add \
  "src/xrGame/SomeFile.cpp" --reason "why this matters" --added-via manual
```

**Read the ledger as a table** instead of raw JSONL:

```
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py render
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py render --verdict skip
```

**See what's queued to be cherry-picked, or preview the apply plan:**

```
python3 .claude/skills/upstream-merge-apply/scripts/apply_helpers.py pending
python3 .claude/skills/upstream-merge-apply/scripts/apply_helpers.py plan --limit 10
```

`plan` also cross-checks every reviewed commit's `ordering_after` flags
against the actual batch and prints any violation up front.

**See the accepted backlog grouped by subsystem, without writing anything:**

```
python3 .claude/skills/upstream-merge-review/scripts/review_helpers.py pending
python3 .claude/skills/upstream-merge-review/scripts/review_helpers.py group
```

`group` prints themes ordered riskiest-first (by hot-zone-entry count, then
size) - the actual explanations and gotcha-checking only happen inside the
skill itself, since they need a real diff read, not something a mechanical
script can produce.

**See what gamedata-touching commits still need a disposition, or read the
disposition ledger:**

```
python3 .claude/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py set-path /abs/path/to/private/gamedata
python3 .claude/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py pending
python3 .claude/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py render
```

`set-path` is one-time per machine. `record` (used by the skill, not
usually by hand) rejects any disposition record that contains the
configured private root path, as a mechanical backstop on top of the
skill's own privacy rules.

None of the above ever runs `git cherry-pick`, `commit`, or any other
git-write in *this* repo - that only happens inside the
`upstream-merge-apply` skill itself, after you've approved its plan-mode
plan. `upstream-merge-gamedata` can write file content into your private
gamedata tree after its own plan approval, but never runs git inside that
tree either - staging/committing there stays a manual step.

## If you're new to this

Read `upstream-merge-triage/SKILL.md`, `upstream-merge-review/SKILL.md`,
`upstream-merge-apply/SKILL.md`, and `upstream-merge-gamedata/SKILL.md`
first - they're short and are the actual source of truth for what each
skill does step by step (this README is the human-onboarding layer on top,
not a replacement). The `references/` subdirectory under each skill has
the deeper schema/protocol detail this README intentionally leaves out.
