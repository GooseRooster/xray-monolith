# Rendering (R1-R4) - reviewed 2026-07-26

## Summary
11 commits: one real bug worth fixing during cherry-pick (light/glow move-detection
epsilon), a small always-on sun-color tuning cvar, a 4-commit multi-monitor
fullscreen-selection feature (2 of the 4 already reviewed under Other/misc), a
one-line hud-shader fix, a 3-commit GIF-playback feature (giflib vendoring +
CTexture support), a detail-object format upgrade (64 -> 16383 objects/level),
and a 2-commit shader-cache source-CRC feature. Overall risk: routine, except
the light-epsilon commit needs a one-line correction before it lands (see
below) - flag that one for extra care during apply.

## Hot-zone / deep-read commits

### c9b23061 - Possibility to provide custom epsilon for light and glow for spatial move optimization
Adds an opt-in `eps` parameter to `light::set_position`/`set_rotation` and
`CGlow::set_position`, so callers can tune the move-detection threshold used
to decide whether a light/glow needs `spatial_move()` (shadow re-registration).
**Bug found**: the new default in `Render.h`/`light.h` is `EPS` (1e-5) instead
of `EPS_L` (1e-3), the value the old hardcoded body used. `CGlow`'s default
correctly stays `EPS_L`; `light`'s does not. Every existing caller (6 call
sites in `Light_DB.cpp`, plus `light.cpp`'s internal copy-update) omits the
new arg, so taking this as-authored silently tightens the move-detection
threshold 100x for every dynamic light in the game - more frequent
`spatial_move()`/shadow re-registration, i.e. a frame-time regression, the
opposite of the commit's stated purpose. Also confirmed the originally-cited
GI-overlap concern doesn't hold: `LightProbeGrid.cpp` never calls
`set_position()`/`set_rotation()` at all.
**Fix required at cherry-pick time**: change the `light::set_position`/
`set_rotation` defaults from `EPS` to `EPS_L` in `Render.h` and `light.h`.
Verdict: unchanged (take), with that one-line correction folded into the
cherry-pick. → `bug_found`, `playtest`

### b7988447 - `r2_sun_lumscale_color` cvar to tune sun color
Adds `ps_r2_sun_lumscale_color` (Fvector3, default `1,1,1`) multiplying sun
color in two places. Correction to the original triage note: only the
`Blender_Recorder_StandartBinding.cpp` shader-constant hunk is
`#if RENDER==R_R1` gated - the `Light_DB.cpp` hunk (`sun_adapted->set_color`)
is **not** gated, so despite the `r2_` naming prefix this also reaches R4.
Safe regardless: default is a true multiplicative identity, so behavior is
byte-identical until a level/mod sets it explicitly, and it extends the
existing `ps_r2_sun_lumscale` scalar that already applies universally
(R4 included) unflagged. Verdict: unchanged (take). No flags.

### c54edaab - monitor selection
Adds `CCC_VidMonitor`, `MonitorList.cpp/.h`, and monitor-selection UI script -
the core of a 4-commit multi-monitor fullscreen feature (`b3529c58` precursor,
`1192d33c`/`7c7ebd76` follow-ups). Read the `xr_ioc_cmd.cpp` hunk directly:
purely additive, no interaction with OWA's dialog-UI-override hook or
`user_default.ltx` loading. Verdict: unchanged (take). → `ordering_after`
(after `b3529c58`, both in this batch)

