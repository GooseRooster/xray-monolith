# Ledger schema

Location: `.agents/upstream-merge/ledger/upstream-merge-ledger.jsonl` (data,
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
  "hotzone_source_hash": "sha256-of-project-md-divergence-section",
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
| `short_hash` | string | 8-char, matches `PROJECT.md`'s own citation style |
| `date` | string (ISO 8601) | author date |
| `subject` | string | commit subject, truncated ~120 chars |
| `verdict` | enum | `take` \| `skip` \| `review` |
| `rationale` | string | 1-2 sentences; cites the `PROJECT.md` section/feature on conflict |
| `hot_zone` | bool | true if touched files intersected the hot-zone set/globs |
| `hot_zone_files` | array\<string\> | overlapping paths; `[]` if not hot-zone |
| `interest_category` | string\|null | which interest category matched (`perf` \| `modding` \| `graphics` \| `infra`), or `null` for `auto_skip` and `needs_review` entries. Set by the `pending` command; persisted for querying and reporting |
| `decision_mode` | enum | `auto` (bulk, unasked) \| `confirmed` (batch-approved) \| `manual` (hand-edited) |
| `applied` | bool | whether it's been cherry-picked onto `merge-upstream` - written by `upstream-merge-apply`, always `false` when this skill creates the entry |
| `applied_date` | string\|null | filled in by `upstream-merge-apply` |
| `run_id` | string | e.g. `2026-07-25T10:00Z`, which invocation produced this entry |
| `reviewed` | bool | optional - whether `upstream-merge-review` has explained this commit in depth. Absent/missing is equivalent to `false`; this skill never sets it |
| `reviewed_date` | string\|null | optional - filled in by `upstream-merge-review` |
| `review_notes` | string\|null | optional - short gotcha note from `upstream-merge-review`, if any surfaced |

The `reviewed*` fields are additive and optional by design - every entry
this skill (`upstream-merge-triage`) or `upstream-merge-apply` writes omits
them, and both treat a missing `reviewed` as `false` rather than requiring
a backfill. Only `upstream-merge-review` ever sets them.

## Worked examples

**`take`, interest-category auto-classified** (real commit, verified against this repo):

```jsonl
{"hash":"884fae13f2b5e2a1c8d7e0f3a4b5c6d7e8f9a0b1","short_hash":"884fae13","date":"2026-07-15T12:30:00+10:00","subject":"DLTX more accurate cache stats","verdict":"take","rationale":"Modding capability: DLTX more accurate cache stats. Touches: src/xrGame/DLTX.cpp, src/xrCore/xrXMLParser/xrXMLParser.cpp","hot_zone":false,"hot_zone_files":[],"interest_category":"modding","decision_mode":"auto","applied":false,"applied_date":null,"run_id":"2026-08-11T10:00Z"}
```

**`review`, graphics auto-classified** (real commit — graphics features are
always flagged `review`, never auto-taken, so they can be evaluated against
Old World's visual vision):

```jsonl
{"hash":"abcd1234abcd5678abcd9012abcd3456abcd7890","short_hash":"abcd1234","date":"2026-06-20T14:00:00+03:00","subject":"enable volumetric and shadow for signal light in Zaton and Red Forest","verdict":"review","rationale":"Graphics/rendering feature - flagged for review against Old World's visual vision: enable volumetric and shadow for signal light in Zaton and Red Forest. Touches: gamedata/configs/scripts/zaton/zaton.ltx, gamedata/configs/scripts/jupiter/jupiter.ltx","hot_zone":false,"hot_zone_files":[],"interest_category":"graphics","decision_mode":"auto","applied":false,"applied_date":null,"run_id":"2026-08-11T10:00Z"}
```

**`review`, hot-zone hit but not obviously conflicting** (real commit -
demonstrates why hot-zone detection must be file-level, and why hits route
to `needs_review` regardless of interest-category match — the file overlaps
our weather divergence work, but the diff itself is a 4-line unrelated tweak):

```jsonl
{"hash":"132ae11be05d2cbfdfc79104508593b223994467","short_hash":"132ae11b","date":"2026-05-23T03:32:43+10:00","subject":"apply random offset for weather's ambient_particles in CGamePersistent::WeathersUpdate","verdict":"review","rationale":"Touches src/xrGame/GamePersistent.cpp, which our weather divergence work also modifies heavily, but this diff only randomizes ambient-particle emitter offset (4 lines) and doesn't touch fog/contrast/wind logic - likely mergeable, flagged for a human look purely on file-level overlap.","hot_zone":true,"hot_zone_files":["src/xrGame/GamePersistent.cpp"],"interest_category":null,"decision_mode":"confirmed","applied":false,"applied_date":null,"run_id":"2026-07-25T10:00Z"}
```

**`skip`, auto-classified — no interest match** (illustrative):

```jsonl
{"hash":"deadbeef00000000000000000000000000000001","short_hash":"deadbeef","date":"2026-01-01T00:00:00+00:00","subject":"[ILLUSTRATIVE] update gamedata files","verdict":"skip","rationale":"No interest-category match (perf/modding/graphics/infra) and no hot-zone overlap. Auto-skipped per Old World's debloat-only triage philosophy. Touches: gamedata/configs/weapons/w_ak74.ltx","hot_zone":false,"hot_zone_files":[],"interest_category":null,"decision_mode":"auto","applied":false,"applied_date":null,"run_id":"2026-08-11T10:00Z"}
```

**`skip`, direct conflict with documented design** (illustrative - not a
real backlog commit as of this writing):

```jsonl
{"hash":"deadbeef00000000000000000000000000000002","short_hash":"deadbeef","date":"2026-01-01T00:00:00+00:00","subject":"[ILLUSTRATIVE - not a real commit] re-add HDR10_TONEMAPPER operator selector","verdict":"skip","rationale":"Directly contradicts the deliberate single-Hermite-spline design (the prior tonemap-selector system was intentionally deleted, not merged into a default); see PROJECT.md's 'Tonemapping is one custom curve, not a menu.'","hot_zone":true,"hot_zone_files":["src/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp"],"interest_category":null,"decision_mode":"confirmed","applied":false,"applied_date":null,"run_id":"2026-07-25T10:00Z"}
```

## Script commands

- `hotzone [--refresh]` - derive/print the hot-zone file set (PROJECT.md
  cited hashes + the `hotzones.jsonl` registry - see
  `hotzones-schema.md`); `--refresh` forces recomputation even if the cache
  looks valid.
- `hotzone-add <pattern> --reason "..."` - append a new pattern to the
  registry; see `hotzones-schema.md`.
- `pending [--since <ref>]` - list upstream commits not yet in the ledger,
  classified by interest category and hot-zone overlap. Returns buckets:
  `auto_skip`, `interest_perf`, `interest_modding`, `interest_infra`,
  `graphics_review`, `needs_review`.
- `append <file|->` - append finalized record(s) (JSON array or JSON-lines)
  to the ledger; deduplicates by hash; updates `meta.json`.
- `render [--verdict take|skip|review] [--category perf|modding|graphics|infra]` - print a Markdown summary table from the ledger, optionally filtered.
