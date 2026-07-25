## Project Overview

This is the **ENGINE repo for Old World** (OWA) - a fork of
[themrdemonized/xray-monolith](https://github.com/themrdemonized/xray-monolith),
which is itself a modded-exe fork of the X-Ray engine used by S.T.A.L.K.E.R.
Anomaly (Monolith 64-bit engine). This repo is C++ engine source only; it does
not contain the mod's gamedata, scripts, or configs.

**Relationship to the game repo**: The actively-developed Old World gamedata
(gameplay scripts, configs, quests, weather tuning, shaders) lives in a
separate repo, referred to there as the "ENGINE repo" pointing back at this
one. See that repo's `CLAUDE.md` for gamedata/scripting context. Concretely:

- Shader development for OWA happens in **that** repo's
  `_GAME/gamedata/shaders/`, not here.
- `gamedata/` in *this* repo is the engine's own bundled/distribution
  gamedata (default shaders etc.), not the mod's active gamedata tree.
- R4 (DirectX 11) is the only renderer under active development for OWA. R1/R2/R3
  are legacy and not a priority to maintain, though R3 shares shader source
  with R4 in `gamedata/shaders/r3/`.

**Upstream**: forked from `themrdemonized/xray-monolith`, branch
`all-in-one-vs2022-wpo`. That upstream repo continues to receive fixes and
features from its own community; this fork periodically merges from it.

## Remotes and merge workflow

- `origin` - `git@github.com:GooseRooster/xray-monolith.git` (this fork)
- `upstream` - `https://github.com/themrdemonized/xray-monolith.git` (added for
  pulling in upstream changes)

The `merge-upstream` branch exists specifically for doing upstream merge work
locally before it lands on `all-in-one-vs2022-wpo` (the main branch used for
PRs). As of the last check, this fork is ~100 commits ahead of
`upstream/all-in-one-vs2022-wpo` (Old World-specific work, see below) and
~396 commits behind (upstream fixes/features not yet merged in).

When merging upstream:
- Expect conflicts concentrated in files this fork has heavily modified
  (rendering pipeline in `src/Layers/xrRenderPC_R4`, sound (Steam Audio),
  first-person body code in `xrGame`).
- The ~1469-file, ~3280-site `#include` case-correction sweep (see "Linux
  cross-compilation tooling" below) touches nearly every file; expect it to
  generate diff noise in merges. It changed include *text* only, never file
  casing, specifically so it wouldn't create a recurring merge tax - keep it
  that way rather than renaming files to match.
- Do the merge on `merge-upstream`, not directly on `all-in-one-vs2022-wpo`.

## Old World's intentional divergence from upstream

This fork carries substantial Old World-specific engine work beyond routine
upstream fixes. Grouped by area (commit references are short hashes on
`merge-upstream`, oldest first within each group):

**First-person body** (`dba226d8`, `2fa17ef9`, `21425d1d`, `152784f6`,
`782fb9fb`, `07760e68`, `36af8ba6`, `4dc4dcc7`, `1bcd7499`, `8d98ee68`,
`b59851b5`, `4484724d`) - a visible player body (not a floating viewmodel):
camera-relative offset handling and clipping fixes (including through thin
walls), crouch-sliding and movement inertia, arm-bone suppression while
climbing, `actor_before_jump` callback, Lua-driven hide/unhide and
direct animation playback with speed scale, and head-bob smoothing options to
reduce motion sickness.

**Probe-based global illumination / `r3_gi`** (`732129ca`, `279e4ccb`,
`e71bd73f`, `7a59ba53`, `f27c365c`, `de430491`, `b62e1124`, `d456677b`,
`0cce1568`, `9323a6be`, `9dd8d57e`, `109845b2`, `d53760c3`, `f22ca3b3`,
`922e74e8`, `8f4923ca`, `96c33099`, `04ecb058`, `952ba361`, `dd3ca606`,
`4fb5e685`) - a light-probe ambient system replacing the old constant
`L_ambient` term. Probes auto-place via the sector/portal system at level
load and update via staggered/compute-shader raycasting with indirect sun
bounce; integrates with XeGTAO (ported to a compute shader for a large FPS
win) and SSFX indirect light as a screen-space fallback. Gated behind the
`r3_gi` console var. Also includes procedural sun/moon, static lighting
quality tiers, and a 16-bit lightmap render target.

**Steam Audio integration** (`81d7cc69`, `a08150ee`, `d900266d`, `615083cb`,
`72e8c1b6`, `bd26256f`, `fd6e8f2d`, `9c2d839f`, `a834cc43`, `c098181b`) -
convolution-based reverb with binaural decoding, replacing/augmenting the
legacy EAX-style reverb (sound-quality tiers: low = old EAX, medium/high =
Steam Audio convolution), material-based absorption mapping, and a fix for
orphaned convolution rings left behind by destroyed sound emitters.

**Weather / atmosphere** (`6d41c074`, `dacb9d71`, `0b872ce7`, `d3c84895`,
`cd345cd1`) - runtime height-fog uniforms, weather-driven texture contrast,
dynamic wind/rain/thunder simulation, and an `r2_auto_fog` debug console
override (default off).

**Post-processing / rendering** (`488e37b3`, `24e28a15`, `61b9066a`,
`507131bd`, `979aa2d4`, `c9ec8e7c`, `4aaeaf92`, `c929b040`) - reworked DOF
(upsample phase, focus-plane transitions), motion blur improvements, a bloom
pyramid fix (extract from full-res scene instead of the 256x256 upscale),
colorgrading uniform rename (`hdr10_*` -> `cg_*`), instanced tree rendering
ported from OGSR, sunshaft and tree-flicker fixes. See "Color grading, HDR
and retro rendering options" below for the full depth of this work - it's
one of the largest and most deliberate pieces of Old World-specific engine
design, not just a bundle of small fixes.

## Color grading, HDR and retro rendering options

This is the deepest and most deliberately-designed piece of Old World's
engine divergence: a single coherent pipeline that lets a level or player
choose *any point* between "authentic 2007 X-Ray look" and "modern HDR10
fidelity," on several independent axes, rather than forcing one look.

**Source-of-truth warning**: the authoritative shader implementation is
`_GAME/gamedata/shaders/r3/hdr10.h` in the private Old World game repo (see
`_GAME` in that repo's `CLAUDE.md`) - shader development happens there, not
in this engine repo. This engine repo's own bundled
`gamedata/shaders/r3/hdr10.h` is a stale, unsynced distribution copy of an
earlier iteration and does **not** reflect current design (it still shows a
menu of selectable tonemapping curves that was later deleted entirely - see
below). Don't read design intent from it. The C++ side
(`src/Layers/xrRender/xrRender_console.{h,cpp}`,
`src/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp`) in *this* repo
is current and matches the private repo's shader (e.g. `r4_cg_*` naming,
`r4_hdr10_chroma_correction`, no tonemap-mode cvar) - cross-check both sides
before trusting either alone.

**Retro axis - shading and lighting can be switched back to old-school:**

- `r4_material_style` (`st_opt_classic` default / `st_opt_pbr`) - classic
  is LUT-based material response, explicitly commented as "default, retro
  look"; PBR is a GGX specular BRDF. Independent of the HDR/lighting
  settings below, so a player can mix and match.
- `r4_lighting_style` (`st_opt_dynamic` default / `st_opt_static`) - dynamic
  is the standard R4 deferred pipeline with cascade shadows; static
  switches to R1-style baked lightmap lighting ("retro mode", also cheaper).
- `r4_static_lighting_quality` (`st_static_low` / `medium` / `high`) - controls
  how much of the modern fog pipeline still runs under static lighting: low
  is flat `fog_color` only (period-accurate R1 behavior), medium adds
  simplified cubemap fog with linear blending, high runs the full pipeline
  (rotation, normalization, Mie scattering, Oklab) - so a "retro" level
  doesn't have to give up modern atmospheric fog entirely.

**Modern axis - probe-based GI and PBR** (see "Probe-based global
illumination" above): `r3_gi`, `ps_r_probe_*`, and SSPE (`ps_r_sspe_*`)
params provide the opposite end of the spectrum - dynamic indirect
lighting instead of baked lightmaps.

**HDR10 output pipeline** (`hdr10.h`) - a genuine HDR10 display pipeline, not
a tonemap-and-call-it-done approach:

- Colorspace selection (`r4_hdr10_colorspace`: Rec.709 / P3-D65 / **Rec.2020
  default**) with real chromatic-adaptation matrices for each conversion,
  correct ST.2084 (PQ) encoding against a configurable whitepoint
  (`r4_hdr10_whitepoint_nits`, default 400 nits, range 10-10000). Inverse PQ
  helpers (`HDR10_InverseST2084_PQ`, `HDR10_PQToLinear`, `HDR10_LinearToPQ`)
  exist for round-tripping already-PQ-encoded values.
- **Tonemapping is one custom curve, not a menu.** An earlier iteration
  exposed ~9 selectable operators (ACES Narkowicz/Hill, AgX Normal/Punchy,
  Uchimura, "SteveM", Uncharted2, Extended Reinhard) behind a
  `HDR10_TONEMAPPER` selector plus a separate luminance-vs-color mode
  toggle. **Both were deleted entirely**, not merged into a default - the
  current shader has no operator selection at all. In their place:
  a single Hermite-spline rolloff (`HDR10_HermiteSplineRolloff`) whose knee
  is placed automatically from the **ITU-R BT.2408** formula: strict linear
  passthrough below the knee (preserving the flat, "raw" retro STALKER look
  for ordinary lighting), a smooth C1-continuous cubic rolloff toward
  `target_white` above it (compression reserved for genuine highlights -
  fires, muzzle flashes, the sun). SDR and HDR share this exact function;
  the *only* difference is what `target_white` is (`1.0` for SDR, `peak_nits
  / 80` for HDR) - there's no separate SDR/HDR tonemap code path anymore.
- **Hybrid luminance/maxRGB blend, done in Oklab.** The spline is evaluated
  twice per pixel - once against luminance (correct hue for neutral/gray
  content) and once against the brightest channel (correct saturation for
  vivid content like fire or neon) - and the two results are blended by
  saturation using `oklab_lerp` from the shared `owa_oklab.h` utility
  (perceptually-uniform Oklab space, so the luminance/maxRGB crossover
  doesn't produce a muddy intermediate color). `owa_oklab.h` has a master
  `#define` kill switch that collapses `oklab_lerp` back to a plain RGB
  `lerp` for A/B testing.
- HDR adds **BT.2390 Annex 1 chroma correction** above the knee
  (`HDR10_ApplyChromaCorrection`, gated by `r4_hdr10_chroma_correction`,
  0.0-1.0, default 0.6) to counteract the perceptual desaturation that
  highlight compression otherwise causes.
- Color grading (`r4_cg_exposure/contrast/contrast_middle_gray/saturation/
  brightness/gamma`) runs through ARRI LogC space for the contrast step
  specifically so contrast doesn't clip/crush before tonemapping - and
  applies identically whether HDR10 is on or off (these were renamed from
  `r4_hdr10_*` to `r4_cg_*` specifically to make that "always active"
  behavior clear, since they previously read as HDR-only).
- UI elements get a separate HDR path (`HDR10_ToDisplay_UI`) with their own
  nits scalar and a saturation-blend workaround for the fact that PQ-space
  alpha blending is not linear: `alpha * PQ(color)` is wrong, blending
  saturation toward 1.0 as alpha approaches 1 approximates the correct
  `PQ(alpha * color)`.
- The in-game 3D PDA screen is intentionally excluded from world tonemapping
  (`ps_r4_hdr10_pda`) to avoid double-applying HDR tonemap to its own
  rendered contents.
- Light expansion into HDR range has a stylized SDR fallback: point/spot and
  general light expansion (`HDR10_ExpandLight*`) only apply when HDR10 is
  on, but sun expansion specifically
  (`HDR10_ExpandSunLight_WithSDR`) also applies a modest brightness lift
  (up to 25%) in plain SDR - scaling with sun brightness, zero at night - so
  direct sunlight still visually "pops" above ambient without requiring HDR
  display output at all. Separate multipliers exist for lights vs.
  particles (`r4_hdr10_light_expansion`, `r4_hdr10_particle_expansion`).
- Three built-in shader debug modes (`HDR10_DEBUG_MODE` 1-3, compile-time):
  a false-color heatmap of raw pre-linearization input, a binary mask of
  which pixels exceed 1.0 (i.e. would clip/bloom pre-HDR), and the same
  heatmap post-linearization - lets HDR/exposure tuning happen without an
  external GPU profiler.
- HDR10 is off by default (`r4_hdr10_on = 0`) - it's an opt-in display mode,
  not the base look.

**Unified post-process consolidation** - several previously-separate R2/R4
postprocess paths were deliberately merged into one, and in the tonemapping
case an entire operator-selection system was deleted outright rather than
just consolidated:
- Tonemapping: both the old R2-only piecewise curve (`tnmp_*` / `r2_tnmp_*`)
  *and* the R4 multi-operator/tonemap-mode system described above were
  replaced by the single Hermite-spline curve inside
  `HDR10_ToDisplay_World()`, used for both SDR and HDR output.
- Bloom: HDR10-specific bloom/lens-flare passes were removed in favor of one
  multi-scale ("Kawase" downsample/blur/upsample mip chain) bloom shared by
  SDR and HDR (`r2_bloom_threshold/intensity/radius`,
  `CBlender_hdr10_bloom_*` in `blender_hdr10_bloom.h`).
- Dead code removed as part of this: `img_corrections()` (never called in
  R4), the old separate `pp_bloom_*` output (never sampled).

**Procedural sun/moon**: `r4_procedural_sun_moon` toggles between the
traditional sprite/texture sun and a fully procedural render with
configurable star-burst flare (`r4_sun_flare_intensity`,
`r4_sun_flare_rays`), a subtle HDR glow for the moon
(`r4_hdr10_moon_intensity`), and dawn/dusk time windows
(`r4_hdr10_sun_dawn_begin/end`, `r4_hdr10_sun_dusk_begin/end`, 24h format)
controlling the transition.

**Why this matters for Old World's vision**: these axes are independent by
design - "retro" (classic materials + static lightmaps) is not simply "HDR
off." A player can run modern probe GI with classic LUT materials, or
static R1-style lightmaps with full HDR10 output, etc. That flexibility is
the point: Old World is explicitly trying to let content lean as retro or
as high-fidelity as it wants, rather than picking one aesthetic for the
whole game.

**Scripting/engine bindings** (`2a6aac40`, `657909a5`, `423264cf`, `e0fb123d`) -
an engine-side dialog UI override hook, loading defaults from
`user_default.ltx` when the launcher wasn't used, init-order guards for a
missing `user.ltx`, and mimalloc integration (currently disabled while
diagnosing a heap-destruction issue - see `e0fb123d`).

**Build/branding** (`e4900c98`, `a67167c1`, `7e45d59b`, `0946f124`,
`0b3c7a69`) - Old World exe naming/version string, MSBuild trimmed to build
DX11 configs by default, and a Discord CI notification webhook.

Most graphics/audio features above are gated behind console vars (`r3_gi`,
`r2_auto_fog`, the Steam Audio quality tier, etc.) rather than always-on -
check the var's default and the originating commit before assuming a feature
is live in a build.

## Architecture

### Directory structure

- `src/` - engine C++ source, built via `engine-vs2022.sln` (or the older
  `engine.sln`)
  - `xrCore` - memory management, containers, low-level utilities
  - `xrEngine` - main runtime and rendering coordination
  - `xrGame` - game logic, Lua/luabind scripting glue, AI systems, the
    first-person body code
  - `xrSound` - audio engine, including the Steam Audio integration
  - `xrPhysics`, `xrNetServer`, `xrParticles`, `xrCPU_Pipe`,
    `xrServerEntities`, `xrXMLParser`, `xrCDB` - supporting subsystems
  - `Layers/xrRender` - render code shared across renderer backends
  - `Layers/xrRenderPC_R4` - the DX11 renderer; primary/only actively
    maintained render target for Old World
  - `Layers/xrRenderPC_R1`, `R2`, `R3`, `xrRenderDX9`, `xrRenderDX10` -
    legacy renderers, not a priority to maintain (R3 shares shader source
    with R4)
  - `3rd party` - vendored dependencies (LuaJIT, OpenSSL, Theora/Vorbis/jpeg
    codecs, OpenAL-soft, optick (git submodule), etc.)
- `sdk/` - SDK binaries/includes/libraries used by the build
- `gamedata/` - engine-bundled gamedata (default shaders, etc.) for
  distribution - not the mod's active gamedata (see "Relationship to the
  game repo" above)
- `compressor/` - `xrCompress` packaging tool and `.cmd` wrappers for
  building distributable game/map archives
- `tools/` - Linux-host developer tooling (clangd setup, compile-commands
  remapping) - see below
- `.github/workflows/` - CI: `msbuild.yml` (build), `discord-notify.yml`

### Build system (Windows/MSVC)

- Primary solution: `src/engine-vs2022.sln` (VS2022; requires MFC and ATL
  components installed alongside the standard C++ workload)
- `src/batch_build.bat` builds all configs (DX8/9/10/11, each with an `-AVX`
  variant) via `vswhere`-located MSBuild
- After compiling: copy the DX11 (or DX11-AVX) executable into the game
  folder and delete the shader cache in the launcher before testing
- CI (`msbuild.yml`) builds on every push/PR

### Linux cross-compilation tooling

This fork is Windows/MSVC-only to build and run (DirectX, Win32 APIs). There
is host-side Linux tooling for *editing* the code with full clangd support
(real headers, per-file flags, go-to-definition) while still building in a
Windows VM/container - see `tools/README.md` for the full setup
(`compile_commands.json` capture via an MSBuild logger,
`tools/remap-compile-commands.py`, the `.clangd` config, and an `xwin`
header/lib cache).

The longer-term goal (in progress, not close to done) is a real CMake-based
cross-compile targeting `clang-cl`/`lld-link` against the `xwin` sysroot -
producing the exact same `x86_64-pc-windows-msvc` binary from a Linux host,
no engine porting or DirectX/OpenGL swap. `tools/README.md` tracks the
source-level MSVC-permissiveness gaps found so far (dependent-base lookup,
non-standard template specializations, `and`/`or` as identifiers, file-casing
mismatches) that block a real `clang-cl` compile even though they don't
affect MSVC. Fix these opportunistically when a file you're already touching
is noisy, rather than trying to sweep the whole repo.

### DXML

`DXML.md` documents the DXML system: engine-side Lua hooks that let gamedata
scripts intercept and rewrite XML (dialogs, UI, translation strings) before
it's parsed by the engine, via `modxml_*.script` files and an `on_xml_read`
callback. This is an engine-provided scripting facility, not gamedata itself
- see it for the API surface (`xml_obj:insertFromXMLString`, etc.) exposed to
scripts.

## Development guidelines

- Ask before large or architecturally invasive changes; this repo is shared
  with a periodic upstream merge in mind, so prefer small, targeted, mergeable
  diffs over broad refactors.
- When changing something shared between the mainline upstream code and Old
  World-specific additions (e.g. `xrRenderPC_R4`, sound, first-person body),
  say which category the change falls into - it affects how it should be
  merged/rebased against upstream later.
- Console-var-gated features (`r3_gi`, `r2_auto_fog`, Steam Audio tiers) may
  be mid-development; check the default value and recent commit history for
  a feature before assuming it's stable or shipped.
- Never ask the user if they've compiled/recompiled the engine - assume they
  have.
