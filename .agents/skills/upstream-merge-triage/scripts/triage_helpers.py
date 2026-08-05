#!/usr/bin/env python3
"""Mechanical helpers for the upstream-merge-triage skill.

Subcommands:
  hotzone [--refresh]         Derive/cache the hot-zone file set from PROJECT.md
                               plus the persistent hotzones.jsonl registry.
  hotzone-add PATTERN         Append a new pattern to the hotzones registry
                               (--reason required, --glob, --related-commit).
  pending [--since REF]       List upstream commits not yet in the ledger, with
                               hot_zone/low_risk_message facts computed and
                               auto-take entries pre-built.
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

LOW_RISK_PATTERN = re.compile(
    r"\b(fix|typo|leak|crash|perf|optimi[sz]e|revert|null|sanitiz|clean ?up|refactor|warning)\b",
    re.IGNORECASE,
)

HASH_RE = re.compile(r"`([0-9a-f]{7,40})`")


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

    commits = candidate_commit_list(since_ref=args.since)
    auto_take = []
    needs_review = []
    for c in commits:
        if c["hash"] in decided_hashes:
            continue
        files_raw = run_git(
            "show", "--name-only", "--format=", c["hash"]
        )
        files = [f for f in files_raw.splitlines() if f]
        hz_files = is_hot_zone(files, hotzone)
        low_risk = bool(LOW_RISK_PATTERN.search(c["subject"]))
        record = {
            "hash": c["hash"],
            "short_hash": c["hash"][:8],
            "date": c["date"],
            "subject": c["subject"][:120],
            "hot_zone": bool(hz_files),
            "hot_zone_files": hz_files,
            "files": files,
        }
        if not hz_files and low_risk:
            record.update(
                {
                    "verdict": "take",
                    "rationale": (
                        f"No hot-zone overlap; message matches low-risk pattern. "
                        f"Touches: {', '.join(files[:5])}"
                        + (f" (+{len(files) - 5} more)" if len(files) > 5 else "")
                    ),
                    "decision_mode": "auto",
                }
            )
            auto_take.append(record)
        else:
            needs_review.append(record)

    print(
        json.dumps(
            {
                "total_new": len(commits) - sum(1 for c in commits if c["hash"] in decided_hashes),
                "auto_take": auto_take,
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
        "hot_zone", "hot_zone_files", "decision_mode",
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

    # Advance last_synced_upstream_hash to the last commit in the longest
    # fully-decided contiguous prefix of the candidate list.
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
    if not ledger:
        print("(no matching ledger entries)")
        return
    print("| hash | date | subject | verdict | hot_zone | rationale |")
    print("|---|---|---|---|---|---|")
    for e in ledger:
        print(
            f"| {e['short_hash']} | {e['date'][:10]} | {e['subject'][:60]} "
            f"| {e['verdict']} | {'yes' if e['hot_zone'] else 'no'} "
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
    p_render.set_defaults(func=cmd_render)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