Note: `1192d33c`/`7c7ebd76` were already reviewed in an earlier Other/misc
session without an `ordering_after` flag back to `b3529c58`/`c54edaab`. Not
corrected here (outside this batch's scope) - chronological cherry-pick order
already satisfies the dependency, but worth knowing if apply ever reorders.

### c4195681 - Fix models not compiling the correct shader when set_shader is used
Adds a `hud` flag to `dxRender_Visual`, captured at `Load()` and restored
around `SetShaderTexture()`'s `shader.create()` call, so hud-flagged models
compile the correct shader variant post-load. Confirmed `::Render->hud_loading`
already exists in this codebase (`Render.h`, used by `player_hud.cpp`) - no
dangling reference. Unrelated to probe GI or the material/lighting retro axes;
`FBasicVisual.h` only hit hot-zone via the GI-commit citation list. Verdict:
unchanged (take). No flags.

### 311f4735 / 73653ed4 - CTexture GIF animation support (v1 + v2)
Vendors giflib and adds GIF playback to `CTexture` (`311f4735`), then refactors
it one day later into a dedicated `gifPlayer`/`GIFResource` and deletes the
original `GIFStream.cpp/.h` outright (`73653ed4`). Self-contained, no OWA
divergence overlap; the `xrEngine.vcxproj` hot-zone hit on both is a
project-file citation artifact (touched by many unrelated OWA commits), not a
real conflict. Verdict: unchanged (take both). → `ordering_after` on
`73653ed4` (after `311f4735` - it renames/deletes files `311f4735` created,
can't apply standalone)

Also noted (not a flag - the commit isn't triaged yet): an untriaged upstream
commit `44e9ee79` ("mid-session texture eviction system (DX11 MT)",
2026-06-29) also touches `SH_Texture.cpp` later on. Worth checking for
interaction with these GIF `SH_Texture` hooks when `44e9ee79` reaches triage.

### 440495c7 / ca24e217 - Shader cache source CRC + less-verbose warning
Adds a source CRC (over shader source + resolved `#include` chain, new
`ShaderSourceCRC.cpp/.h` in xrCore) alongside the existing compiled-bytecode
CRC in the R1-R4 shader cache, so hand-edited shader source invalidates the
cache instead of silently reusing stale bytecode (`440495c7`), then trims the
resulting warning message (`ca24e217`). Cache format gains a leading 4-byte
`source_crc`; old-format cache files simply fail the new length/CRC check and
recompile - no corruption risk. Orthogonal to r3_gi/HDR10/post-process
customizations in `r4.cpp`, and plausibly useful for the private-repo
shader-editing workflow OWA already relies on. Verdict: unchanged (take
both). → `ordering_after` on `ca24e217` (after `440495c7` - edits the exact
`Msg()` call it added, across all four renderer backends)

## Batch-summarized commits

| hash | subject | take |
|---|---|---|
| b3529c58 | Match refresh rates and fullscreen output to the window's monitor | Precursor to monitor-selection feature (HW.h/dx10HW.cpp), orthogonal to rendering-pipeline divergence |
| a7e50843 | Add GIF spec. features | Extends gifPlayer/GIFResource (looping removal, interlace, disposal modes); builds on 73653ed4's files |
| de084e28 | details: add v4 format (14-bit ids, up to 16383 objects) | Detail-object slot format upgrade; read Load/Unload directly - heap-alloc/free is clean, no leak or dangling-VFS-alias risk |

## Gotchas / follow-ups
- **`c9b23061`** ships with `EPS` where `EPS_L` belongs as the light eps
  default - a one-line fix (`Render.h`, `light.h`) needed at cherry-pick time
  to avoid a 100x tighter move-detection threshold for every dynamic light.
  → `bug_found`, `playtest`
- **`c54edaab`** depends on **`b3529c58`** landing first (both this batch).
  → `ordering_after`
- **`73653ed4`** depends on **`311f4735`** (renames/deletes its files).
  → `ordering_after`
- **`a7e50843`** depends on **`73653ed4`** (edits its `gifPlayer`/`GIFResource`
  files). → `ordering_after`
- **`ca24e217`** depends on **`440495c7`** (edits the same `Msg()` call).
  → `ordering_after`
- Untriaged commit `44e9ee79` ("mid-session texture eviction system (DX11 MT)")
  also touches `SH_Texture.cpp` - flag for attention when it reaches triage,
  given the GIF commits' `SH_Texture` hooks in this batch.
- Pre-existing gap (not fixed this session, out of batch scope): `1192d33c`/
  `7c7ebd76` (Other/misc, already reviewed) lack an `ordering_after` flag back
  to `b3529c58`/`c54edaab` even though they depend on the MonitorList files
  those create.
