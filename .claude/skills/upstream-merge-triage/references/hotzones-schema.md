# Hot-zone registry schema

Location: `.claude/upstream-merge/hotzones.jsonl` - a persistent,
hand-curated companion to the `CLAUDE.md`-derived hot-zone set. Same JSONL
append-only rationale as the ledger (see `ledger-schema.md`): trivial
concurrent-append conflicts instead of a scrambled table, independent
per-line parsing, no drift from hand-editing a formatted table.

## Why this exists as a separate file, not more prose in `CLAUDE.md`

`CLAUDE.md`'s divergence section is *derived from* - it's the human-facing
narrative of Old World's engineering decisions, and the triage skill mines
it for cited commit hashes. But triage sessions routinely surface files
that matter and aren't covered by any cited hash (e.g. a file touched
alongside a documented feature, but not by one of the specific commits
named in the prose). Requiring a `CLAUDE.md` edit every time one of these
turns up would mean either skipping the discovery or degrading `CLAUDE.md`
into a hot-zone list instead of a design narrative. This file is the
pressure release: a append-only, structured place for exactly those
discoveries, unioned with the `CLAUDE.md`-derived set at hotzone-compute
time (see `derive_hotzone()` in `triage_helpers.py`).

## Entry fields

| Field | Type | Notes |
|---|---|---|
| `pattern` | string | exact file path or glob (e.g. `src/xrSound/*`) |
| `is_glob` | bool | informational for humans; matching always uses `fnmatch`, which degrades to exact-match for literal strings, so this doesn't change behavior either way |
| `reason` | string | why this pattern matters |
| `added_at` | string (ISO 8601) | |
| `added_via` | enum | `seed` (original backstop list, migrated in) \| `triage-session` (discovered and confirmed during a triage run) \| `manual` (hand-added outside a triage session) |
| `related_commit` | string\|null | the upstream commit hash that prompted the addition, if any |

## Worked examples

**Seed entry** (migrated from the original hardcoded backstop list):

```jsonl
{"pattern": "src/xrSound/*", "is_glob": true, "reason": "Backstop hot-zone from original design - explicitly named hot files/globs in CLAUDE.md's merge-workflow and HDR sections, not resolvable via commit-hash citation.", "added_at": "2026-07-25T00:00:00+00:00", "added_via": "seed", "related_commit": null}
```

**Discovered during a triage session** (illustrative shape - not a real
entry as of this writing):

```jsonl
{"pattern": "src/xrGame/ActorCondition.cpp", "is_glob": false, "reason": "Upstream commit abc1234 reworks stamina regen curves here; this file also carries Old World's custom hunger/psy balancing that isn't cited by any divergence commit hash yet.", "added_at": "2026-08-02T09:15:00+00:00", "added_via": "triage-session", "related_commit": "abc1234..."}
```

## Adding an entry

Never hand-edit the file. Always go through the script so dedup-by-pattern
is enforced:

```
python3 .claude/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add "<pattern>" \
  --reason "<why this matters>" \
  --related-commit <hash-that-surfaced-it> \
  [--glob] [--added-via triage-session|manual|seed]
```

Re-adding an already-present pattern is a no-op (printed, not an error) -
safe to call speculatively.

## Interaction with the cache

`derive_hotzone()`'s cache key is a hash of `CLAUDE.md`'s relevant sections
*plus* the full text of this file, so any addition here invalidates the
cache on the very next `hotzone` call - no separate cache-busting step
needed.
