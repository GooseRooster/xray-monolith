# Upstream Merge Review

# Upstream merge review

Sits between `upstream-merge-triage` (decides take/skip/review) and
`upstream-merge-apply` (executes the cherry-picks). Triage's rationale is
deliberately terse - it has to get through hundreds of commits. This skill
goes back over the accepted (`take`) backlog at real depth, grouped by
subsystem rather than commit order, so you actually understand what's about
to land on `merge-upstream` and can flag gotchas before it does - not
after a conflict or a surprising gameplay change shows up mid-apply.

**Read-only with respect to git state** (aside from read-only `git show`/
`git diff-tree` calls to inspect commits). The only thing it writes is the
shared ledger (`reviewed`/`reviewed_date`/`review_notes` fields, and
occasionally a `verdict` flip if review surfaces a reason to reconsider one)
and per-theme report files under `.agents/upstream-merge/reviews/`. It
never cherry-picks or commits code - see `upstream-merge-apply` for that.

## Steps

1. **Query the pending set**:
    ```
    python3 .agents/skills/upstream-merge-review/scripts/review_helpers.py pending [--limit N]
    ```
   Returns `verdict: take` ledger entries not yet `reviewed`, oldest-first.
   If empty, report that and stop - either nothing's been triaged yet, or
   everything pending has already been reviewed.

2. **Group thematically**:
    ```
    python3 .agents/skills/upstream-merge-review/scripts/review_helpers.py group [--limit N]
    ```
   Buckets the same set into subsystem themes (Rendering, Sound, AI/Combat,
   Player/first-person body, UI, Build/CI, Physics, Network, xrCore,
   Gamedata, Other/misc) via touched-file heuristics - see
   `references/review-protocol.md` for why thematic beats chronological
   here, and the exact rules. Themes are pre-ordered riskiest-first (by
   hot-zone-entry count, then size).

3. **Pick a theme for this session** - if the user passed `--theme`, use
   it; otherwise propose the top 1-2 themes from step 2's ordering and ask
   which to do this session (300 commits doesn't fit in one sitting; doing
   one coherent theme per session is the point).

4. **Review the theme's batch** following `references/review-protocol.md`:
   depth proportional to risk (full diff read + gotcha checklist for
   hot-zone entries and anything else that looks divergence-adjacent;
   batch-level summary for the rest, unless something in a stat/message
   looks off). Explicitly run the gotcha checklist from that reference for
   anything reviewed in depth.

5. **Write and render the findings**: save the report per
   `references/review-protocol.md`'s format to
    `.agents/upstream-merge/reviews/<theme-slug>-<date>.md`, and render the
   same content directly in the chat reply - don't make the user open the
   file to see what this session found.

6. **Confirm the batch** via the single `AskUserQuestion` shape in
   `references/review-protocol.md`. Apply any verdict flips with:
    ```
    python3 .agents/skills/upstream-merge-review/scripts/review_helpers.py revise <hash> --verdict take|skip|review --rationale "..."
    ```
    and any approved hot-zone additions via the triage skill's own
    `hotzone-add` (reused, not duplicated - see the reference). Then mark
    the batch reviewed:
    ```
    python3 .agents/skills/upstream-merge-review/scripts/review_helpers.py mark-reviewed <file-with-hash/notes-records>
    ```

7. **Final report**: how many commits reviewed this session, how many
   `take`/unreviewed remain (and in which themes), any verdict flips or
   hot-zone additions made, and a pointer to `upstream-merge-apply` once a
   theme (or the whole backlog) is reviewed and ready to land.

## Additional resources

- `references/review-protocol.md` - the thematic grouping rationale, the
  gotcha checklist, the report file format, and the exact confirmation flow.
