#!/usr/bin/env python3
"""Mechanical helpers for the upstream-merge-gamedata skill.

Subcommands:
  set-path <abs-path>     One-time setup: store the absolute path to the
                           private gamedata tree (the directory whose
                           relative structure mirrors this repo's gamedata/,
                           e.g. .../OldWorldRepo/_GAME/gamedata) in the
                           gitignored pointer file. Overwrites any existing
                           value.
  show-path                Print the currently configured private path, or
                           exit non-zero with a setup hint if unconfigured.
  pending [--limit N]      Cross-reference the shared upstream-merge ledger
                           (verdict=take) against this skill's own
                           gamedata-ledger.jsonl: commits that touch
                           gamedata/ and haven't been dispositioned yet,
                           oldest-first.
  record FILE|-            Given records (JSON array of {"hash":..,
                           "disposition":.., "notes":.., "engine_files":[..]})
                           append/upsert them into gamedata-ledger.jsonl.
                           Validated: disposition must be a known value,
                           engine_files must all start with "gamedata/", and
                           no string field may contain the configured
                           private root path - the mechanical half of never
                           letting the private location leak into a file
                           this repo tracks.
  render [--disposition D] Print a Markdown summary table of the gamedata
                           ledger.

This script never writes anything outside .agents/upstream-merge/ (the
shared, tracked ledger area) except the gitignored pointer file itself. It
never reads or writes inside the private gamedata tree - that happens in
the skill directly (Read/Write tools), never through this script, so the
one thing this script is trusted to enforce (no leaked path) stays a single
choke point.
"""
import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
UPSTREAM_MERGE_DIR = REPO_ROOT / ".agents" / "upstream-merge"
SHARED_LEDGER_PATH = UPSTREAM_MERGE_DIR / "ledger" / "upstream-merge-ledger.jsonl"
GAMEDATA_LEDGER_PATH = UPSTREAM_MERGE_DIR / "gamedata-ledger.jsonl"
POINTER_PATH = UPSTREAM_MERGE_DIR / "gamedata-review.local.json"

DISPOSITIONS = {
    "ported",          # applied to the private tree as-is (or trivially adapted), done
    "adapted",         # applied with a deliberate manual adaptation, done
    "not_applicable",  # out of scope for the mod (vanilla/legacy-only content etc.), no action
    "skipped",         # in scope but deliberately not ported (explain why in notes)
    "needs_followup",  # looked at, not resolved yet - revisit next session
}


def run_git(*args):
    result = subprocess.run(
        ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, check=True
    )
    return result.stdout


def load_jsonl(path):
    if not path.exists() or path.stat().st_size == 0:
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def save_jsonl(path, entries):
    with path.open("w", encoding="utf-8") as f:
        for e in entries:
            f.write(json.dumps(e) + "\n")


def load_pointer():
    if not POINTER_PATH.exists():
        return None
    data = json.loads(POINTER_PATH.read_text(encoding="utf-8"))
    return data.get("private_gamedata_root")


