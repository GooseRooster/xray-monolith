---
name: upstream-merge-triage
description: Proposes take/skip/review verdicts for upstream commits and logs decisions to a hash-keyed ledger
---

# Upstream Merge Triage

# Upstream merge triage

Proposes take/skip/review verdicts for upstream commits this fork hasn't
decided on yet, and logs every decision to a hash-keyed ledger so future
runs only look at genuinely new commits. This skill is **read-only with
respect to git state** (aside from `git fetch`) and never cherry-picks or
commits anything - see the companion `upstream-merge-apply` skill for that.

`PROJECT.md`'s "Old World's intentional divergence from upstream" and "Color
grading, HDR and retro rendering options" sections are the living source of
truth for what this fork has deliberately customized. Never hardcode that
knowledge here - always re-derive it at runtime (step 2) so the two can't
drift apart.

The hot-zone set has a second, persistent source alongside `PROJECT.md`:
`.agents/upstream-merge/hotzones.jsonl`, a hand-curated registry for things
discovered *during* triage rather than derived from a cited commit. This
skill can propose additions to it (step 5) - always confirmed the same way
as a verdict, never silently appended - so the hot-zone set gets more
complete over time instead of staying frozen at whatever `PROJECT.md`
happened to cite when this skill was built.

## Steps

1. **Sync**: run `git fetch upstream all-in-one-vs2022-wpo`. If the current
   branch isn't `merge-upstream`, warn but continue - analysis is safe from
   anywhere.

2. **Derive the hot-zone set**:
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone
    ```
    This resolves every commit hash cited in `PROJECT.md`'s divergence
    section, unions the files each one touched, and adds every pattern
    currently in `.agents/upstream-merge/hotzones.jsonl` (seed backstop
    globs plus anything added by a previous triage session). Cached in
    `.agents/upstream-merge/.cache/hotzone.json`, keyed by a hash of both
    sources combined - it recomputes automatically the moment either
    `PROJECT.md` or `hotzones.jsonl` changes, no manual invalidation needed.

3. **Enumerate and pre-classify new commits**:
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py pending [--since <ref>]
    ```
   Returns `auto_take` (commits with no hot-zone overlap and an
   uncontroversial-looking message - already fully classified) and
   `needs_review` (everything else: any hot-zone hit, or no hot-zone hit but
   an ambiguous message). The script only computes objective facts
   (hot-zone overlap, message pattern, files touched) - it never proposes
   `skip` or `review` verdicts itself.

4. **Append the `auto_take` batch directly** (no confirmation needed - these
    are logged, not asked about):
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py append <file-with-auto_take-array>
    ```

5. **Classify the `needs_review` batch**: for each commit, read the actual
   diff (`git show <hash>`) - file-overlap alone is too coarse, see the
   `review`-verdict worked example in `references/ledger-schema.md` - and
       the relevant `PROJECT.md` paragraph if it's a hot-zone hit. Propose
   `take` / `skip` / `review` with a one-to-two sentence rationale that
   names the specific divergence area on conflict. Hot-zone hits should
   default toward `review` unless the diff is unambiguously unrelated to
   what was customized there.

   If a commit conflicts with something we've clearly customized but that
   *isn't* currently flagged `hot_zone` (i.e. it slipped through because
       nothing in `PROJECT.md` or the registry covers that file yet), also
   propose adding it to the hot-zone registry - as a separate, explicitly
   flagged suggestion alongside the verdict, never silently. Include the
   proposed pattern and a one-line reason in the batch table (step 6).

6. **Batch and confirm** - see `references/batching-protocol.md` for the
   exact grouping and `AskUserQuestion` shape, including how proposed
   hot-zone additions are shown and confirmed alongside verdicts. Apply any
   free-text corrections the user gives before finalizing a batch, then
    append the confirmed batch the same way as step 4, and for any confirmed
    hot-zone additions:
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add "<pattern>" --reason "<why>" --related-commit <hash>
    ```

7. **Final report**: counts (auto-take / confirmed take / skip / review),
   how many `take` entries are sitting at `applied: false` (point at
   `upstream-merge-apply` as the next step), and anything left at `review`
   with no resolution as an explicit follow-up.

## Additional resources

- `references/ledger-schema.md` - full field reference, worked examples for
  each verdict, and the append/meta-update mechanics.
- `references/batching-protocol.md` - exact batch size, grouping order, and
  `AskUserQuestion` option wording for step 6.
- `references/hotzones-schema.md` - the persistent hot-zone registry's
  schema, why it's separate from `PROJECT.md`, and how to add to it.
