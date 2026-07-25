# Upstream merge skills

Two Claude Code skills for pulling fixes/features from
`themrdemonized/xray-monolith` (the `upstream` remote) into this fork
without re-litigating, every session, which of the hundreds of upstream
commits conflict with Old World's deliberate engine divergence (documented
in `CLAUDE.md`).

- **`upstream-merge-triage`** - read-only analysis. Proposes take/skip/review
  verdicts for new upstream commits and logs every decision to a shared
  ledger. Never touches git state beyond `git fetch`.
- **`upstream-merge-apply`** - execution. Takes the ledger's accepted
  (`take`) commits, drafts a plan, gets it approved through Claude Code's
  real plan-mode flow, then cherry-picks them onto `merge-upstream`. Never
  touches `all-in-one-vs2022-wpo`.

They share state under `.claude/upstream-merge/` (see below), so either
teammate running either skill sees the same history of decisions.

## Invoking them

From a Claude Code session in this repo:

```
/upstream-merge-triage
/upstream-merge-triage --since <ref>
/upstream-merge-apply
/upstream-merge-apply --limit 10
```

Triage will fetch `upstream`, classify anything new, auto-log the obvious
low-risk ones, and walk you through the rest in batches (usually 1-2
rounds of questions once the initial backlog is cleared - see
`upstream-merge-triage/references/batching-protocol.md` if you want the
exact mechanics). Apply will show you a plan-mode plan of exactly what it's
about to cherry-pick before it touches anything.

Typical flow: run triage periodically as `upstream` moves, let confirmed
`take` verdicts accumulate, then run apply when you're ready to actually
land a batch of them.

## Shared state (`.claude/upstream-merge/`)

| Path | What it is |
|---|---|
| `ledger/upstream-merge-ledger.jsonl` | One JSON line per triaged upstream commit: verdict, rationale, hot-zone info, whether it's been applied yet. Append-only from triage; `applied`/`applied_date` get flipped in place by apply. |
| `ledger/upstream-merge-ledger.meta.json` | Small state record: how far triage has fully processed (`last_synced_upstream_hash`), entry count, etc. |
| `hotzones.jsonl` | Persistent, hand-curated list of files/globs we've deliberately customized, beyond what's derivable from commit hashes cited in `CLAUDE.md`. Grows over time as triage sessions discover more. |
| `.cache/hotzone.json` | Recomputed automatically whenever `CLAUDE.md` or `hotzones.jsonl` changes - not something you need to touch or clear by hand. |

Full field-level schemas: `upstream-merge-triage/references/ledger-schema.md`
and `upstream-merge-triage/references/hotzones-schema.md`.

## Running the scripts yourself

Both skills are backed by small stdlib-only Python scripts that do nothing
git-mutating except `append`/`hotzone-add` (ledger/registry writes) and
apply's actual `git cherry-pick` (which the skill runs directly, not the
script). You can run them straight from a terminal any time you want to
poke at triage state without going through Claude at all.

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

None of the above ever runs `git cherry-pick`, `commit`, or any other
git-write - that only happens inside the `upstream-merge-apply` skill
itself, after you've approved its plan-mode plan.

## If you're new to this

Read `upstream-merge-triage/SKILL.md` and `upstream-merge-apply/SKILL.md`
first - they're short and are the actual source of truth for what each
skill does step by step (this README is the human-onboarding layer on top,
not a replacement). The `references/` subdirectory under each skill has
the deeper schema/protocol detail this README intentionally leaves out.
