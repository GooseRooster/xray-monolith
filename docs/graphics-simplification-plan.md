# Graphics Simplification — Implementation Plan

Companion to `graphics-simplification-spec.md`. File-by-file, ordered checklist.
Each work package is independently buildable against DX11 + DX11-AVX.

Decisions locked in during planning:

- **Cross-renderer removal** — shared phases plus R2/R3/R4 copies all removed, so
  `batch_build.bat` (DX8/9/10/11) still compiles. CI gate remains DX11 + DX11-AVX.
- **Probe volume layout** — keep the 3-texture `VoxelGPUUpdate` layout. vol1
  (`shDirection`) is zeroed; vol2 keeps `sunVisibility`, `pointLightColor` zeroed.
  No compute-shader or struct-layout changes.
- **Shader side** — the private gamedata repo (`_GAME/gamedata/shaders/`) is the
  source of truth. This repo's `gamedata/shaders/` is a stale distribution copy and
  is not edited here.

---

## P0 — Discord notify webhook fix (CI)

File: `.github/workflows/msbuild.yml`

Root cause: `discord-notify` reads `needs.release.outputs.publish-url` and
`publish-tag_name`, but `softprops/action-gh-release` only emits `url`, `id`,
`upload_url`, `assets` (no `publish-url`/`publish-tag_name`), and the `release`
job never declares job-level `outputs:`. Both must be fixed.

1. Add `outputs:` to the `release` job:
   ```yaml
   outputs:
     release-url: ${{ steps.publish.outputs.url }}
     release-tag: ${{ steps.datetime.outputs.today }}
   ```
2. In `discord-notify`, change:
   ```yaml
   RELEASE_URL: ${{ needs.release.outputs.release-url }}
   RELEASE_TAG: ${{ needs.release.outputs.release-tag }}
   ```

---

## P1 — Probe bounce removal

Files: `src/Layers/xrRender/LightProbeGrid.{h,cpp}`, `xrRender_console.{h,cpp}`

Remove secondary bounce entirely. Probes become `ambient = skyColor * skyVisibility`.

### LightProbeGrid.h
- Delete `ProbeNeighbors` struct (lines ~91-97).
- Delete `CLightProbe::bounce` field (CPU debug only, not in GPU cache).
- Delete member declarations: `m_probeNeighbors`, `m_materialAlbedos`,
  `m_propagationBuffer`, `m_propagationActiveSet`, `m_propagationIters`,
  `m_propagationRate`, `m_bounceIntensity`.
- Delete method declarations: `CastBounceRay`, `ComputeTriangleNormal`,
  `BuildMaterialAlbedos`, `GetMaterialAlbedo`, `BuildNeighborConnectivity`,
  `PropagateLight`.
- Keep `shDirection`, `pointLightColor`, `pointLightIntensity` struct fields
  (preserve 64-byte GPU layout); they are simply never computed.

### LightProbeGrid.cpp
- Delete thread-local `CachedBounceLight` struct + `s_tl_bounceLightCache`.
- Delete externs: `ps_r_probe_bounce_intensity`, `ps_r_probe_bounce_lights`,
  `ps_r_probe_max_distance`.
- Delete `CastBounceRay`, `ComputeTriangleNormal`, `BuildMaterialAlbedos`,
  `GetMaterialAlbedo`, `s_albedoTable[]`, `BuildNeighborConnectivity`,
  `PropagateLight`.
- `UpdateProbe()`: remove the bounce block (hemisphere-ray hit → CastBounceRay),
  sunlit-neighbor bounce section, point-light injection section, and the
  `dirAccum`/`energyAccum`/`newSHDir` dominant-direction computation.
  Final ambient = `ambientAccum / totalRays` (no `bounceAccum` add).
  Remove `bounceAccum` local. Zero `shDirection`, `pointLightColor`,
  `pointLightIntensity` remain at default (never assigned).
- `Build()`: drop `BuildMaterialAlbedos()`, `BuildNeighborConnectivity()`,
  `PropagateLight()`, `m_propagationBuffer`/`m_propagationActiveSet` resize.
- `Update()`: drop the periodic `PropagateLight()` call (`m_currentFrame % rate`).
- `Clear()`: drop `m_probeNeighbors`, `m_materialAlbedos`, `m_propagationBuffer`,
  `m_propagationActiveSet` clears.
- Constructor: drop `m_bounceIntensity`, `m_propagationIters`, `m_propagationRate`
  initializers.

### xrRender_console.cpp / .h
- Remove `ps_r_probe_bounce_intensity`, `ps_r_probe_bounce_lights`,
  `ps_r_probe_max_distance` (decl + CMD4).
- Keep `ps_r_probe_ambient_floor`, `ps_r_probe_chroma_blend`, `ps_r_probe_gi_boost`,
  `ps_r_debug_probes`, `ps_r_probe_update_rate`, `ps_r_probe_upload_rate`,
  `ps_r3_ssfx_il`.

---

## P2 — SSPE removal

