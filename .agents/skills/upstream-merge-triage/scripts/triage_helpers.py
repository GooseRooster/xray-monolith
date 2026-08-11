#!/usr/bin/env python3
"""Mechanical helpers for the upstream-merge-triage skill.

Subcommands:
  hotzone [--refresh]         Derive/cache the hot-zone file set from PROJECT.md
                               plus the persistent hotzones.jsonl registry.
  hotzone-add PATTERN         Append a new pattern to the hotzones registry
                               (--reason required, --glob, --related-commit).
  pending [--since REF]       List upstream commits not yet in the ledger,
                               classified by interest category and hot-zone
                               overlap.
  append FILE|-               Append finalized ledger record(s) (JSON array or
                               JSON-lines) to the ledger and update meta.json.
  render [--verdict V]        Render a Markdown summary table from the ledger.

All commands are read-only with respect to git state except `append` and
`hotzone-add`, which only ever write to files under this skill's shared
ledger/registry directory - never to git.
"""
import argparse
import fnmatch
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
LEDGER_DIR = REPO_ROOT / ".agents" / "upstream-merge" / "ledger"
LEDGER_PATH = LEDGER_DIR / "upstream-merge-ledger.jsonl"
META_PATH = LEDGER_DIR / "upstream-merge-ledger.meta.json"
CACHE_DIR = REPO_ROOT / ".agents" / "upstream-merge" / ".cache"
HOTZONE_CACHE_PATH = CACHE_DIR / "hotzone.json"
PROJECT_MD_PATH = REPO_ROOT / "PROJECT.md"
HOTZONES_REGISTRY_PATH = REPO_ROOT / ".agents" / "upstream-merge" / "hotzones.jsonl"

UPSTREAM_REMOTE_BRANCH = "upstream/all-in-one-vs2022-wpo"
LOCAL_MERGE_BRANCH = "merge-upstream"

DIVERGENCE_HEADING = "## Old World's intentional divergence from upstream"
HDR_HEADING = "## Color grading, HDR and retro rendering options"
NEXT_HEADING_AFTER_HDR = "## Architecture"

HASH_RE = re.compile(r"`([0-9a-f]{7,40})`")

INTEREST_CATEGORIES = {
    "perf": {
        "pattern": re.compile(
            r"\b(multithread|thread[._]?(pool|safe|local|desc)|"
            r"parallel|simd|vectori[sz]|lock[._]?(less|free)|"
            r"job[._]?syste|fiber|coroutine|concurrent|async|"
            r"perf(ormance)?\b|optimi[sz]|cache|prefetch|"
            r"throughput|latency|bottleneck|memory[._]?pool|"
            r"batch|inline|unroll|spatial[._]?hash|octree|"
            r"quadtree|aabb|broadphase|narrowphase|"
            r"frustum.*cull|occlusion.*cull|visibility.*test|"
            r"lod|instanc|pixel.?shader.*drop|"
            r"frame[._]?time|frame[._]?rate|framerate|fps|"
            r"micro[._]?optimiz"
            r")\b",
            re.IGNORECASE,
        ),
        "auto_verdict": "take",
        "rationale_prefix": "Performance optimisation:",
    },
    "modding": {
        "pattern": re.compile(
            r"\b(dltx|DXML|modxml|mod[._]?xml|script[._]?engin|"
            r"lua[._]?bindin|xml[._]?(pars|config|rewrit|read|write|lint|validat)|"
            r"config[._]?pars|ltx[._]?pars|"
            r"callback|event[._]?hook|extend[._]?api|api[._]?extend|"
            r"export[._]?script|script[._]?export|register[._]?script|"
            r"expose.*lua|lua.*expose|open.*lua|lua.*open|"
            r"fs[._]?game|game[._]?fs|virtual[._]?fs|"
            r"spawn[._]?read|spawn[._]?write|spawn[._]?pars|"
            r"add[._]?callback|get[._]?callback"
            r")\b",
            re.IGNORECASE,
        ),
        "auto_verdict": "take",
        "rationale_prefix": "Modding capability:",
    },
    "graphics": {
        "pattern": re.compile(
            r"\b(shader|render|hdr|hdr10|post[._]?process|ssao|ssr|gtao|bloom|"
            r"dof|motion[._]?blur|tonemap|cascade|shadow[._]?map|light[._]?map|"
            r"indirect[._]?light|gi\b|illuminat|pbr|volumetric|fog|particle|"
            r"terrain|tree|grass|instanc|decal|water|ocean|reflect|refract|"
            r"tessellat|sunshaft|god.?ray|lens.?flare|color.?grad|procedural|"
            r"material|light[._]?expansion|skybox|skydome|cloud|rain[._]?drop|"
            r"wet.?surf|ssfx|screen[._]?space"
            r")\b",
            re.IGNORECASE,
        ),
        "auto_verdict": "review",
        "rationale_prefix": "Graphics/rendering feature - flagged for review against Old World's visual vision:",
    },
    "infra": {
        "pattern": re.compile(
            r"\b(xrCore|xrcore|engine[._]?core|"
            r"build[._]?system|toolchain|cmake|msbuild|"
            r"clang|mingw|gcc|cross[._]?compil|"
            r"allocat|heap|memory[._]?manag|"
            r"container|xr_vector|xr_map|xr_set|xr_list|xr_deque|"
            r"file[._]?system|serializ|profiler|tracy|optick|"
            r"xrCPU[._]?Pipe|xrCDB|xrXML|"
            r"safe.?wrap|lock[._]?guard|scoped[._]?ptr|unique[._]?ptr|"
            r"raii|smart[._]?ptr|shared[._]?ptr"
            r")\b",
            re.IGNORECASE,
        ),
        "auto_verdict": "take",
        "rationale_prefix": "Engine infrastructure:",
    },
}