def cmd_set_path(args):
    path = Path(args.path).expanduser().resolve()
    if not path.is_dir():
        print(f"error: {path} is not a directory", file=sys.stderr)
        sys.exit(1)
    UPSTREAM_MERGE_DIR.mkdir(parents=True, exist_ok=True)
    POINTER_PATH.write_text(
        json.dumps(
            {
                "private_gamedata_root": str(path),
                "configured_at": datetime.now(timezone.utc).isoformat(),
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"stored private gamedata root ({POINTER_PATH} is gitignored - never committed)")


def cmd_show_path(args):
    root = load_pointer()
    if not root:
        print(
            "not configured - run "
            "`gamedata_helpers.py set-path <abs-path-to-private-gamedata-dir>` first",
            file=sys.stderr,
        )
        sys.exit(1)
    print(root)


def gamedata_files_for(commit_hash):
    try:
        files = run_git(
            "diff-tree", "--no-commit-id", "--name-only", "-r", commit_hash
        ).splitlines()
    except subprocess.CalledProcessError:
        return []
    return [f for f in files if f.startswith("gamedata/")]


def cmd_pending(args):
    shared = load_jsonl(SHARED_LEDGER_PATH)
    take = [e for e in shared if e["verdict"] == "take"]

    dispositioned = {e["hash"] for e in load_jsonl(GAMEDATA_LEDGER_PATH)}

    pending = []
    for e in take:
        if e["hash"] in dispositioned:
            continue
        files = gamedata_files_for(e["hash"])
        if not files:
            continue
        pending.append(
            {
                "hash": e["hash"],
                "short_hash": e["short_hash"],
                "date": e["date"],
                "subject": e["subject"],
                "engine_files": files,
            }
        )

    pending.sort(key=lambda e: datetime.fromisoformat(e["date"]))
    if args.limit:
        pending = pending[: args.limit]
    print(json.dumps(pending, indent=2))


def validate_record(r, private_root):
    if r.get("disposition") not in DISPOSITIONS:
        raise ValueError(f"disposition {r.get('disposition')!r} not in {sorted(DISPOSITIONS)}")
    engine_files = r.get("engine_files") or []
    for f in engine_files:
        if not f.startswith("gamedata/"):
            raise ValueError(f"engine_files entry {f!r} doesn't start with 'gamedata/'")
    if private_root:
        haystack = json.dumps(r)
        if private_root in haystack:
            raise ValueError(
                "record contains the configured private gamedata root path - "
                "refusing to write it into a tracked file"
            )


def cmd_record(args):
    raw = sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")
    records = json.loads(raw.strip())
    if isinstance(records, dict):
        records = [records]

    private_root = load_pointer()
    for r in records:
        try:
            validate_record(r, private_root)
        except ValueError as exc:
            print(f"invalid record for {r.get('hash')}: {exc}", file=sys.stderr)
            sys.exit(1)

    now = datetime.now(timezone.utc).isoformat()
    entries = load_jsonl(GAMEDATA_LEDGER_PATH)
    by_hash = {e["hash"]: e for e in entries}
    for r in records:
        by_hash[r["hash"]] = {
            "hash": r["hash"],
            "short_hash": r.get("short_hash", r["hash"][:8]),
            "subject": r.get("subject", ""),
            "engine_files": r.get("engine_files", []),
            "disposition": r["disposition"],
            "notes": r.get("notes", ""),
            "decided_date": now,
        }
    save_jsonl(GAMEDATA_LEDGER_PATH, list(by_hash.values()))
    print(f"recorded {len(records)} disposition(s)")


def cmd_render(args):
    entries = load_jsonl(GAMEDATA_LEDGER_PATH)
    if args.disposition:
        entries = [e for e in entries if e["disposition"] == args.disposition]
    entries.sort(key=lambda e: e["decided_date"])
    print("| hash | disposition | subject | engine files |")
    print("|---|---|---|---|")
    for e in entries:
        files = ", ".join(e["engine_files"])
        print(f"| `{e['short_hash']}` | {e['disposition']} | {e['subject']} | {files} |")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_set = sub.add_parser("set-path")
    p_set.add_argument("path")
    p_set.set_defaults(func=cmd_set_path)

    p_show = sub.add_parser("show-path")
    p_show.set_defaults(func=cmd_show_path)

    p_pending = sub.add_parser("pending")
    p_pending.add_argument("--limit", type=int, default=None)
    p_pending.set_defaults(func=cmd_pending)

    p_record = sub.add_parser("record")
    p_record.add_argument("file", help="JSON array (or single object) of disposition records, or - for stdin")
    p_record.set_defaults(func=cmd_record)

    p_render = sub.add_parser("render")
    p_render.add_argument("--disposition", choices=sorted(DISPOSITIONS), default=None)
    p_render.set_defaults(func=cmd_render)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