Files: delete `r4_rendertarget_phase_sspe.cpp`, `blender_cs_sspe.{h,cpp}`;
edit `r4_rendertarget.{h,cpp}`, `r2_types.h`, `blender_combine.cpp`,
`r4_rendertarget_phase_combine.cpp`, `xrRender_console.{h,cpp}`.

- `r4_rendertarget.h`: remove `b_cs_sspe`, `s_sspe`, `rt_sspe`, `rt_sspe_prev`,
  `rt_sspe_scene`, `phase_sspe()`.
- `r4_rendertarget.cpp`: remove `b_cs_sspe` new/delete, `rt_sspe*` creation.
- `r2_types.h`: remove `r2_RT_sspe`, `r2_RT_sspe_prev`, `r2_RT_sspe_scene`.
- `blender_combine.cpp`: remove `s_sspe` texture bind (elements 0 and msaa).
- `r4_rendertarget_phase_combine.cpp`: remove `phase_sspe()` call, `rt_sspe_scene`
  copy, `sspe_params2` set_c, `ps_r_sspe_*` externs.
- Console: remove `ps_r_sspe_radius`, `ps_r_sspe_intensity`,
  `ps_r_sspe_max_distance`.

---

## P3 — Perceptual Lighting removal

Files: `blender_blur.{h,cpp}`, `r4_rendertarget.{h,cpp}`, `r2_types.h`,
`rendertarget_phase_blur.cpp`, `r4_rendertarget_phase_combine.cpp`,
`r4_rendertarget_phase_PP.cpp`, `r4.{h,cpp}`, `xrRender_console.{h,cpp}`.

- `blender_blur.{h,cpp}`: remove `CBlender_blur_pl`, `CBlender_perceptual_lighting`.
- `r4_rendertarget.h`: remove `b_blur_pl`, `b_perceptual_lighting`, `s_blur_pl`,
  `s_perceptual_lighting`, `rt_pl_half/quad/octo/hexa/hblur/vblur/short/source`,
  `phase_blur_pl()`, `phase_perceptual_lighting()`.
- `r4_rendertarget.cpp`: remove PL blender create/delete + `rt_pl_*` creation.
- `r2_types.h`: remove `r2_RT_pl_*` defines.
- `rendertarget_phase_blur.cpp`: remove `phase_blur_pl`, `PL_BlurPass`,
  `phase_perceptual_lighting`.
- `r4_rendertarget_phase_combine.cpp`: remove `phase_blur_pl()` call, PL `u_setrt`
  branches, `phase_perceptual_lighting()` call.
- `r4_rendertarget_phase_PP.cpp`: remove `rt_pl_source` write branch.
- `r4.{h,cpp}`: remove `o.ssfx_pl` bit + `= 0` assignment.
- Console: remove `ps_r3_gi_pl_params`, `ps_r3_gi_pl_params2`.

---

## P4 — Screen-space sunshafts removal (volumetric stays)

Files: delete `blender_ss_sunshafts.{h,cpp}` (R2/R3/R4) and
`rendertarget_phase_sunshafts.cpp`; edit `r2/r3/r4_rendertarget.{h,cpp}`,
`r2/r3/r4_rendertarget_phase_combine.cpp`, `xrRender_console.cpp`.

- Combine phases: remove `phase_sunshafts()` call + `R2SS_SCREEN_SPACE` /
  `R2SS_COMBINE_SUNSHAFTS` branch (keep volumetric).
- `r2/r3/r4_rendertarget.h`: remove `phase_sunshafts()`, `b_sunshafts`,
  `s_sunshafts`.
- `r2/r3/r4_rendertarget.cpp`: remove `b_sunshafts` new/delete, `s_sunshafts.create`,
  `rt_sunshafts_0/1` creation.
- `r2_types.h` (R2/R3/R4): remove `r2_RT_sunshafts0/1`.
- **Keep** `need_to_render_sunshafts()` (feeds volumetric min/max shadow map).
- Console: keep `ps_sunshafts_mode`; simplify `sunshafts_mode_token` to volumetric only.

---

## P5 — SSFX fog removal

Files: `blender_blur.{h,cpp}`, `r4_rendertarget.{h,cpp}`,
`rendertarget_phase_blur.cpp`, `r4_rendertarget_phase_combine.cpp`, `r4.{h,cpp}`,
`xrRender_console.{h,cpp}`.

- `blender_blur.{h,cpp}`: remove `CBlender_ssfx_fog_scattering`.
- `r4_rendertarget.h`: remove `b_ssfx_fog_scattering`, `s_ssfx_fog_scattering`,
  `phase_ssfx_fog_scattering()`.
- `r4_rendertarget.cpp`: remove create/delete.
- `rendertarget_phase_blur.cpp`: remove `phase_ssfx_fog_scattering()`.
- `r4_rendertarget_phase_combine.cpp`: remove `phase_ssfx_fog_scattering()` call.
- `r4.{h,cpp}`: remove `o.ssfx_fog` bit + `FS.exist` probe.
- Console: remove `ps_ssfx_fog`, `ps_ssfx_fog_scattering`, `ps_ssfx_fog_terrain_y`,
  `ps_r3_ssfx_fog`.

