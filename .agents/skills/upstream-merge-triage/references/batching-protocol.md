# Batching protocol

`AskUserQuestion` is capped at 2-4 options and a handful of questions per
call, so hundreds of `needs_review` commits can't be confirmed one at a
time. This is how step 6 of `SKILL.md` scales.

## Grouping

1. Group `needs_review` commits into batches of **25** (override with
   `--batch-size`).
2. Order groups so hot-zone hits come first (highest-value review time),
   then everything else, oldest-first within each group.
3. Render each batch as a Markdown table directly in the chat reply before
   asking anything, so every individual proposal is visible:

   ```
   | hash | subject | proposed verdict | hot-zone files | rationale |
   |---|---|---|---|---|
   | 132ae11b | apply random offset for weather's... | review | GamePersistent.cpp | Touches weather divergence file but only randomizes an emitter offset... |
   | ... | ... | ... | ... | ... |
   ```

   If any commit in the batch surfaced a proposed hot-zone registry
   addition (per `SKILL.md` step 5), list those separately underneath the
   table so they're reviewed as their own thing, not folded into a verdict:

   ```
   Proposed hot-zone additions this batch:
   - `src/xrGame/ActorCondition.cpp` (triggered by abc1234) - reworks stamina
     regen curves here, which overlaps our custom hunger/psy balancing.
   ```

## Confirming a batch

Issue exactly **one** `AskUserQuestion` per rendered batch:

```
question: "How do you want to handle this batch of N commits?"
options:
  - "Approve all as proposed"
  - "Approve all except ones I'll list"
  - "Hold this batch - go commit-by-commit"
  - "Stop here for now"
```

- **Approve all as proposed** → append the batch verbatim, then run
  `hotzone-add` for any proposed hot-zone additions in this batch (also
  approved by implication - call this out in the confirmation summary
  after so it's not a silent side effect).
- **Approve all except ones I'll list** → the user replies in free text
  (e.g. "flip 132ae11b to take", "skip the rest of the bloom-related ones",
  "skip the ActorCondition.cpp hot-zone addition") - apply those overrides
  to the in-memory batch (verdicts and/or hot-zone additions) before
  appending/adding.
- **Hold this batch - go commit-by-commit** → drop batching for just this
  group; walk each commit individually in plain conversation (no
  `AskUserQuestion` needed - just ask directly) until the user says to
  resume batching.
- **Stop here for now** → append nothing further this run (including no
  `hotzone-add` calls for this batch's proposals), report progress so far,
  and let the ledger's `meta.json` marker pick up correctly next time (do
  not append a partial/unconfirmed batch).

## Steady state

This grouping is sized for the *initial* backlog (hundreds of commits). In
steady state - triaging periodically as upstream moves - the delta since
`meta.last_synced_upstream_hash` should be tens of commits at most, which
collapses to 1-2 batches (1-2 `AskUserQuestion` calls) per session.
