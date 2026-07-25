# Ledger schema

Location: `.claude/upstream-merge/ledger/upstream-merge-ledger.jsonl` (data,
append-only) + `upstream-merge-ledger.meta.json` (single mutable record).
Both are managed exclusively through `scripts/triage_helpers.py` - don't
hand-edit the JSONL file directly; append through the script so the
`meta.json` marker stays consistent.

## Why JSONL, not a Markdown table

Appends are just new trailing lines - concurrent appends from two people
produce at worst a trivial line-order conflict, not a scrambled table. Each
line parses independently for the next run's hash lookup. A Markdown table
would drift out of column alignment under hand edits and turn every insert
into a diff-noisy mid-file change. Human skimmability is handled by
`triage_helpers.py render`, which renders a Markdown summary **on demand**
rather than maintaining a second stored copy that could go stale.

## `meta.json`

```json
{
  "schema_version": 1,
  "last_synced_upstream_hash": "920da8f82876ae37615c168fd83d0d73e4915422",
  "hotzone_source_hash": "sha256-of-claude-md-divergence-section",
  "entry_count": 44,
  "updated_at": "2026-07-25T15:57:28+00:00"
}
```

`last_synced_upstream_hash` only advances through a **fully-decided
contiguous prefix** of the candidate commit list (oldest-first). This is
recomputed automatically on every `append` call - it means an interrupted
triage session resumes correctly instead of silently skipping an undecided
commit that happens to sit earlier in history than a later one that got
decided out of order.

## Ledger entry fields

| Field | Type | Notes |
|---|---|---|
| `hash` | string | full 40-char SHA, primary key |
| `short_hash` | string | 8-char, matches `CLAUDE.md`'s own citation style |
| `date` | string (ISO 8601) | author date |
| `subject` | string | commit subject, truncated ~120 chars |
| `verdict` | enum | `take` \| `skip` \| `review` |
| `rationale` | string | 1-2 sentences; cites the `CLAUDE.md` section/feature on conflict |
| `hot_zone` | bool | true if touched files intersected the hot-zone set/globs |
| `hot_zone_files` | array\<string\> | overlapping paths; `[]` if not hot-zone |
| `decision_mode` | enum | `auto` (bulk, unasked) \| `confirmed` (batch-approved) \| `manual` (hand-edited) |
| `applied` | bool | whether it's been cherry-picked onto `merge-upstream` - written by `upstream-merge-apply`, always `false` when this skill creates the entry |
| `applied_date` | string\|null | filled in by `upstream-merge-apply` |
| `run_id` | string | e.g. `2026-07-25T10:00Z`, which invocation produced this entry |

## Worked examples

**`take`, auto-classified** (real commit, verified against this repo):

```jsonl
{"hash":"676edc74e117e98e6841f5f406c3369c10340edb","short_hash":"676edc74","date":"2026-07-05T17:58:25+10:00","subject":"fix CPatrolPoint::load_from_config","verdict":"take","rationale":"Bugfix confined to src/xrGame/patrol_point.cpp, no hot-zone overlap, message matches low-risk pattern.","hot_zone":false,"hot_zone_files":[],"decision_mode":"auto","applied":false,"applied_date":null,"run_id":"2026-07-25T10:00Z"}
```

**`review`, hot-zone hit but not obviously conflicting** (real commit -
demonstrates why hot-zone detection must be file-level, and why hits route
to `review` rather than a silent verdict either way: the file overlaps our
weather divergence work, but the diff itself is a 4-line unrelated tweak):

```jsonl
{"hash":"132ae11be05d2cbfdfc79104508593b223994467","short_hash":"132ae11b","date":"2026-05-23T03:32:43+10:00","subject":"apply random offset for weather's ambient_particles in CGamePersistent::WeathersUpdate","verdict":"review","rationale":"Touches src/xrGame/GamePersistent.cpp, which our weather divergence work also modifies heavily, but this diff only randomizes ambient-particle emitter offset (4 lines) and doesn't touch fog/contrast/wind logic - likely mergeable, flagged for a human look purely on file-level overlap.","hot_zone":true,"hot_zone_files":["src/xrGame/GamePersistent.cpp"],"decision_mode":"confirmed","applied":false,"applied_date":null,"run_id":"2026-07-25T10:00Z"}
```

**`skip`, direct conflict with documented design** (illustrative - not a
real backlog commit as of this writing):

```jsonl
{"hash":"deadbeef00000000000000000000000000000000","short_hash":"deadbeef","date":"2026-01-01T00:00:00+00:00","subject":"[ILLUSTRATIVE - not a real commit] re-add HDR10_TONEMAPPER operator selector","verdict":"skip","rationale":"Directly contradicts the deliberate single-Hermite-spline design (the prior tonemap-selector system was intentionally deleted, not merged into a default); see CLAUDE.md's 'Tonemapping is one custom curve, not a menu.'","hot_zone":true,"hot_zone_files":["src/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp"],"decision_mode":"confirmed","applied":false,"applied_date":null,"run_id":"2026-07-25T10:00Z"}
```

## Script commands

- `hotzone [--refresh]` - derive/print the hot-zone file set (CLAUDE.md
  cited hashes + the `hotzones.jsonl` registry - see
  `hotzones-schema.md`); `--refresh` forces recomputation even if the cache
  looks valid.
- `hotzone-add <pattern> --reason "..."` - append a new pattern to the
  registry; see `hotzones-schema.md`.
- `pending [--since <ref>]` - list commits not yet in the ledger, split
  into `auto_take` (ready to append as-is) and `needs_review` (needs a
  model-proposed verdict).
- `append <file|->` - append finalized record(s) (JSON array or JSON-lines)
  to the ledger; deduplicates by hash; updates `meta.json`.
- `render [--verdict take|skip|review]` - print a Markdown summary table.
