# Upstream merge triage

Proposes take/skip/review verdicts for upstream commits this fork hasn't
decided on yet, and logs every decision to a hash-keyed ledger so future
runs only look at genuinely new commits. This skill is **read-only with
respect to git state** (aside from `git fetch`) and never cherry-picks or
commits anything - see the companion `upstream-merge-apply` skill for that.

**Philosophy**: Old World is opinionated and intentionally debloated. We
don't need every feature upstream adds. The triage default is **skip
everything** — we only flag commits matching one of four interest categories:

1. **Performance** — genuine optimisation work (multithreading, SIMD, cache,
   memory pools, throughput improvements) that benefits all players.
   Auto-verdict: `take`.
2. **Modding capabilities** — features that extend the engine's script/config
   surface for mod authors (DLTX, Lua bindings, XML hooks, config processing).
   Auto-verdict: `take`.
3. **Graphics** — new or improved rendering features (shaders, post-process,
   lighting, particles). Auto-verdict: `review`, *never* auto-taken, so they
   can be evaluated against Old World's deliberate visual vision.
4. **Infrastructure** — engine-core plumbing, build system, memory
   management, cross-compilation support, profiling/debugging tooling, RAII/
   smart-pointer safety wrappers. Auto-verdict: `take`.

Everything not matching one of these four categories is auto-skipped without
confirmation. A **safety-net agent** pass (step 8) audits the `auto_skip`
batch after each session for commits whose diffs contain interest-category
signals the subject-matching missed (e.g. a commit titled "cleanup" that
actually introduces new Lua bindings), so the keyword sets grow organically
without requiring pre-emptive diff reads of every auto-skipped commit.

