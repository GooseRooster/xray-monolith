---
name: upstream-merge-gamedata
description: Ports upstream gamedata changes to the private Old World gamedata repository
---

# Upstream Merge Gamedata

# Upstream merge gamedata

The fourth skill in the upstream-merge system, sitting alongside
`upstream-merge-triage`/`-review`/`-apply` but pointed at a different
problem: `gamedata/` in *this* repo is the engine's own bundled/distribution
copy, not the mod's actively-developed tree (see `PROJECT.md`'s
"Relationship to the game repo"). When an upstream commit changes something
under `gamedata/`, that change usually also needs to be manually ported
into the private Old World gamedata repo - this skill finds those commits,
compares the two sides, and drafts (then, on approval, executes) the port.

**Privacy boundary - read this before doing anything else in this skill.**
The private gamedata tree's location lives in one gitignored file,
`.agents/upstream-merge/gamedata-review.local.json`, written only by
`gamedata_helpers.py set-path` and never by hand. Nothing this skill writes
to a file *this repo tracks* - the `gamedata-ledger.jsonl`, this SKILL.md,
anything under `references/` - may ever contain the private root path, a
private-tree-relative file path, or private file content. Chat output
during the session will necessarily reference private content (that's the
whole point - you can't compare against something you can't see), which is
fine; the constraint is specifically about what lands in a tracked file.
`gamedata_helpers.py record` mechanically rejects any disposition record
containing the configured private root path as a second line of defense -
see `references/gamedata-protocol.md` for the full rule set.

## Steps

1. **Confirm setup**:
    ```
    python3 .agents/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py show-path
    ```
   If unconfigured, ask the user for the absolute path to their private
   gamedata tree (the directory whose relative structure mirrors this
   repo's `gamedata/` - e.g. `.../OldWorldRepo/_GAME/gamedata`), then run
   `set-path <path>` yourself. This is a one-time step per machine.

2. **Query pending commits**:
    ```
    python3 .agents/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py pending [--limit N]
    ```
   Returns shared-ledger `take` commits that touch `gamedata/` and haven't
   been dispositioned by this skill yet, oldest-first, each with its
   `engine_files` (this repo's own `gamedata/...` paths - safe to name,
   they're public). If empty, report that and stop.

3. **Compare each commit's touched files** against the private tree per
   `references/gamedata-protocol.md`'s classification rules (clean port /
   already ported / diverged, needs adaptation / private file doesn't
   exist - possibly out of scope). Batch the out-of-scope questions
   (`references/gamedata-protocol.md` has the exact `AskUserQuestion`
   shape) and get those answered *before* drafting the plan, so the plan
   reflects settled scope rather than open questions.

4. **Draft the plan** - required here because this skill can write into a repo 
    outside this one. The plan (see `references/gamedata-protocol.md` for exact 
    content) lists every file's classification and, for anything going to be 
    written, the specific change. Get approval through plan-mode flow.

5. **On approval, execute** per
   `references/gamedata-protocol.md`'s execution rules: clean ports go in
   confirmed batches, files needing adaptation get walked one at a time
   with a proposed resolution confirmed before writing - this skill never
   runs `git add`/`commit` inside the private tree, only file writes;
   committing there is the user's own deliberate step.

6. **Record dispositions** for everything resolved this session:
    ```
    python3 .agents/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py record <file-with-records>
    ```

7. **Final report**: counts by disposition, anything left `needs_followup`,
   and an explicit reminder that the private repo's git state (staging,
   committing) hasn't been touched - only its working-tree files.

## Alternative Approach: File-Based Processing

For large merge sessions with many commits touching the same files, consider a **file-based approach** instead of commit-by-commit:

1. **Identify unique files**: Get all unique files touched by pending commits (excluding out-of-scope like Russian localization)
2. **Get final upstream state**: For each file, get the content from the latest upstream commit that modifies it
3. **Compare with private tree**: Determine classification (clean port / needs adaptation / already ported / not applicable)
4. **Batch process by file**: Adapt each file as a whole rather than piecemeal across multiple commits

**Benefits**:
- Faster processing (avoid redundant work on same file across multiple commits)
- Better conflict resolution (see complete picture of all changes to a file)
- Easier to preserve Old World customizations
- More accurate progress tracking

**Implementation**:
```bash
# Get all unique files from pending commits
python3 .agents/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py pending | jq -r '.[] | .engine_files[]' | sort | uniq

# For each file, get latest upstream version
git show $(git log --oneline --follow -- <file> | head -1 | cut -d' ' -f1):<file>
```

## Additional resources

- `references/gamedata-protocol.md` - file classification rules, the
  out-of-scope question shape, plan content, execution/confirmation rules,
  and the full privacy rule set.
