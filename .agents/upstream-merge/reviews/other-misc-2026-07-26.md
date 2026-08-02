# Other / misc - reviewed 2026-07-26

109 commits, 2026-03-30 through 2026-07-23. This theme is the catch-all
bucket, so it's genuinely heterogeneous: ~36 README/changelog-only commits,
a large mechanical `SAFE_WRAP` cleanup, several small cvar/bugfix commits
scattered across weapons/AI/alife/UI, an in-progress vehicle "drone/turret
camera" feature, and a handful of commits that needed real attention despite
not being flagged `hot_zone` by triage.

## Summary

Routine overall. Nothing here fights an Old World divergence directly - nothing
touches r3_gi, Steam Audio, HDR10, or the render pipeline. The two things
worth care during apply: (1) three small dependency chains where a later
commit edits code a specific earlier commit added (restriction-type,
alife-online-offline-group, and a cross-theme AI/Combat binding-dedup fix -
all already in correct chronological order in the ledger, just flagged so
`upstream-merge-apply` doesn't reorder them), and (2) one real behavior
change worth a playtest: `eb841304`'s FPCam custom-smoothing fix, which is
adjacent to the documented first-person-body camera work. Everything else is
either mechanical (SafeWrap removal), additive (new cvars/bindings), or
self-contained bugfixes.

## Hot-zone / deep-read commits

### SafeWrap cleanup - `cb47f5fd`, `9ac78aff`, `af8330fa`, `ae2404e8`, `8ef8413f`
Four "SafeWrap: Remove where its safe" parts (p1-p4) plus one companion WIP
commit, spanning `script_game_object_script2.cpp`/`script3.cpp`/
`script_game_object_script_trader.cpp`. `SAFE_WRAP` is upstream's own debug
wrapper (`script_game_object.h:1298`) - gated by `lua_busy_hands_debug`, it
just logs "Accessing destroyed object" before calling through; it isn't an
exception-safety net. All four commits are purely mechanical:
`SAFE_WRAP(&Foo::Bar)` → `&Foo::Bar`, no logic changes, confirmed by reading
full diffs. Low risk. Gotcha check: none - this is upstream's own maintenance
of upstream's own system, doesn't interact with anything Old World-specific.

### `01c20e39` - `duplicate_story_id_crash` cvar
Adds an opt-in cvar (default crash-on, `R_ASSERT4`) to downgrade the
"Specified story object is already in the Story registry!" assert to a
logged warning. Pure opt-in, no default behavior change.

### `20a1e919` / `2d605090` - ammo-type guard + red error text
`20a1e919` adds a null-check-and-reset for `m_ammoTypes[m_ammoType]` in
`CWeapon::net_Spawn` (prevents a crash on bad ammo config); `2d605090`
immediately follow-up-edits the same lines plus `WeaponMagazinedWGrenade.cpp`
to prefix `Msg()` calls with `!` (red console text convention). Sequential,
no issue.

### `5aabbd59` - `CRayPick::get_normal()` binding
Pure addition: new `get_normal()` Lua export on `CRayPick`, computes a face
normal from static verts/tris. No conflict surface.

### `e3c99a6f` / `875f3f19` - `movement_manager_move_along_path_query_pos_threshold` cvar
Adds a tunable threshold (default 0.75, later commit `875f3f19` - not
hot-zone but same feature - changes default to 0.25) for the nearest-object
physics query cache in `move_along_path`. Additive cvar, no default behavior
change beyond the tuning itself.

### `b99b08d2` - `r__optimize_torch` cvar + `GetPerceivedDist`
Switches `CTorch::UpdateCL`'s position-epsilon calculation to
`Device.GetPerceivedDist` (accounts for scope/binoc zoom) behind an
opt-out cvar (default on). Self-contained perf/accuracy tweak.

### `80fd08f8` - `level.get_weather_weight` / `set_weather_weight`
Adds `CEnvironment::set_lerp()` - repositions the two active weather
descriptors' `exec_time` so the current interpolation weight becomes a given
value, preserving the interval length. New script-facing weather-blend
control. Additive, no default behavior change (nothing calls it yet).

### `f59465e5` - `motion_exists` Lua binding
Loads a model, checks if a named motion cycle exists via
`IKinematicsAnimated::ID_Cycle_Safe`, then deletes the model. Straightforward
existence-check utility exposed to scripts.

### `d75a1f5f` - `g_clamp_actor_camera_collision` cvar cleanup
Removes an unnecessary scope block and changes `FALSE, TRUE` to `0, 1` for
`CCC_Integer` bounds - cosmetic, no behavior change (same cvar values).

### `eb841304` - FPCam custom-smoothing fix - **playtest-worthy**
`actor->m_FPCam->m_customSmoothing = smoothing` → `_max(1, smoothing)`.
Traced the semantics in `EffectorBobbing.h`/`.cpp`: `m_customSmoothing == 0`
means "fall back to FPDeath default EMA smoothing" (not "no smoothing"),
`== 1` means instant/no smoothing, `> 1` means custom EMA over that many
steps. So a script calling `set_cam_custom_position_direction(pos, dir, 0)`
previously got the *default death-cam smoothing* applied silently instead of
an instant snap - this commit makes `0` behave as instant/no-smoothing
instead. The commit title ("don't apply FPCam smoothing if custom smoothing
is 0") and the clamp-to-1 code agree once you know the enum semantics; it's
not contradictory, just non-obvious. This is directly adjacent to the
documented first-person-body camera work (`CLAUDE.md`'s head-bob-smoothing
paragraph) - any gamedata script calling this API with `smoothing=0`
expecting the old fallback-to-default behavior will see a visible change.
Worth a quick playtest of any custom-camera cutscenes/sequences after
landing.

### `132ae11b` / `cf936fa1` - ambient-particle offset randomization
`132ae11b` adds a ±5 random XZ offset to weather ambient-particle spawn
position (`CGamePersistent::WeathersUpdate`); `cf936fa1` immediately refines
it to a randomized minimum-0.5 offset (avoids spawning right on top of the
camera). Sequential refinement, both purely additive/visual, no gotchas.

### `300e0162` → `9f572394` → `2adafde7` - restriction/pathing cluster - **ordering dependency**
- `300e0162`: adds `ForceSetRestrictionType`/`InvalidateRestrictions` Lua
  bindings for space restrictors and custom monsters.
- `9f572394`: independent mechanism - `g_restriction_rebuild_frames` cvar
  that lets a monster keep moving along its old path for N frames while a
  restriction-triggered path rebuild is pending, instead of freezing.
- `2adafde7`: **edits code added by `300e0162`** - adds an early-return in
  `ForceSetRestrictionType` if `new_type == old_type`.

`2adafde7` will not apply/compile correctly unless `300e0162` has already
landed. Ledger order is already correct (300e0162 dated 05-26 08:36,
2adafde7 dated 05-26 21:38, same day) - flagging so `upstream-merge-apply`
doesn't split them across separate cherry-pick batches. `9f572394` has no
code dependency on the other two, just thematically adjacent.

### `3f6e8c95` - save/load console autocomplete stutter fix
Adds a 10s TTL cache to `get_files_list()` (called every frame while typing
tooltips) plus a forced rescan right after a save completes. Contained,
sensible.

### `c3550100` - safer transfer-money functions
Adds `GiveMoneySafe()` with overflow/underflow-guarded `u32` arithmetic,
rewrites `TransferMoney`/`GiveMoney` to use it. Preserves the "negative
transfer" escape hatch intentionally (comment: "unknown how it is used in
3rd party"). No behavior change for the normal-range case, hardens the
edge cases.

### `8cab4393` - dedup rank_name/smart_cover bindings - **cross-theme ordering dependency**
Removes 3 duplicate `.def()` lines from `script_game_object_script3.cpp`.
Traced back: `3e1d4be8` (AI/Combat theme, take, 06-03 03:10) registers
`rank_name` in `script2.cpp`; `c9063c22` (AI/Combat theme, take, 06-03 19:24)
re-registers `rank_name` + 2 more bindings in `script3.cpp`, duplicating
`3e1d4be8`'s `rank_name`; `8cab4393` (Other/misc, take, 06-03 19:33) removes
the 3 duplicate lines. **All three must land in exactly this chronological
order** - `8cab4393` deletes lines `c9063c22` adds, and both touch
`script_game_object.h`/`script3.cpp` regions `3e1d4be8` also touches. Dates
are already in the correct order, but this chain crosses the AI/Combat and
Other/misc theme boundary - flag for `upstream-merge-apply` so a
theme-batched cherry-pick order doesn't separate them.

### `a6bed37a` - `g_fireParams` binding + AK74 fire-point getters
Adds `CScriptGameObject::g_fireParams()` (actor/stalker fire origin+dir as a
Lua table) and 3 new AK74 fire-point getter bindings. Pure addition.

### `c602437d` - `on_loading_screen_dismissed` callback fix
Moves the `crash_saving` forward-declare, adds a mouse-state reset and fires
`_G.OnLoadingScreenDismissed` after the existing `OnLoadingScreenKeyPrompt`
call, inside `game_loaded()`. Also enables `crash_saving::save_impl` at this
point (previously presumably not wired at all in this path, or wired
elsewhere - worth confirming during apply that this doesn't double-enable
crash-saving if some other code path also sets `save_impl`). Self-contained
otherwise.

### `1baee035` - `bullet_check_visual` object option
New `CBULLETMANAGER_EX`-gated feature: bullets can require hitting the
dynamic visual mesh (not just bone hitboxes) via
`ValidateHitDynamicVisualMesh`. Opt-in per-object (`bullet_check_visual` in
the object section, default off). Additive, no default behavior change.

## Additional deep-reads (not flagged `hot_zone`, pulled in per judgment call)

### Alife online/offline-group crash-fix cluster - `a7bc3dd6` → `219e6373` → `4ad41ad2` → `fb20ae7f` → `60904b8a`
Five commits over three weeks (05-08 to 05-30) iteratively hardening
`CSE_ALifeOnlineOfflineGroup::update()`/`synchronize_location()` against
crashes: empty-`m_members` guard added, then null-`MEMBER*` guards added,
then wrapped in try/catch, then the empty-check is *removed* (claiming
`.begin()` already actualizes the lazy container), then **re-added** one
week later with an explicit `m_members.begin(); // force actualize` comment
- i.e. `60904b8a` discovered `fb20ae7f`'s premise was wrong (`.empty()`
doesn't trigger actualization, only `.begin()` does) and fixed it forward
rather than reverting. This is genuine iterative back-and-forth, not
redundant/conflicting takes - the final state (in date order) is coherent
and correct. **Must land in exact chronological order** - already the case
in the ledger, just documenting why the diffs look like they contradict each
other if read out of order.

### `e152fe1a` - double online-transition crash fix
One-line change in `Level.cpp::ProcessSpawnEvents`: skip spawn if the object
is missing *or offline* (previously only checked missing). Independent of
the cluster above (different file).

### Offline-bolts / busyhands cluster - `6992a1bc` → `cec3db85`
`6992a1bc` *removes* savable-children filtering from
`CALifeSwitchManager::remove_online` (claims it's redundant with
`add_offline_impl`'s own filtering, and the `alife_trader_abstract.cpp` loop
had an index-decrement bug from the removed erase). `cec3db85` (2 days
later) re-adds equivalent filtering under a renamed/corrected predicate
(`remove_non_alife_controlled_predicate`, checking `m_bALifeControl` instead
of `can_save()`) to fix a specific case: offline bolts (client-only children
not in `objects()`) that `add_offline_impl` can't handle. Same
"temporarily remove, then reintroduce corrected" pattern as the cluster
above - land in order.

### `a5cb7680` - CHangingLamp per-light shadow/volumetric properties
Lets `shadow`/`volumetric`/`ambient_shadow`/`ambient_volumetric` be set per
light-source-config instead of only from the hanging-lamp's spawn flags.
Not GI/probe-system code (no `r3_gi` interaction) - just exposes existing
`IRender_Light` setters through config. Backward compatible (falls back to
the old flag-derived values via `READ_IF_EXISTS` defaults).

### `57de78c5` - remove double occluder_volume in level_sounds - **audible behavior change**
Static level sounds (`SStaticSound::Update`) previously multiplied volume by
`Sound->get_occlusion()` on top of whatever occlusion the sound emitter
itself already applies - this removed the second application. Real
attenuation-strength change for static ambient level sounds behind
geometry (they'll sound louder/less occluded than before, since only one
occlusion pass now applies instead of two compounding). Framed as a bugfix
(double-application), reasonable to take, but audible - worth a quick
in-game A/B if any level relies on heavily-occluded static sounds sounding
very quiet.

### `ad8c7ea2` - map-spot click deferred to MOUSE_UP
Detailed, well-explained fix for map-pan hijacking level-changer-icon
clicks. New `OnMouseAction` override with 5px drag threshold, disarm on
focus-lost. Self-contained to `map_spot.cpp/.h`, no gotchas found.

### `5d8a5d07` - `iterate_level_objects_of_clsid` Lua binding
Perf-motivated addition (avoids ~776-squad global scan for level-local
alife queries). Adds `CALifeGraphRegistry::level_exists()` guard for the
pre-actor-spawn window. Purely additive, documented iterator-mutation
constraint matches existing `iterate_objects` convention.

### `ad6edf9b` - CCar reverse-gear speed governor support
Makes `SetTargetSpeed`/`DriveRefSpeed` signed so a negative target speed
commands reverse thrust; auto-transmission still never selects reverse gear
on its own. Contained to `Car.cpp`, not an Old World divergence area.

### `1b02a82d` - LMG/tri-state-reload motion-mark fixes - **gameplay-timing change**
Adds `ClickInterruptFlag`/`MotionMarked` state to
`CWeaponAutomaticShotgun`, changes reload-interrupt logic from "fire key
pressed" to "fire key was pressed *during* reload", and adds LMG-specific
motion-mark handling in `WeaponMagazined.cpp::OnMotionMark` (fixes belts
visually disappearing on certain motion marks). Real reload-timing/feel
change for tri-state-reload weapons (shotguns/LMGs) - flag for playtesting
if Old World's weapon configs use `tri_state_reload`/`m_bTriStateReload` on
any weapons, since this changes when a fire-key press during reload
actually interrupts it.

### Vehicle "turret camera" / drone-control feature - `348b881b` → `b4da0b51` → `20e00410`
Three "WIP" commits (06-24 to 06-30) building out `CCar::SVisualCamera` (a
bone-driven physical turret camera, opt-in via a vehicle's
`visual_camera_definition` ltx section, disabled unless a vehicle config
explicitly sets `enable=true`) and a scope/zoom system (`m_scopes`,
`ScopeOnMouseWheel`) plus drone control-force renames
(`m_control_*_max` → `m_control_*_force`) and damping fixes (per-axis
rotation damping via `Fvector` instead of a single scalar). Not an Old World
divergence area (vehicles aren't mentioned in `CLAUDE.md`), and both new
features are opt-in/dormant unless a vehicle's `.ltx` config sets them up -
no shipped Old World vehicle config does today, so no default-behavior risk.

**Bug found** (uncorrected through all 3 commits): `SVisualCamera::OnMouseMove`
in `348b881b` (`src/xrGame/CarNew.cpp`) checks `if (dx)` twice - the second
branch, which should update vertical rotation (`m_desire_ang.x`) from `dy`,
guards on `dx` instead:
```cpp
if (dx) { ... m_desire_ang.y -= d; ... }     // horizontal, correct
if (dx) { ... m_desire_ang.x -= d; ... }     // should be `if (dy)`
```
Effect: the turret camera's vertical look only updates when there's also
horizontal mouse movement in the same event. Low real-world impact today
(feature is dormant, no shipped config enables it), but worth fixing before
anyone builds content on top of it. Not proposing a ledger verdict change -
this is a bug *within* an otherwise-fine upstream commit, not a reason to
skip taking it.

## Batch-summarized commits

**README/changelog-only (36 commits, no code changes):** `1d83a6f9`,
`213c57a2`, `36cd1100`, `1a9a26c2`, `845bad09`, `8a1d37cf`, `2d876595`,
`591b6f2b`, `dd472fe8`, `ec729e21`, `47b2628b`, `e89f694f`, `c660ceb2`,
`93ec0e88`, `32693078`, `cb243a80`, `dffc8ab2`, `c7481735`, `ab17a4c4`,
`4be9532d`, `b813607d`, `db7e03bd`, `766c5eb7`, `08d28112`, `6a08e090`,
`fdc43bc6`, `424be659`, `078ac226`, `719b44bd`, `672b2f08`, `0641b087`,
`78d398f0`, `91431f36`, `b59010dc`, `1a6c1584`, `41cfd8b1`.

**Doc-file churn, nets to zero (4 commits):** `dfcd3708` adds
`AI_HOOKS.md`/`PR_DESCRIPTION.md` (981 lines, renamed from
`CLAUDE_CODE_CONTEXT_AI_HOOKS.md`), `e730474f`/`e6482077` clean up em-dashes
in them, `7d8d03e4` deletes both files entirely ("content moved to PR
description"). Taking all four in order reproduces upstream's actual
end state: neither file exists. No risk, just noting so nobody's surprised
by the add-then-delete pattern mid-apply.

| hash | subject | take |
|---|---|---|
| `d41c2acb` | WIP (remove unused local in CustomMonster.cpp) | trivial cleanup |
| `3d9e5011` | DXML safer lua callback | null/empty-string guards, ScriptXMLInit.cpp |
| `59d76547` | `get_new_local_point_on_mesh` early-return + log | replaces VERIFY2 hard-assert with graceful fallback |
| `a9680efe` | auto-fire on reload-end if fire held | WeaponMagazined QoL, opt-in behavior |
| `443603be` | CMissile progress-bar caching (#507) | avoids UI-object creation during render phase |
| `bb3a48c1` | CTorch::Update position/rotation epsilon tuning | perf optimization |
| `048c158d` | move_along_path: cache nearest-objects query | perf, paired with `e3c99a6f`/`875f3f19` above |
| `6a9e7abe` | move_along_path: remove empty check, reduce defaults | tuning follow-up |
| `65c22ad1` | don't set thread description (#511) | xrCPU_Pipe, avoids an API issue |
| `7a84d0e7` | WIP - small additive hooks (4 files, 15 lines) | build_config_defines.h flag + spawn/space hooks, additive |
| `1192d33c` | disable reshade on dx8/dx9 after monitor select | MonitorList.cpp, legacy-renderer path |
| `7c7ebd76` | refresh display list on the fly | MonitorList/device wndproc, additive |
| `2ad3fd14` | LoadScopeKoeffs error message | WeaponMagazined, diagnostics only |
| `738c0173` | better error messages | WeaponMagazined + damage_manager, diagnostics only |
| `639d3bf8` | auto-fire uses Level().IR_OnKeyboardPress (#521) | input-source fix for `a9680efe`'s feature |
| `7e5d0c36` | luabind class_rep::function_dispatcher try/catch | 3rd-party luabind, error containment |
| `9a30134a` | tri-state reload: add missing `CMD_START` check | one-line fix, companion to `1b02a82d` |
| `51c33d31` | ActivateShell more error info | CharacterPhysicsSupport, diagnostics only |
| `07fb2ced` | splash logo window position fix | xrEngine x_ray.cpp, one line |
| `0462d14e` | floating artefact fix | Artefact.cpp |
| `7c4c16db` | upgradeable fire_point/fire_point2/fire_point_silencer + hud variants | WeaponMagazined.cpp, additive, 120 lines |
| `74231391` | numpad support for console | xrEngine input/console, contained |
| `006fc833` | cleanup/unused code removal | follow-up to `74231391` |
| `c57c8c16` | numlock disabled for scrolling like normal apps | follow-up to `74231391` |
| `676edc74` | fix CPatrolPoint::load_from_config | patrol_point.cpp |

## Gotchas / follow-ups

1. **Ordering dependencies to preserve during `upstream-merge-apply`:**
   - `300e0162` before `2adafde7` (same-theme, code dependency)
   - Alife online/offline cluster: `a7bc3dd6` → `219e6373` → `4ad41ad2` →
     `fb20ae7f` → `60904b8a` (same-theme, code dependency, non-monotonic diffs)
   - Busyhands/offline-bolts: `6992a1bc` → `cec3db85` (same-theme, code dependency)
   - **Cross-theme**: `3e1d4be8` (AI/Combat) → `c9063c22` (AI/Combat) →
     `8cab4393` (Other/misc) - all dated 2026-06-03, already in order, but
     don't let a theme-batched apply separate them.
2. **Real bug found, not a reason to skip**: `SVisualCamera::OnMouseMove` in
   `348b881b` (`src/xrGame/CarNew.cpp`) has a copy-paste `if (dx)` that
   should be `if (dy)` on the vertical-look branch. Feature is dormant
   (opt-in per-vehicle config, nothing ships with it enabled), so no action
   needed before landing - just flagging for whoever eventually builds a
   vehicle config that uses it.
3. **No hot-zone registry gaps found.** Checked whether `level_script.cpp`,
   `console_commands.cpp`, `script_game_object*.cpp`, or the alife/vehicle
   files touched in this batch needed a `hotzone-add` - they're already
   effectively treated as hot zones by triage's own per-commit flagging
   (visible in this batch's 25 `hot_zone: true` entries) even though they
   aren't literal patterns in `hotzones.jsonl`. Vehicle files (`Car*.cpp`)
   and generic alife files (`alife_online_offline_group.cpp`,
   `alife_switch_manager.cpp`) correctly stayed unflagged - neither is a
   documented Old World divergence area.
4. **No verdict flips proposed.** Everything in this batch is either
   mechanical, additive/opt-in, or a self-contained bugfix. The two
   playtest-worthy behavior changes (`eb841304` FPCam smoothing, `57de78c5`
   static-sound occlusion, `1b02a82d` tri-state reload timing) are still
   `take` - they're legitimate fixes, just worth a look in-game after
   landing rather than blind-trusting the commit message.