**Project-vision guard**: commits that touch files in the hot-zone set
(derived from `PROJECT.md`'s divergence section) always route to manual
`needs_review` regardless of subject match — project-vision protection
trumps interest-category auto-classification. Hot-zone hits default toward
`review` or `skip` unless the diff is unambiguously unrelated to what was
customised there.

`PROJECT.md`'s "Old World's intentional divergence from upstream" and "Color
grading, HDR and retro rendering options" sections are the living source of
truth for what this fork has deliberately customized. Never hardcode that
knowledge here - always re-derive it at runtime (step 2) so the two can't
drift apart.

The hot-zone set has a second, persistent source alongside `PROJECT.md`:
`.agents/upstream-merge/hotzones.jsonl`, a hand-curated registry for things
discovered *during* triage rather than derived from a cited commit. This
skill can propose additions to it (step 6) - always confirmed the same way
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

3. **Enumerate and classify**:
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py pending [--since <ref>]
    ```
   Returns commits not yet in the ledger, split into these buckets:

   - `already_cherry_picked` — already cherry-picked onto `merge-upstream`,
     detected via `git cherry`'s patch-id equivalence. These commits landed
     with different hashes (e.g. after conflict resolution during manual
     cherry-pick) but their diffs are identical to upstream. Auto-skipped
     with no re-triage needed. Appended first (step 3b), no confirmation.
   - `auto_skip` — no interest-category match, no hot-zone overlap. The
     default bucket.
   - `interest_perf` — subject matched performance patterns
     (`multithread`, `parallel`, `SIMD`, `optimi[sz]`, `cache`, etc.).
     Pre-built with `verdict: take`.
   - `interest_modding` — subject matched modding patterns
     (`DLTX`, `DXML`, `lua.*bindin`, `xml.*pars`, `callback`, etc.).
     Pre-built with `verdict: take`.
   - `interest_infra` — subject matched infrastructure patterns
     (`xrCore`, `build.*system`, `allocat`, `serializ`, `SafeWrap`, etc.).
     Pre-built with `verdict: take`.
   - `graphics_review` — subject matched graphics patterns
     (`shader`, `render`, `bloom`, `volumetric`, `ssao`, etc.).
     Pre-built with `verdict: review`, `interest_category: graphics`.
     Always confirmed in the batch step, never auto-taken.
   - `needs_review` — hot-zone overlap (always trumps interest-category
     match). No verdict pre-built; examined manually in step 6.
   
   Every record carries an `interest_category` field (set to the matching
   category, or `null` for `already_cherry_picked`, `auto_skip`, and
   `needs_review`).

   The interest-category regexes live in `INTEREST_CATEGORIES` in
   `triage_helpers.py`. When the safety-net pass (step 8) surfaces a new
   keyword that should match, add it to the appropriate regex in that file
   — it's the single source of truth for auto-classification, not a
   separate config.

3b. **Append the `already_cherry_picked` batch** (no confirmation needed —
    these are already merged and don't need re-triaging):
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py append <file-with-already_cherry_picked-array>
    ```

4. **Append the `auto_skip` batch** (no confirmation needed — these
    deliberately don't match any interest category):
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py append <file-with-auto_skip-array>
    ```

5. **Append the auto-take interest batches** (`interest_perf`,
   `interest_modding`, `interest_infra`) — also no confirmation needed, same
   `append` command as step 4. These are pre-approved by the interest-category
   design: performance improvements and modding/infra capabilities are always
   welcome.

6. **Classify `needs_review` commits** (hot-zone hits): for each commit,
   read the actual diff (`git show <hash>`) — file-overlap alone is too
   coarse, see the `review`-verdict worked example in
   `references/ledger-schema.md` — and the relevant `PROJECT.md` paragraph
   if it's a hot-zone hit. Propose `take` / `skip` / `review` with a
   one-to-two sentence rationale that names the specific divergence area on
   conflict.

   If a commit conflicts with something we've clearly customized but that
   *isn't* currently flagged `hot_zone` (i.e. it slipped through because
   nothing in `PROJECT.md` or the registry covers that file yet), also
   propose adding it to the hot-zone registry — as a separate, explicitly
   flagged suggestion alongside the verdict, never silently. Include the
   proposed pattern and a one-line reason in the batch table (step 7).

7. **Batch and confirm** — see `references/batching-protocol.md` for the
   exact grouping and `AskUserQuestion` shape. This batch includes both
   `needs_review` (manual verdicts from step 6) and `graphics_review`
   (auto-verdict `review` from step 3). The table should include an
   `interest_category` column so graphics-review entries are visually
   distinct from hot-zone-flagged entries. Apply any free-text corrections
   the user gives before finalizing a batch, then append the confirmed batch
   the same way as step 4, and for any confirmed hot-zone additions:
    ```
    python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone-add "<pattern>" --reason "<why>" --related-commit <hash>
    ```

8. **Safety-net pass**: launch a Task agent (see
   `references/safety-net.md` for the full prompt template) to audit the
   `auto_skip` commits just appended this session. The agent:
   - Reads each auto-skipped commit's diff via `git show`
   - Looks for file-path hints (e.g. a commit titled "cleanup" that touches
     `ScriptEngine.cpp` → likely modding-interest) and code-content signals
     (e.g. new Lua bindings, parallel-for loops, SIMD intrinsics)
   - Reports any commits that look like they should have matched an interest
     category but didn't, with proposed verdict flips
   - Suggests new keywords to add to `INTEREST_CATEGORIES` in
     `triage_helpers.py` to catch similar commits in future sessions
   
   Present the agent's findings as a small batch table. Confirm flips with
   `AskUserQuestion` (same shape as step 7, but typically only a handful of
   entries), then apply verdict changes via the `upstream-merge-review`
   skill's `revise` command and add any approved keywords directly to the
   relevant regex in `triage_helpers.py`.

9. **Final report**: counts by bucket (auto-skipped / perf / modding /
   infra / graphics-review / needs-review), how many `take` entries are
   sitting at `applied: false` (point at `upstream-merge-apply` as the next
   step), any new keywords added to `INTEREST_CATEGORIES` this session,
   anything left at `review` with no resolution as an explicit follow-up,
   and any hot-zone registry additions made.

## Additional resources

- `references/ledger-schema.md` — full field reference including the new
  `interest_category` field, worked examples for each verdict type.
- `references/batching-protocol.md` — exact batch size, grouping order, and
  `AskUserQuestion` option wording for step 7.
- `references/safety-net.md` — the Task agent prompt template for step 8.
- `references/hotzones-schema.md` — the persistent hot-zone registry's
  schema, why it's separate from `PROJECT.md`, and how to add to it.