def run_git(*args):
    result = subprocess.run(
        ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def extract_section(text, start_heading, end_heading):
    start = text.index(start_heading)
    end = text.index(end_heading, start) if end_heading else len(text)
    return text[start:end]


def load_hotzones_registry():
    """Persistent, hand-curated hot-zone patterns - a third layer alongside
    PROJECT.md-derived commit hashes. Includes the original backstop globs
    (added_via: seed) plus anything discovered and confirmed during later
    triage sessions (added_via: triage-session) or added by hand
    (added_via: manual). Kept separate from PROJECT.md deliberately, per
    owner request, so new hot zones don't require editing prose."""
    if not HOTZONES_REGISTRY_PATH.exists() or HOTZONES_REGISTRY_PATH.stat().st_size == 0:
        return []
    entries = []
    for line in HOTZONES_REGISTRY_PATH.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            entries.append(json.loads(line))
    return entries


def append_hotzone_registry_entry(entry):
    existing = load_hotzones_registry()
    if any(e["pattern"] == entry["pattern"] for e in existing):
        return False
    with HOTZONES_REGISTRY_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")
    return True


def derive_hotzone(refresh=False):
    project_md = PROJECT_MD_PATH.read_text(encoding="utf-8")
    divergence_section = extract_section(project_md, DIVERGENCE_HEADING, HDR_HEADING)
    hdr_section = extract_section(project_md, HDR_HEADING, NEXT_HEADING_AFTER_HDR)
    registry = load_hotzones_registry()
    registry_text = HOTZONES_REGISTRY_PATH.read_text(encoding="utf-8") if HOTZONES_REGISTRY_PATH.exists() else ""
    source_text = divergence_section + hdr_section + registry_text
    source_hash = hashlib.sha256(source_text.encode("utf-8")).hexdigest()

    if not refresh and HOTZONE_CACHE_PATH.exists():
        cached = json.loads(HOTZONE_CACHE_PATH.read_text(encoding="utf-8"))
        if cached.get("source_hash") == source_hash:
            return cached

    candidate_hashes = sorted(set(HASH_RE.findall(divergence_section)))
    resolved_files = set()
    unresolved_hashes = []
    for h in candidate_hashes:
        try:
            full_hash = run_git("rev-parse", "--verify", h + "^{commit}").strip()
        except RuntimeError:
            unresolved_hashes.append(h)
            continue
        files = run_git("show", "--name-only", "--format=", full_hash)
        resolved_files.update(f for f in files.splitlines() if f)

    result = {
        "source_hash": source_hash,
        "computed_at": datetime.now(timezone.utc).isoformat(),
        "cited_hashes": candidate_hashes,
        "unresolved_hashes": unresolved_hashes,
        "files": sorted(resolved_files),
        "registry_patterns": [e["pattern"] for e in registry],
    }
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    HOTZONE_CACHE_PATH.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def is_hot_zone(files, hotzone):
    hits = []
    exact = set(hotzone["files"])
    for f in files:
        if f in exact:
            hits.append(f)
            continue
        for pattern in hotzone["registry_patterns"]:
            if fnmatch.fnmatch(f, pattern):
                hits.append(f)
                break
    return sorted(set(hits))


def classify_interest(subject):
    for category, config in INTEREST_CATEGORIES.items():
        if config["pattern"].search(subject):
            return category, config
    return None, None


def load_ledger():
    if not LEDGER_PATH.exists() or LEDGER_PATH.stat().st_size == 0:
        return []
    entries = []
    for line in LEDGER_PATH.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            entries.append(json.loads(line))
    return entries


def load_meta():
    if not META_PATH.exists():
        return {
            "schema_version": 1,
            "last_synced_upstream_hash": None,
            "hotzone_source_hash": None,
            "entry_count": 0,
            "updated_at": None,
        }
    return json.loads(META_PATH.read_text(encoding="utf-8"))


def save_meta(meta):
    META_PATH.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


def candidate_commit_list(since_ref=None):
    """Non-merge commits from the merge base (or --since ref) up to upstream,
    oldest first."""
    if since_ref:
        base = since_ref
    else:
        base = run_git(
            "merge-base", LOCAL_MERGE_BRANCH, UPSTREAM_REMOTE_BRANCH
        ).strip()
    log = run_git(
        "log",
        "--no-merges",
        "--reverse",
        "--format=%H%x1f%ad%x1f%s",
        "--date=iso-strict",
        f"{base}..{UPSTREAM_REMOTE_BRANCH}",
    )
    commits = []
    for line in log.splitlines():
        if not line:
            continue
        full_hash, date, subject = line.split("\x1f", 2)
        commits.append({"hash": full_hash, "date": date, "subject": subject})
    return commits


def get_cherry_picked_hashes(base_ref):
    """Return the set of upstream commit hashes that already have patch-id
    equivalents on base_ref. Uses `git cherry` which applies --cherry-pick
    detection via patch-id comparison — catches commits that were cherry-picked
    with a different hash (e.g. due to conflict resolution)."""
    merge_base = run_git("merge-base", base_ref, UPSTREAM_REMOTE_BRANCH).strip()
    try:
        output = run_git(
            "cherry", "-v", base_ref, UPSTREAM_REMOTE_BRANCH, merge_base
        )
    except RuntimeError:
        return set()
    hashes = set()
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("- "):
            # '- <hash>' means the commit has an equivalent on base_ref
            parts = line.split()
            if len(parts) >= 2:
                hashes.add(parts[1])
    return hashes


def _build_auto_rationale(prefix, subject, files):
    tail = (
        f"Touches: {', '.join(files[:5])}"
        + (f" (+{len(files) - 5} more)" if len(files) > 5 else "")
    )
    return f"{prefix} {subject[:100]}. {tail}"


def cmd_hotzone(args):
    hz = derive_hotzone(refresh=args.refresh)
    print(json.dumps(hz, indent=2))
    print(
        f"\n{len(hz['files'])} hot-zone files derived from "
        f"{len(hz['cited_hashes'])} cited hashes "
        f"({len(hz['unresolved_hashes'])} unresolved) "
        f"+ {len(hz['registry_patterns'])} patterns from hotzones.jsonl.",
        file=sys.stderr,
    )


def cmd_hotzone_add(args):
    entry = {
        "pattern": args.pattern,
        "is_glob": args.glob or ("*" in args.pattern or "?" in args.pattern),
        "reason": args.reason,
        "added_at": datetime.now(timezone.utc).isoformat(),
        "added_via": args.added_via,
        "related_commit": args.related_commit,
    }
    added = append_hotzone_registry_entry(entry)
    if added:
        print(f"added hot-zone pattern: {args.pattern}")
    else:
        print(f"pattern already present, not re-added: {args.pattern}")


def cmd_pending(args):
    hotzone = derive_hotzone(refresh=False)
    ledger = load_ledger()
    decided_hashes = {e["hash"] for e in ledger}
    cherry_picked = get_cherry_picked_hashes(LOCAL_MERGE_BRANCH)

    commits = candidate_commit_list(since_ref=args.since)
    already_cherry_picked = []
    auto_skip = []
    interest_perf = []
    interest_modding = []
    interest_graphics = []
    interest_infra = []
    needs_review = []

    for c in commits:
        if c["hash"] in decided_hashes:
            continue
        files_raw = run_git("show", "--name-only", "--format=", c["hash"])
        files = [f for f in files_raw.splitlines() if f]
        hz_files = is_hot_zone(files, hotzone)

        record = {
            "hash": c["hash"],
            "short_hash": c["hash"][:8],
            "date": c["date"],
            "subject": c["subject"][:120],
            "hot_zone": bool(hz_files),
            "hot_zone_files": hz_files,
            "files": files,
            "interest_category": None,
        }

        # Already cherry-picked onto merge-upstream — no need to re-triage.
        # Detected via git cherry's patch-id equivalence, which catches
        # commits that landed with a different hash (e.g. after conflict
        # resolution during manual cherry-pick).
        if c["hash"] in cherry_picked:
            record["verdict"] = "skip"
            record["rationale"] = (
                f"Already cherry-picked onto merge-upstream (detected via "
                f"patch-id equivalence). No re-triage needed."
            )
            record["decision_mode"] = "auto"
            already_cherry_picked.append(record)
            continue

        # Hot-zone hits always go to manual review, regardless of subject match.
        # This protects Old World's deliberate divergence areas from accidental
        # auto-classification.
        if hz_files:
            needs_review.append(record)
            continue

        category, config = classify_interest(c["subject"])
        if category:
            rationale = _build_auto_rationale(config["rationale_prefix"], c["subject"], files)
            record["interest_category"] = category
            record["verdict"] = config["auto_verdict"]
            record["rationale"] = rationale
            record["decision_mode"] = "auto"

            if category == "graphics":
                interest_graphics.append(record)
            elif category == "perf":
                interest_perf.append(record)
            elif category == "modding":
                interest_modding.append(record)
            elif category == "infra":
                interest_infra.append(record)
        else:
            record["verdict"] = "skip"
            record["rationale"] = (
                f"No interest-category match (perf/modding/graphics/infra) "
                f"and no hot-zone overlap. Auto-skipped per Old World's "
                f"debloat-only triage philosophy. "
                f"Touches: {', '.join(files[:5])}"
                + (f" (+{len(files) - 5} more)" if len(files) > 5 else "")
            )
            record["decision_mode"] = "auto"
            auto_skip.append(record)

    new_count = (
        len(already_cherry_picked) + len(auto_skip)
        + len(interest_perf) + len(interest_modding)
        + len(interest_graphics) + len(interest_infra)
        + len(needs_review)
    )

    print(
        json.dumps(
            {
                "total_new": new_count,
                "already_cherry_picked": already_cherry_picked,
                "auto_skip": auto_skip,
                "interest_perf": interest_perf,
                "interest_modding": interest_modding,
                "interest_infra": interest_infra,
                "graphics_review": interest_graphics,
                "needs_review": needs_review,
            },
            indent=2,
        )
    )


def cmd_append(args):
    raw = sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")
    raw = raw.strip()
    if not raw:
        print("nothing to append", file=sys.stderr)
        return
    try:
        records = json.loads(raw)
        if isinstance(records, dict):
            records = [records]
    except json.JSONDecodeError:
        records = [json.loads(line) for line in raw.splitlines() if line.strip()]

    required = {
        "hash", "short_hash", "date", "subject", "verdict", "rationale",
        "hot_zone", "hot_zone_files", "decision_mode", "interest_category",
    }
    run_id = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    existing = load_ledger()
    existing_hashes = {e["hash"] for e in existing}

    new_lines = []
    for r in records:
        missing = required - r.keys()
        if missing:
            raise ValueError(f"record {r.get('hash', '?')} missing fields: {missing}")
        if r["hash"] in existing_hashes:
            continue
        r.setdefault("applied", False)
        r.setdefault("applied_date", None)
        r.setdefault("run_id", run_id)
        new_lines.append(json.dumps(r))
        existing_hashes.add(r["hash"])

    if new_lines:
        with LEDGER_PATH.open("a", encoding="utf-8") as f:
            for line in new_lines:
                f.write(line + "\n")

    meta = load_meta()
    all_entries = load_ledger()
    meta["entry_count"] = len(all_entries)
    meta["hotzone_source_hash"] = derive_hotzone()["source_hash"]

    decided = {e["hash"] for e in all_entries}
    commits = candidate_commit_list()
    marker = meta.get("last_synced_upstream_hash")
    for c in commits:
        if c["hash"] in decided:
            marker = c["hash"]
        else:
            break
    meta["last_synced_upstream_hash"] = marker
    meta["updated_at"] = datetime.now(timezone.utc).isoformat()
    save_meta(meta)

    print(f"appended {len(new_lines)} new ledger entries ({len(records) - len(new_lines)} already present)")


def cmd_render(args):
    ledger = load_ledger()
    if args.verdict:
        ledger = [e for e in ledger if e["verdict"] == args.verdict]
    if args.category:
        ledger = [e for e in ledger if e.get("interest_category") == args.category]
    if not ledger:
        print("(no matching ledger entries)")
        return
    print("| hash | date | subject | verdict | interest | hot_zone | rationale |")
    print("|---|---|---|---|---|---|---|")
    for e in ledger:
        interest = e.get("interest_category") or "-"
        print(
            f"| {e['short_hash']} | {e['date'][:10]} | {e['subject'][:60]} "
            f"| {e['verdict']} | {interest} | {'yes' if e['hot_zone'] else 'no'} "
            f"| {e['rationale'][:100]} |"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_hotzone = sub.add_parser("hotzone")
    p_hotzone.add_argument("--refresh", action="store_true")
    p_hotzone.set_defaults(func=cmd_hotzone)

    p_hotzone_add = sub.add_parser("hotzone-add")
    p_hotzone_add.add_argument("pattern", help="Exact file path or glob pattern")
    p_hotzone_add.add_argument("--reason", required=True)
    p_hotzone_add.add_argument("--glob", action="store_true", help="Force is_glob=true regardless of pattern content")
    p_hotzone_add.add_argument("--related-commit", default=None)
    p_hotzone_add.add_argument(
        "--added-via", default="triage-session",
        choices=["triage-session", "manual", "seed"],
    )
    p_hotzone_add.set_defaults(func=cmd_hotzone_add)

    p_pending = sub.add_parser("pending")
    p_pending.add_argument("--since", default=None)
    p_pending.set_defaults(func=cmd_pending)

    p_append = sub.add_parser("append")
    p_append.add_argument("file", help="JSON file with record(s), or - for stdin")
    p_append.set_defaults(func=cmd_append)

    p_render = sub.add_parser("render")
    p_render.add_argument("--verdict", default=None, choices=["take", "skip", "review"])
    p_render.add_argument("--category", default=None, choices=["perf", "modding", "graphics", "infra"])
    p_render.set_defaults(func=cmd_render)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
