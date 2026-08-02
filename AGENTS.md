# Agent Skills for Old World Engine

This is the **engine repo** for Old World (OWA) - a fork of `themrdemonized/xray-monolith` with substantial deliberate divergences. See [PROJECT.md](PROJECT.md) for full context.

## Core Workflow

- **Upstream**: `themrdemonized/xray-monolith` (remote: `upstream`), branch `all-in-one-vs2022-wpo`
- **Main branch**: `all-in-one-vs2022-wpo` (used for PRs)
- **Merge branch**: `merge-upstream` (for staging upstream changes before landing)
- **Gamedata**: Lives in a separate private repo. This repo's `gamedata/` is bundled/distribution only.

## Build System

- **Primary solution**: `src/engine-vs2022.sln` (VS2022, requires MFC/ATL workload)
- **Build command**: `msbuild /p:Configuration=DX11 src/engine-vs2022.sln` (DX11 is the only actively maintained renderer)
- **Batch build**: `src/batch_build.bat` builds all configs (DX8/9/10/11, each with AVX variant)
- **CI**: `.github/workflows/msbuild.yml` builds on push/PR, produces artifacts for DX11 and DX11-AVX
- **Post-build**: Copy executable to game folder, delete shader cache in launcher before testing

## Linux Host Tooling

- **clangd support**: `tools/README.md` documents `compile_commands.json` generation via MSBuild logger + `tools/remap-compile-commands.py`
- **Requirements**: `xwin` cache for Windows SDK headers, `.clangd` config, `compile_commands.json` (both gitignored)
- **Regenerate**: Run MSBuild with logger in Windows container, then `tools/regen-compile-commands.sh` on host
- **Known gaps**: ~1500 files have MSVC-permissiveness issues (dependent-base lookup, template specializations, `or`/`and` as identifiers). Fix opportunistically when editing affected files.

## Upstream Merge Skills

Skills in `.agents/skills/` manage merging from upstream. Shared state in `.agents/upstream-merge/`.

| Skill | Purpose |
|-------|---------|
| `upstream-merge-triage` | Triage new upstream commits into take/skip/review verdicts |
| `upstream-merge-review` | Deep dive on accepted commits before applying |
| `upstream-merge-apply` | Apply accepted commits to `merge-upstream` branch |
| `upstream-merge-gamedata` | Port gamedata changes to private tree |

**Typical flow**: triage periodically → review themes → apply to `merge-upstream` → gamedata port to private tree

**Key commands** (run from repo root):
```bash
# Check pending upstream commits
python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py pending

# Preview apply plan
python3 .agents/skills/upstream-merge-apply/scripts/apply_helpers.py plan

# Check hot-zone files (deliberately diverged)
python3 .agents/skills/upstream-merge-triage/scripts/triage_helpers.py hotzone
```

## Architecture Notes

- **Renderer**: R4 (DX11) is the only actively maintained renderer. R1/R2/R3 are legacy.
- **Key subsystems**: 
  - `src/Layers/xrRenderPC_R4` - DX11 renderer (heavily modified for Old World)
  - `src/xrGame` - game logic, first-person body, Lua scripting
  - `src/xrSound` - audio engine with Steam Audio integration
  - `src/xrCore` - memory management, containers, utilities
- **Shader source of truth**: Private game repo's `_GAME/gamedata/shaders/r3/hdr10.h`. This repo's `gamedata/shaders/` is stale distribution copy.

## DXML System

Engine-side Lua hooks for intercepting and rewriting XML before parsing. See [DXML.md](DXML.md) for API (`xml_obj:insertFromXMLString`, `query`, `setText`, etc.) and usage patterns.

## Conventions

- **Console-var gating**: Most graphics/audio features are gated behind cvars (`r3_gi`, `r2_auto_fog`, Steam Audio tiers). Check defaults before assuming feature is live.
- **Merge conflicts**: Expect in `src/Layers/xrRenderPC_R4`, `src/xrSound`, `src/xrGame` (first-person body). The ~3280-site `#include` case-correction sweep touches nearly every file.
- **Never ask**: "Have you compiled the engine?" - assume yes.
- **Small changes**: Prefer small, targeted, mergeable diffs over broad refactors due to periodic upstream merges.
