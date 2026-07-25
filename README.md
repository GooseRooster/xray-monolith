# X-Ray sources based on Modded Exes for Anomaly - specialized edits made for Old World Mod. 

NOTE: 
The engine here likely won't work without the custom Old World gamedata (still to be released) 

For more information, see the original: https://github.com/themrdemonized/xray-monolith

## What's different from upstream


- **First-person body** - a visible player body system (not just floating weapon
  viewmodel): camera-relative offset/clipping fixes, crouch-sliding, movement
  inertia, arm suppression while climbing, Lua-driven hide/unhide and animation
  playback, and reduced default head-bob for motion-sickness comfort.
- **Probe-based global illumination (`r3_gi`)** - a light-probe ambient system
  (replacing the old constant ambient term) auto-placed via the sector/portal
  system, updated via compute-shader raycasting, and integrated with a
  compute-shader port of XeGTAO plus procedural sun/moon and static lighting
  quality tiers.
- **Steam Audio integration** - convolution-based reverb (binaural decode,
  low/medium/high quality tiers gated on the old EAX path) replacing/augmenting
  the legacy reverb, with material-based absorption mapping.
- **Weather/atmosphere** - dynamic wind, rain and thunder simulation, runtime
  height-fog uniforms, weather-driven texture contrast.
- **Post-processing** - reworked DOF (focus-plane transitions), motion blur
  improvements, a bloom pyramid fix, instanced tree rendering ported from OGSR.
- **Color grading, HDR and retro rendering** - a genuine HDR10 output pipeline
  (Rec.709/P3-D65/Rec.2020, ST.2084 PQ, automatic BT.2408 highlight
  expansion) built around one custom tonemapper: a Hermite-spline rolloff
  (linear passthrough below an auto-placed knee, smooth compression above)
  blended between luminance- and maxRGB-preserving results in Oklab space.
  This replaced an earlier menu of ~9 selectable operators (ACES/AgX/
  Uchimura/Reinhard/etc.) outright - not a "default picked from the menu."
  Runs alongside independent "retro" toggles - classic LUT-based materials
  vs. GGX PBR (`r4_material_style`), and R1-style baked lightmaps vs. modern
  probe-based GI (`r4_lighting_style`, `r3_gi`) - so content can lean as
  retro or as high-fidelity as it wants rather than the engine picking one
  look. Also unified bloom into a single multi-scale pass across SDR/HDR,
  replacing several separate legacy paths. See `CLAUDE.md` for the full
  breakdown (and its note on which shader source is actually authoritative).
- **Build/branding/tooling** - Old World exe naming/branding, MSBuild trimmed to
  DX11-first, a Discord CI notification hook, and Linux-side clangd/`compile_commands.json`
  tooling working toward an eventual CMake/Linux cross-compile build (see `tools/README.md`).

Most graphics features above are gated behind console vars (`r3_gi`, `r2_auto_fog`,
etc.) and some are still WIP/experimental - check the var's default and the
originating commit message before assuming a feature is live.
 
