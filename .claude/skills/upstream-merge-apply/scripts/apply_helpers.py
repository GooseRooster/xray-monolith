#!/usr/bin/env python3
"""Mechanical helpers for the upstream-merge-apply skill.

Subcommands:
  pending                 List ledger entries with verdict=take, applied=false,
                           sorted oldest-first (cherry-pick order).
  plan [--limit N]        Render a Markdown draft plan for the pending set,
                           for pasting into the plan-mode plan file.
  mark-applied FILE|-      Given hashes (JSON array or one-per-line) that were
                           successfully cherry-picked, set applied=true and
                           applied_date on those ledger entries in place.

This script never touches git state - it only reads/writes the shared ledger.
All actual `git cherry-pick`/`git commit` calls are run directly by the skill
(never by this script), only after the plan-mode approval gate.
"""
import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
LEDGER_DIR = REPO_ROOT / ".claude" / "upstream-merge" / "ledger"
LEDGER_PATH = LEDGER_DIR / "upstream-merge-ledger.jsonl"
META_PATH = LEDGER_DIR / "upstream-merge-ledger.meta.json"


def load_ledger():
    if not LEDGER_PATH.exists() or LEDGER_PATH.stat().st_size == 0:
        return []
    entries = []
    for line in LEDGER_PATH.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            entries.append(json.loads(line))
    return entries


def save_ledger(entries):
    with LEDGER_PATH.open("w", encoding="utf-8") as f:
        for e in entries:
            f.write(json.dumps(e) + "\n")


def pending_entries():
    entries = load_ledger()
    pending = [e for e in entries if e["verdict"] == "take" and not e["applied"]]
    pending.sort(key=lambda e: e["date"])
    return pending


def cmd_pending(args):
    pending = pending_entries()
    if args.limit:
        pending = pending[: args.limit]
    print(json.dumps(pending, indent=2))


def cmd_plan(args):
    pending = pending_entries()
    if args.limit:
        pending = pending[: args.limit]
    if not pending:
        print("(no pending 'take' entries - nothing to apply)")
        return

    hot = [e for e in pending if e["hot_zone"]]
    plain = [e for e in pending if not e["hot_zone"]]

    print(f"# Upstream apply plan - {len(pending)} commit(s)\n")
    print(
        "Cherry-pick each commit individually onto `merge-upstream`, oldest "
        "first, preserving original message and authorship (no squashing). "
        "Stop at the first conflict for manual resolution. Finish with one "
        "small trailing commit that updates the ledger's `applied`/"
        "`applied_date` fields for this batch.\n"
    )
    if hot:
        print(
            f"## Hot-zone commits ({len(hot)}) - reviewed and accepted despite "
            "touching a file we've customized; most likely conflict points\n"
        )
        for e in hot:
            print(
                f"- `{e['short_hash']}` {e['date'][:10]} - {e['subject']}\n"
                f"  - files: {', '.join(e['hot_zone_files'])}\n"
                f"  - why accepted: {e['rationale']}"
            )
        print()
    if plain:
        print(f"## Remaining commits ({len(plain)})\n")
        for e in plain:
            print(f"- `{e['short_hash']}` {e['date'][:10]} - {e['subject']}")
    print(
        f"\n## Ordered cherry-pick list\n\n"
        + "\n".join(f"{i + 1}. `{e['hash']}` - {e['subject']}" for i, e in enumerate(pending))
    )


def cmd_mark_applied(args):
    raw = sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")
    raw = raw.strip()
    try:
        hashes = json.loads(raw)
        if isinstance(hashes, str):
            hashes = [hashes]
    except json.JSONDecodeError:
        hashes = [line.strip() for line in raw.splitlines() if line.strip()]
    hashes = set(hashes)

    entries = load_ledger()
    now = datetime.now(timezone.utc).isoformat()
    updated = 0
    for e in entries:
        if e["hash"] in hashes and not e["applied"]:
            e["applied"] = True
            e["applied_date"] = now
            updated += 1
    save_ledger(entries)
    print(f"marked {updated} entries as applied")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_pending = sub.add_parser("pending")
    p_pending.add_argument("--limit", type=int, default=None)
    p_pending.set_defaults(func=cmd_pending)

    p_plan = sub.add_parser("plan")
    p_plan.add_argument("--limit", type=int, default=None)
    p_plan.set_defaults(func=cmd_plan)

    p_mark = sub.add_parser("mark-applied")
    p_mark.add_argument("file", help="JSON array or newline list of hashes, or - for stdin")
    p_mark.set_defaults(func=cmd_mark_applied)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
