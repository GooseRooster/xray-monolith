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
    # Parse to aware datetimes rather than sorting the raw strings - commit
    # authors span multiple UTC offsets, and plain string comparison of
    # ISO8601 timestamps only sorts correctly when every entry shares the
    # same offset. Correct ordering here is what the ordering_after check in
    # cmd_plan relies on.
    pending.sort(key=lambda e: datetime.fromisoformat(e["date"]))
    return pending


def cmd_pending(args):
    pending = pending_entries()
    if args.limit:
        pending = pending[: args.limit]
    print(json.dumps(pending, indent=2))


def cmd_plan(args):
    all_entries = load_ledger()
    by_hash = {e["hash"]: e for e in all_entries}

    full_pending = pending_entries()
    full_index = {e["hash"]: i for i, e in enumerate(full_pending)}

    pending = full_pending[: args.limit] if args.limit else full_pending
    batch_index = {e["hash"]: i for i, e in enumerate(pending)}
    if not pending:
        print("(no pending 'take' entries - nothing to apply)")
        return

    hot = [e for e in pending if e["hot_zone"]]
    plain = [e for e in pending if not e["hot_zone"]]
    unreviewed = [e for e in pending if not e.get("reviewed", False)]

    # Ordering-dependency check: does every ordering_after prerequisite for a
    # commit in this batch either already be applied, or appear earlier in
    # this same batch? If not, cherry-picking this batch in isolation would
    # violate a dependency review surfaced - most likely because --limit cut
    # the batch short of the prerequisite.
    violations = []
    for e in pending:
        for flag in e.get("review_flags", []):
            if flag.get("type") != "ordering_after":
                continue
            for prereq_hash in flag.get("after", []):
                prereq = by_hash.get(prereq_hash)
                if prereq is None:
                    violations.append((e, prereq_hash, "prerequisite hash not found in ledger at all"))
                    continue
                if prereq.get("applied"):
                    continue
                if prereq_hash in batch_index and batch_index[prereq_hash] < batch_index[e["hash"]]:
                    continue
                if prereq_hash in batch_index:
                    violations.append((e, prereq_hash, "prerequisite is in this batch but ordered AFTER the dependent commit - ledger date order is broken"))
                elif prereq.get("verdict") != "take":
                    violations.append((e, prereq_hash, f"prerequisite verdict is '{prereq.get('verdict')}', not 'take' - will never apply"))
                else:
                    where = full_index.get(prereq_hash)
                    hint = f"pending, position {where + 1} of {len(full_pending)} in full queue" if where is not None else "pending, not found in take/unapplied queue"
                    violations.append((e, prereq_hash, f"prerequisite not applied and not in this batch ({hint}) - likely cut off by --limit"))

    print(f"# Upstream apply plan - {len(pending)} commit(s)\n")

    if violations:
        print(f"## ⚠️  Ordering-dependency violations ({len(violations)}) - resolve before approving\n")
        for e, prereq_hash, reason in violations:
            prereq = by_hash.get(prereq_hash)
            prereq_label = f"`{prereq['short_hash']}` ({prereq['subject']})" if prereq else f"`{prereq_hash[:8]}` (unknown)"
            print(f"- `{e['short_hash']}` ({e['subject']}) requires {prereq_label} first: {reason}")
        print(
            "\nFix by raising `--limit` to include the prerequisite, deferring "
            "the dependent commit to a later batch, or (if the prerequisite "
            "was wrongly flagged) revisiting the review flag. Do not cherry-pick "
            "the dependent commit ahead of its prerequisite.\n"
        )

    if unreviewed:
        print(
            f"Note: {len(unreviewed)}/{len(pending)} of these have not been "
            "through `upstream-merge-review` yet (informational only, not a "
            "blocker - see that skill if you want deeper explanation before "
            "landing them).\n"
        )
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
                + _flags_suffix(e)
            )
        print()
    if plain:
        print(f"## Remaining commits ({len(plain)})\n")
        for e in plain:
            print(f"- `{e['short_hash']}` {e['date'][:10]} - {e['subject']}" + _flags_suffix(e))

    _print_flag_section(pending, "playtest", "Playtest reminders", "carry into the final report after landing")
    _print_flag_section(pending, "bug_found", "Known bugs found during review",
                         "not a reason to skip - fix opportunistically if you're already touching the file, "
                         "otherwise carry into the final report as a follow-up")
    _print_flag_section(pending, "dangling_reference", "Dangling references flagged during review",
                         "re-verify before cherry-picking (a later commit may have already cleaned it up) - "
                         "if still dangling, note it in the final report rather than landing it silently")

    print(
        f"\n## Ordered cherry-pick list\n\n"
        + "\n".join(f"{i + 1}. `{e['hash']}` - {e['subject']}" for i, e in enumerate(pending))
    )


def _print_flag_section(pending, flag_type, heading, subtitle):
    matches = [e for e in pending if any(f.get("type") == flag_type for f in e.get("review_flags", []))]
    if not matches:
        return
    print(f"\n## {heading} ({len(matches)}) - {subtitle}\n")
    for e in matches:
        for f in e.get("review_flags", []):
            if f.get("type") == flag_type:
                print(f"- `{e['short_hash']}` {e['subject']}: {f.get('note', '')}")


def _flags_suffix(e):
    flags = [f for f in e.get("review_flags", []) if f.get("type") != "playtest"]
    if not flags:
        return ""
    parts = []
    for f in flags:
        t = f["type"]
        if t == "ordering_after":
            parts.append(f"ordering_after {', '.join(h[:8] for h in f.get('after', []))}")
        elif t == "hotzone_gap":
            parts.append(f"hotzone_gap ({f.get('pattern', '')}): {f.get('note', '')}")
        elif t in ("bug_found", "dangling_reference"):
            parts.append(f"{t}: {f.get('note', '')}")
        else:
            parts.append(t)
    return "\n  - review flags: " + "; ".join(parts)


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
