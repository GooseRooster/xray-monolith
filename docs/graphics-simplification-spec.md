# Graphics Pipeline Simplification Spec

## Core Philosophy

Keep what works, simplify the options surface, trim only genuinely redundant
subsystems. Remaining features get sensible dev-set defaults via a Lua init script
instead of per-tweak MCM pages.

---

## Removals

### 1. Probe Light Bounce

Engine: `LightProbeGrid.cpp` `UpdateProbe()`

Remove all secondary bounce computation from the light probe system. Probes become a
pure world-space dynamic ambient term.

Removed code paths:

- `CastBounceRay()` — sun bounce, sky bounce, point light bounce from hemisphere ray
  hits against geometry
- Sunlit neighbor bounce — probes borrowing ambient from sunlit neighbors
- `PropagateLight()` — iterative neighbor ambient blending
- Point light injection — nearby point/spot light contribution to probe data

Kept code paths:

- Hemisphere rays for sky color accumulation and `skyVisibility`
- Soft shadow rays for `sunVisibility`
- Probe rasterization into volume textures (`vol0`, `vol2`)
- Column flood-fill in volume rasterization
- Temporal smoothing with adaptive time-of-day blending
- `probe_ambient_floor` (minimum ambient in deep interiors)

Result: `ambient = skyColor * skyVisibility`. No secondary bounce.
`vol1` (SH direction) becomes zero. `vol2.pointLightColor` becomes zero.

### 2. SSPE (Screen-Space Probe Enhancement)

Engine: `r4_rendertarget_phase_sspe.cpp`, `blender_cs_sspe.*`

Remove the screen-space color bounce pass that reads the previous frame's composited
scene and G-buffer to scatter indirect light. Remove SSPE render targets
(`rt_sspe`, `rt_sspe_prev`, `rt_sspe_scene`) and the scene-copy in `phase_combine()`.

### 3. Perceptual Lighting

Engine: `rendertarget_phase_blur.cpp`, `blender_blur.cpp`

Remove the post-post-processing "GI feel" effect — progressive downsample chain,
cascaded blur at 1/16 res, and perceptually-weighted composite blend back onto the
backbuffer. Remove PL render targets (`rt_pl_source`, `rt_pl_vblur`, `rt_pl_short`)
and the final PL call in `phase_combine()`.

CVars removed: `r3_gi_pl_params`, `r3_gi_pl_params2`.

### 4. PBR Materials

Engine: Deferred shader GGX code path, `o.pbr_materials` feature flag,
`r4_material_style st_opt_pbr`.

Gamedata: Remove `material_style` list option from `options_video_advanced_main.script`.
Hardcode `st_opt_classic`.

### 5. Screen-space Sunshafts

Engine: `phase_sunshafts()`, `blender_ss_sunshafts.*`, screen-space sunshaft render
targets.

Volumetric sunshafts (`accum_direct_volumetric()`) stay — they were always in STALKER
and are now just higher quality.

Gamedata: Simplify `r2_sunshafts_mode` options (remove screen_space and combined).
Only volumetric remains.

### 6. SSFX Fog

Engine: `CBlender_ssfx_fog_scattering`, `phase_ssfx_fog_scattering()`, fog
scattering render targets, fog-related uniforms in combine phase.

Gamedata shaders: Remove `screenspace_fog.h`, `ssfx_fog_scattering.ps`,
`ssfx_fog_scattering_blur.ps`.

Classic planar fog (original STALKER, controlled by level/weather configs) stays.

CVars removed: `ssfx_fog`, `ssfx_fog_scattering`, `ssfx_fog_terrain_y`,
`r3_ssfx_fog` (master toggle), `o.ssfx_fog` feature flag.

### 7. Grass Shadows

Engine: Remove `ssfx_grass_shadows` path in detail rendering and shadow cascade
grass integration.

Screenspace shadows (SSS) are a better replacement and more performant.

CVars removed: `ssfx_grass_shadows`, `r__enable_grass_shadow`.

### 8. Blood Decals

Engine: Remove `ssfx_blood_decals` cvar and any associated rendering path.

### 9. Gas Mask Drops (SSFX)

Engine: Remove `blender_gasmask_drops.*`, `phase_gasmask_drops()`,
`ssfx_hud_drops_1/2` cvars.

Classic gas mask DUDV distortion stays — this only removes the SSFX-enhanced droplet
overlay.