---

## P6 — Gas-mask drops removal (DUDV stays)

Files: delete `blender_gasmask_drops.{h,cpp}` (R2/R3/R4) and
`rendertarget_phase_gasmask_drops.cpp`; edit `r2/r3/r4_rendertarget.{h,cpp}`,
`r2/r3/r4_rendertarget_phase_combine.cpp`, `Blender_Recorder_StandartBinding.cpp`,
`xrRender_console.{h,cpp}`.

- Combine phases: remove `phase_gasmask_drops()` call; keep `phase_gasmask_dudv()`.
- `r2/r3/r4_rendertarget.h`: remove `b_gasmask_drops`, `s_gasmask_drops`,
  `phase_gasmask_drops()`.
- `r2/r3/r4_rendertarget.cpp`: remove create/delete.
- `Blender_Recorder_StandartBinding.cpp`: remove `ssfx_hud_drops_1/2` constant setup
  + registration.
- Console: remove `ps_ssfx_hud_drops_1`, `ps_ssfx_hud_drops_2`, and orphaned
  `ps_r2_drops_control`.

---

## P7 — Grass shadows, blood decals, r2_auto_fog

- Grass shadows:
  - `r2_R_lights.cpp`: remove `check_grass_shadow()` + call.
  - `r2_R_sun.cpp`: remove grass-in-cascade block.
  - `DetailManager.cpp`: remove `ps_ssfx_grass_shadows` check in `details_clear()`.
  - `DetailManager_VS.cpp` (R_R2) / `dx10DetailManager_VS.cpp` (DX10): remove
    `ps_ssfx_grass_shadows`/`rsGrassShadow` references.
  - Console: remove `ps_ssfx_grass_shadows`, `r__enable_grass_shadow`
    (`rsGrassShadow` mask).
- Blood decals:
  - `Blender_Recorder_StandartBinding.cpp`: remove `ssfx_blood_decals` constant setup
    + registration.
  - Console: remove `ps_ssfx_blood_decals`.
  - **Keep** `o.ssfx_blood` / `effects_wallmark_blood.ps` (blood wallmarks, unrelated).
- `r2_auto_fog`: remove `ps_r2_auto_fog` + CMD4 (console only).

---

## P8 — PBR materials removal

Files: `xrRender_console.{h,cpp}`, `r4.{h,cpp}`.

- `xrRender_console.h`: remove `st_opt_pbr` enum value (keep `st_opt_classic`).
- `xrRender_console.cpp`: remove `"st_opt_pbr"` token entry, `material_style_token`,
  `ps_r4_material_style` + its `CMD3`.
- `r4.h`: remove `pbr_materials` bitfield.
- `r4.cpp`: remove `o.pbr_materials` assignment, `USE_PBR_MATERIALS` define block +
  `sh_name` char.

---

## P9 — Build + verify

1. `msbuild /p:Configuration=DX11 src/engine-vs2022.sln`
2. `msbuild /p:Configuration=DX11-AVX src/engine-vs2022.sln`
3. Grep for every removed cvar / function / RT-enum / shader name; expect zero
   remaining references (outside private gamedata).
4. Delete shader cache in launcher before smoke test.
5. Smoke test: verify water SSR, TAA, volumetric sunshafts, SSS, DOF, motion blur,
   probe ambient all still render.

---

## Shader cleanup checklist (private gamedata repo)

Delete once the engine no longer references them:

| Feature | Delete |
|---|---|
| SSPE | `sspe_main.cs` |
| Perceptual Lighting | `pp_pl_downsample.ps`, `pp_blur_pl.ps`, `pp_perceptual_lighting.ps` |
| Screen-space sunshafts | `ogse_sunshafts_mask.ps`, `ogse_sunshafts_blur.ps`, `ogse_sunshafts_final.ps` (keep `effects_sun.ps` / `accum_volumetric_sun*`) |
| SSFX fog | `ssfx_fog_scattering.ps`, `ssfx_fog_scattering_blur.ps`, `screenspace_fog.h` |
| Gas-mask SSFX drops | `gasmask_drops.ps` + `gasmasks/mask_noise` (keep `gasmask_dudv.ps`) |
| PBR | `USE_PBR_MATERIALS` / GGX branches in deferred shaders |
| Grass shadows | grass-shadow code in detail/grass + shadow-map shaders |
| Blood decals | `ssfx_blood_decals` reads in wallmark shaders (keep `effects_wallmark_blood.ps`) |

Also: remove `s_sspe`/`sspe_params2` from combine shader; simplify `probe_params`
(drop bounce-intensity); drop `material_style` and `r2_sunshafts_mode` non-volumetric
options from `options_video_advanced_main.script`; delete the 12 `options_ssfx_*.script`
MCM pages; add `owa_graphics_init.script`; remove the Lua setter for
`ps_ssfx_fog_terrain_y`.
