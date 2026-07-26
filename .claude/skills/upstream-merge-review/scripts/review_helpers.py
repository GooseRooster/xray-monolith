#!/usr/bin/env python3
"""Mechanical helpers for the upstream-merge-review skill.

Subcommands:
  pending [--verdicts take,review] [--limit N]
                           List ledger entries matching the given verdicts
                           (default: take) that haven't been reviewed yet,
                           oldest-first.
  group [--verdicts take,review] [--limit N]
                           Same set as `pending`, bucketed into coarse
                           subsystem themes (Rendering, Sound, AI/Combat,
                           Player/body, UI, Build/CI, Gamedata, xrCore,
                           Physics, Network, Other/misc) by touched-file
                           heuristics. Themes are ordered by how many
                           hot-zone entries they contain (highest first),
                           then by size. This grouping is for organizing
                           review sessions only - it is NOT the hot-zone
                           detector and never affects verdicts.
  mark-reviewed FILE|-     Given records (JSON array of {"hash":..,"notes":..}
                           or a plain hash list) that were looked at this
                           session, set reviewed=true, reviewed_date, and
                           review_notes (if given) on those ledger entries.
  revise HASH --verdict V --rationale TEXT [--decision-mode MODE]
                           Change an existing entry's verdict/rationale in
                           place (e.g. a review session surfaces a reason to
                           flip take->skip). Updates run_id; decision_mode
                           defaults to "confirmed" (batch-approved via
                           AskUserQuestion, same as a fresh triage decision).

This script never touches git state except read-only `git diff-tree` calls
(to bucket commits by touched files for `group`). It only reads/writes the
shared ledger. All actual git operations remain in upstream-merge-apply.
"""
import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
LEDGER_DIR = REPO_ROOT / ".claude" / "upstream-merge" / "ledger"
LEDGER_PATH = LEDGER_DIR / "upstream-merge-ledger.jsonl"

THEME_RULES = [
    ("Rendering (R1-R4)", [
        "src/Layers/xrRenderPC_R4/", "src/Layers/xrRenderPC_R3/",
        "src/Layers/xrRenderPC_R2/", "src/Layers/xrRenderPC_R1/",
        "src/Layers/xrRenderDX9/", "src/Layers/xrRenderDX10/",
        "src/Layers/xrRender/", "gamedata/shaders/",
    ]),
    ("Sound / Steam Audio", ["src/xrSound/"]),
    ("AI / Combat", [
        "stalker_", "ai_stalker", "enemy_manager", "cover_evaluators",
        "danger_manager", "/ai/", "object_handler_planner", "agent_manager",
        "agent_enemy_manager", "visual_memory_manager", "stalker_search_planner",
    ]),
    ("Player / first-person body", [
        "Actor.cpp", "Actor.h", "ActorCondition", "player_hud", "HudItem",
        "hud_item", "PhysicsShellHolder", "actor_",
    ]),
    ("UI", ["src/xrGame/ui/", "gamedata/configs/text", "gamedata/configs/ui"]),
    ("Build / CI", [
        ".github/", ".sln", ".vcxproj", "batch_build.bat", "compressor/",
    ]),
    ("Physics", ["src/xrPhysics/", "physics_shell"]),
    ("Network / server entities", ["src/xrNetServer", "src/xrServerEntities"]),
    ("xrCore / low-level", ["src/xrCore/"]),
    ("Gamedata (bundled/distribution)", ["gamedata/"]),
]


def run_git(*args):
    result = subprocess.run(
        ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, check=True
    )
    return result.stdout


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


def pending_entries(verdicts):
    entries = load_ledger()
    pending = [
        e for e in entries
        if e["verdict"] in verdicts and not e.get("reviewed", False)
    ]
    pending.sort(key=lambda e: e["date"])
    return pending


def parse_verdicts(raw):
    return {v.strip() for v in raw.split(",") if v.strip()}


def cmd_pending(args):
    pending = pending_entries(parse_verdicts(args.verdicts))
    if args.limit:
        pending = pending[: args.limit]
    print(json.dumps(pending, indent=2))


def theme_for_files(files):
    for theme, patterns in THEME_RULES:
        for f in files:
            if any(p in f for p in patterns):
                return theme
    return "Other / misc"


def cmd_group(args):
    pending = pending_entries(parse_verdicts(args.verdicts))
    if args.limit:
        pending = pending[: args.limit]

    groups = {}
    for e in pending:
        try:
            files = run_git(
                "diff-tree", "--no-commit-id", "--name-only", "-r", e["hash"]
            ).splitlines()
        except subprocess.CalledProcessError:
            files = []
        theme = theme_for_files(files)
        groups.setdefault(theme, []).append(e)

    # Order themes by hot-zone-entry count (descending), then size (descending).
    def sort_key(item):
        _, entries = item
        hot = sum(1 for e in entries if e.get("hot_zone"))
        return (-hot, -len(entries))

    ordered = dict(sorted(groups.items(), key=sort_key))
    print(json.dumps(ordered, indent=2))


def cmd_mark_reviewed(args):
    raw = sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")
    raw = raw.strip()
    try:
        records = json.loads(raw)
        if isinstance(records, str):
            records = [{"hash": records}]
        elif isinstance(records, list) and records and isinstance(records[0], str):
            records = [{"hash": h} for h in records]
    except json.JSONDecodeError:
        records = [{"hash": line.strip()} for line in raw.splitlines() if line.strip()]

    notes_by_hash = {r["hash"]: r.get("notes") for r in records}

    entries = load_ledger()
    now = datetime.now(timezone.utc).isoformat()
    updated = 0
    for e in entries:
        if e["hash"] in notes_by_hash:
            e["reviewed"] = True
            e["reviewed_date"] = now
            e["review_notes"] = notes_by_hash[e["hash"]]
            updated += 1
    save_ledger(entries)
    print(f"marked {updated} entries as reviewed")


def cmd_revise(args):
    entries = load_ledger()
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    found = False
    for e in entries:
        if e["hash"] == args.hash or e["short_hash"] == args.hash:
            e["verdict"] = args.verdict
            e["rationale"] = args.rationale
            e["decision_mode"] = args.decision_mode
            e["run_id"] = now
            found = True
            break
    if not found:
        print(f"no ledger entry found for {args.hash}", file=sys.stderr)
        sys.exit(1)
    save_ledger(entries)
    print(f"revised {args.hash}: verdict -> {args.verdict}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_pending = sub.add_parser("pending")
    p_pending.add_argument("--verdicts", default="take")
    p_pending.add_argument("--limit", type=int, default=None)
    p_pending.set_defaults(func=cmd_pending)

    p_group = sub.add_parser("group")
    p_group.add_argument("--verdicts", default="take")
    p_group.add_argument("--limit", type=int, default=None)
    p_group.set_defaults(func=cmd_group)

    p_mark = sub.add_parser("mark-reviewed")
    p_mark.add_argument("file", help="JSON array of {hash,notes} or hashes, or - for stdin")
    p_mark.set_defaults(func=cmd_mark_reviewed)

    p_revise = sub.add_parser("revise")
    p_revise.add_argument("hash")
    p_revise.add_argument("--verdict", required=True, choices=["take", "skip", "review"])
    p_revise.add_argument("--rationale", required=True)
    p_revise.add_argument("--decision-mode", default="confirmed", choices=["auto", "confirmed", "manual"])
    p_revise.set_defaults(func=cmd_revise)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
