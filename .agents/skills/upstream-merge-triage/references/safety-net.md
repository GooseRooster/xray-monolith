# Safety-net agent

After each triage session's auto-skip batch is appended, launch a Task agent
with the prompt below to audit the auto-skipped commits for classification
gaps. This catches things subject-matching missed — e.g. a commit titled
"cleanup" that introduces new Lua bindings — and grows the keyword sets
organically over time.

## Agent prompt template

Use this prompt verbatim when launching the safety-net agent. Substitute
`<HASHES>` with the JSON array of auto-skipped commit hashes from this
session's `pending` output.

```
You are auditing auto-skipped upstream commits from a triage session. These
commits were auto-skipped because their subject lines didn't match any
interest-category pattern (perf, modding, graphics, infra) and they don't
touch hot-zone files. Your job is to check whether any of them *should* have
matched, based on what the diff actually contains.

The commit hashes to audit: <HASHES>

For EACH hash:
1. Run `git show <hash> --format="%h %s" --stat` to see the subject and
   which files were touched.
2. Only do a full diff read (`git show <hash>`) if BOTH of these are true:
   - The file paths suggest it might touch an interest area (see file-path
     hints below)
   - OR the commit is unusually large for its subject (a vague subject like
     "cleanup" or "refactor" with 10+ files or 50+ changed lines is worth
     a closer look)
3. If neither condition is met, skip the full diff read and mark it as
   "confirmed_skip" — it genuinely doesn't look like an interest-area commit.

When reading the full diff, look for these signals:

**Performance signals** (candidate for interest_category: "perf"):
- New or changed parallelism: threading primitives (std::thread, Task,
  ttapi), job/fiber systems, thread pools, parallel loops
- SIMD/vectorisation: __m128, __m256, emmintrin, xmmintrin, any intrinsics
- Data-structure optimisation: new spatial acceleration structures
  (octree, BVH, spatial hash), changed container hot-paths
- Memory/cache: new allocators, memory pool changes, prefetch intrinsics,
  cache-line alignment (alignas(64), __declspec(align))
- Algorithmic complexity improvements: loop-invariant hoisting, early-out
  conditions, batched operations replacing per-element calls
- Frame-level optimisation: moved work off the hot path, changed update
  frequency, reduced redundant recomputation

**Modding signals** (candidate for interest_category: "modding"):
- New or expanded Lua bindings: .def("..."), luabind::class_, addFunction,
  new exposed classes/methods/fields to the scripting surface
- DLTX/DXML changes: xml_obj methods, insertFromXMLString, on_xml_read,
  new xml rewriting hooks, config patching infrastructure
- Config/INI parsing expansion: new config sections read at runtime,
  new console variables that expose engine state to scripts
- Script callback infrastructure: new event hooks, new
  luabind::functor registrations, new signal/slot connections to Lua
- Filesystem hooks: changes to FS_Game, virtual filesystem, game-data
  path resolution that affect mod loading

**Graphics signals** (candidate for interest_category: "graphics"):
- New shader passes or shader modifications (touching gamedata/shaders/
  or shader compilation code)
- New or changed post-process effects (bloom, DOF, SSAO, SSR, tonemapping)
- Rendering pipeline changes: new render phases, changed render-target
  management, new draw-call paths
- Lighting changes: new light types, shadow improvements, GI changes
- Particle/atmosphere: new particle system features, volumetric fog/clouds,
  skybox changes, weather rendering

**Infrastructure signals** (candidate for interest_category: "infra"):
- xrCore changes: new container types, memory management, string handling
- Build system: new CMake/MSBuild configs, compiler flags, toolchain changes
- Profiling: Tracy/Optick integration, new profiling scopes
- Serialisation: new or changed serialisation paths, save/load format changes
- Safety wrappers: new RAII guards, smart-pointer adoption, SafeWrap patterns
- Cross-compilation: clang-cl fixes, include-path changes, encoding/UTF fixes

File-path hints (these paths, when seen in --stat output, justify a full
diff read):
- src/Layers/xrRender*/  → possible graphics or perf interest
- src/xrGame/*.cpp files touching script/lua/ai/ paths → possible modding
- src/xrCore/  → possible infrastructure interest
- src/xrSound/  → check for Steam Audio or audio optimisation
- Files referencing "script_", "lua_", "_script", "_lua" → possible modding
- Files referencing "thread", "parallel", "concurrent" → possible perf
- Files referencing "alloc", "memory", "heap" → possible infra

Report your findings as a JSON array of objects, one per commit that looks
like a classification gap:

```json
[
  {
    "hash": "<full 40-char hash>",
    "current_verdict": "skip",
    "proposed_verdict": "take",
    "interest_category": "modding",
    "rationale": "Introduces new Lua binding for alife object iteration despite vague subject 'cleanup task handling'. Touches script_alife_object.cpp with new .def() calls.",
    "suggested_keywords": ["alife", "iteration", "cleanup"]   // optional
  }
]
```

For commits where no gap was found, include them in a separate
"confirmed_skip" array with hash only (no rationale needed):

```json
{
  "gaps": [...],
  "confirmed_skips": ["hash1", "hash2", ...],
  "stats": {
    "total_audited": N,
    "gaps_found": N,
    "diffs_read": N,
    "stat_only": N
  }
}
```

Do NOT propose verdict flips for commits that:
- Are purely documentation/changelog/readme updates
- Only touch gamedata configs in non-interest ways (e.g. weapon stat tweaks)
- Are trivial one-line fixes with no interest-category signal
- Touch hot-zone files (shouldn't happen — hot-zone hits go to needs_review,
  but double-check anyway)
```

## Confirmation protocol

The safety-net agent returns its findings as JSON. Present the `gaps` array
to the user as a small batch table (same format as the main batch table in
`batching-protocol.md`, with an extra `suggested_keywords` column):

```
| hash | subject | proposed verdict | interest_category | rationale | suggested keywords |
|---|---|---|---|---|---|
| abc1234 | cleanup task handling | take | modding | Introduces new Lua binding... | alife, iteration |
```

Issue one `AskUserQuestion`:
```
question: "Safety net found N classification gaps. Apply these verdict flips?"
options:
  - "Apply all proposed flips and keywords"
  - "Apply flips but skip keyword additions"
  - "Go commit-by-commit"
  - "Skip all — keep original auto-skip verdicts"
```

On approval:
1. Flip verdicts via:
   ```
   python3 .agents/skills/upstream-merge-review/scripts/review_helpers.py revise <hash> --verdict take|review --rationale "..." --decision-mode confirmed
   ```
2. Add any approved suggested keywords to the corresponding `INTEREST_CATEGORIES`
   regex in `.agents/skills/upstream-merge-triage/scripts/triage_helpers.py`
   directly — these are the single source of truth for auto-classification.
   Place new keywords at the end of the appropriate alternation group,
   preserving the existing line layout.
3. Report keyword additions in the final report (SKILL.md step 9).

## Keyword growth heuristics

When adding suggested keywords to `INTEREST_CATEGORIES`:

- Keep them narrow — a keyword that matches too broadly (e.g. just "fix" or
  "update") creates more false positives than it catches missing commits.
- Prefer prefix/stem matching: `lua` not `lua_`, `bindin` not `binding`,
  `pars` not `parser` — so variations get caught.
- Add to the existing alternation group; don't create a new group.
- If a keyword would cause 10+ false-positive matches against the current
  upstream backlog, consider it too broad — skip it or use a more specific
  variant.
- The safety-net agent itself will flag when a keyword is problematic in
  future sessions (if it generates too many false positives that then need
  manual override). That's the feedback loop.