### 10. r2_auto_fog

Engine: Remove dead cvar — legacy unfinished toggle for auto-computing fog that was
never functional.

---

## What Stays

| System | Notes |
|--------|-------|
| SSFX Water | SSR, blur, waves — keep full system |
| TAA + Motion Vectors | Many players prefer TAA over SMAA |
| Motion Blur | Enhanced STALKER-original, now more stable |
| DOF | Aim/reload/dialog gameplay feedback |
| Volumetric Lights | STALKER-original, improved quality |
| Volumetric Sunshafts | STALKER-original, now higher quality |
| Object POM | Parallax on buildings/walls |
| Terrain POM | Parallax on terrain surfaces |
| Glass Refraction | Toggleable |
| Flora SSS | Toggleable |
| Rain Puddles | Dynamic wet surfaces |
| Sky Debanding | |
| Volumetric Smoke | 3D fluid simulation |
| SSS Shadows | Directional + point screen-space shadows |
| Wind | Grass + tree animation |
| Interactive Grass | Player/mutant/anomaly grass response |
| Terrain Quality | Detail distance, grass alignment/slope |
| SSAO/GTAO | XeGTAO with bent normals + obscurance |
| HDR10 | Full OWA custom HDR pipeline |
| Procedural Sun/Moon | HDR sun disc + flares |
| Classic Materials | Only `st_opt_classic` |
| DX11 Static Lighting | OWA retro customization |
| Classic Planar Fog | Original level/weather-based fog |
| Simplified Probe Dynamic Ambient | `skyColor * skyVisibility` only |

---

## Menu Cleanup

### Removed MCM Pages (12)

- `options_ssfx_shadows.script`
- `options_ssfx_cascades.script`
- `options_ssfx_water.script`
- `options_ssfx_parallax.script`
- `options_ssfx_terrain.script`
- `options_ssfx_fog.script`
- `options_ssfx_taa.script`
- `options_ssfx_wind.script`
- `options_ssfx_grass.script`
- `options_ssfx_flora.script`
- `options_ssfx_sss.script`
- `options_ssfx_il.script`

### Utility Scripts Kept (not MCM pages)

- `ssfx_terrain_parallax.script` — per-map terrain POM height offsets
- `ssfx_underground_check.script` — disables Water SSR on underground maps

### Master Toggles Kept in Advanced Main

- `r3_ssfx_water` — Water SSR
- `r3_ssfx_shadows` — SSS + shadow quality
- `r3_ssfx_taa` — TAA
- `r3_gi` — Simplified probe dynamic ambient

### Removed from Advanced Main

- `material_style` (PBR removed)
- `r3_ssfx_fog` toggle (SSFX fog removed)
- `r2_auto_fog` (never functional)
- `r2_sunshafts_mode` screen-space and combined options (only volumetric remains)
- Duplicate `ssfx_*` entries consolidated into the 4 master toggles above

### Advanced Group Structure (after cleanup)

```
VIDEO → Basic (PAGE)
      → Advanced (GROUP)
         → Main (PAGE)      ← simplified, ~35 settings
      → HUD (PAGE)
      → Player (PAGE)
      → Night (PAGE)
```

Removed: all 12 SSFX sub-pages from Advanced GROUP.

---

## Static Dev Configuration

New script: `_GAME/gamedata/scripts/owa_graphics_init.script`

Sets sensible defaults at game start for features whose MCM fine-tuning pages were
removed. Tuned by developers; not user-configurable.

Covered features: wind, interactive grass, screen-space shadows, terrain quality,
object POM, flora SSS/specular.

---

## File Impact Estimate

| Area | C++ Edited | Shaders Removed | Scripts Removed | Scripts Modified | CVars Removed |
|------|-----------|-----------------|-----------------|------------------|---------------|
| Probe/SSPE/PL | ~6 | - | - | - | ~15 |
| PBR Materials | ~3 | - | - | 1 | 2 |
| Screen Sunshafts | ~2 | - | - | 1 | 2 |
| SSFX Fog | ~3 | 4 | 1 | 1 | 5 |
| Grass Shadows | ~1 | - | - | - | 2 |
| Blood/GasMask/r2_auto_fog | ~3 | - | - | - | 5 |
| MCM Cleanup | - | - | 12 | 3 | - |
| Static Config | - | - | +1 new | - | - |
| **Total** | **~18** | **4** | **12** | **5** | **~31** |
