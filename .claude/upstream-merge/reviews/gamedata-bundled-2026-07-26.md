# Gamedata (bundled/distribution) - reviewed 2026-07-26

## Summary

87 `take`-verdict commits touching `gamedata/scripts/`, `gamedata/configs/`,
and (in ~14 of the 87) a handful of `src/xrGame/*` files that back new Lua
exports. Reminder: this repo's `gamedata/` is the engine's bundled/
distribution copy, not the mod's active gamedata tree - the private tree
reconciliation for anything gameplay-relevant here is `upstream-merge-
gamedata`'s job, not this review's. This batch is routine overall: mostly
additive Lua-API exports (many self-documenting via `lua_help_ex.script`/
`ltx_help_ex.script`), monkeypatches in `aaaa_script_fixes_mp.script` and
`_g_patches.script`, and a few small, well-isolated C++ hooks backing those
exports. Nothing here touches a CLAUDE.md-documented Old World divergence
area in a way that looks likely to produce a hard conflict - the C++ hunks
are all short, additive, and land in functions/structs Old World hasn't
rewritten (with one exception, `Environment.h`/`level_script.cpp`, flagged
below). One real **ordering bug** found in the ledger itself (not the
commits) and one likely-dead **revert commit** - both listed under Gotchas.
Two feature clusters (persistent weather, anomaly evasion for monsters) are
gameplay-visible changes worth a playtest pass after apply.

## Hot-zone / deep-read commits

### 5911cd2a - persistent weather improvements, new method to set next weather's environment to interpolate to
Touches `src/xrEngine/Environment.h` (moves `SelectEnv` from private to
public) and adds `set_weather_smooth()` in `src/xrGame/level_script.cpp`,
a thin wrapper that calls `Environment().SetWeather()` then forces
`SelectEnv` on the current weather set to seed the interpolation target.
Small and purely additive - no existing logic changed, just one method's
visibility. `Environment.h`/`.cpp` is one of Old World's most heavily
customized files (procedural sun/moon, dynamic wind/rain/thunder, texture
contrast - see CLAUDE.md's Weather/atmosphere section), so a conflict on
context lines during cherry-pick is likely even though the change itself
is tiny. Verdict: unchanged, but flag for careful conflict resolution
during apply, not because the change is risky.

This is the tail commit of a 6-commit persistent-weather cluster (see
Batch-summarized below: 7a459869, 9239dd9d, 1322d882, 50f4fe10, 4f01fa62,
5911cd2a). I read the first (7a459869) in full since it's conceptually
adjacent to Old World's own weather divergence work even though it wasn't
flagged hot-zone: it overrides `level_weathers.WeatherManager` methods
(`load_state`/`save_state`/`select_weather`/`reset`) and adds a full
copy-pasted reimplementation of `select_custom_weather` inside
`aaaa_script_fixes_mp.script` (this codebase's monkeypatch convention -
same pattern used throughout this batch, e.g. 8302b238, 3c183f5d). Because
it's a full-body override rather than a wrapper, if the *private* gamedata
tree has its own customized `select_custom_weather` (plausible, given the
weather divergence work), this override would silently replace it rather
than compose with it - worth a specific check when `upstream-merge-
gamedata` reconciles this cluster. Not a reason to skip here; the engine
repo's bundled copy has no such customization to clobber. → `playtest`
(weather now persists across save/load with interpolation instead of
resetting - a player-visible behavior change).

### 17b711c8 - obj:is_enabled_anomaly() export for anomalies
Adds `CScriptGameObject::IsEnabledAnomaly()` wrapping `CCustomZone::
IsEnabled()`. Purely additive getter alongside the existing `enable_anomaly`/
`disable_anomaly` pair. No gotchas.

### f27211ad - xr_weapon_jam refactor (engine calls GetConditionMisfireProbability via npc_get_misfire_probability callback)
Rewrites `CWeapon::GetConditionMisfireProbability()` in `Weapon.cpp` from
early-return style to an if/else building a `result`, then adds a Lua
callback hook (`xr_weapon_jam.GetConditionMisfireProbability`) that only
fires for non-`CActor` parents (i.e. NPC-held weapons only). Functionally
equivalent to the old code for the actor's own weapon, **except** the
clamp changed from `clamp(mis, 0.0f, 0.99f)` to `clamp(result, 0.0f, 1.f)`
- the misfire probability ceiling moved from 99% to 100%, and this applies
unconditionally (actor and NPC weapons both use this function). Small but
real balance change. → `playtest`. The function itself is short and
self-contained; despite `Weapon.cpp` being heavily modified elsewhere by
Old World (DOF, non-linear inertia, firepos work), this specific function
hasn't been touched by any of that, so conflict risk is low.

### 34500bf7 - expose inside_anomaly() to Lua
Adds `CScriptGameObject::inside_anomaly()` wrapping `CAI_Stalker::
inside_anomaly()` (pre-existing engine method). Purely additive, no
gotchas.

### e259b9e5 - New Lua exports to get/set hud fire bone/pos (silencer)
Large (~150 line) but entirely additive set of getters/setters for
`attachable_hud_item`'s fire-point/bone data (main + secondary + silencer).
Verified `m_fire_point_silencer`/`m_fire_bone_silencer`/
`e_fire_point_silencer` already exist in this repo's `player_hud.h` (not
newly introduced here), so no missing-symbol risk. No existing logic
touched. No gotchas.

### 4cc68513 - bind can_kill_enemy/member and fire_make_sense fire gates
Three new `CScriptGameObject` wrappers over existing `CAI_Stalker`
fire-decision methods, each null/active-item-guarded with an error-logged
fallback. Purely additive, unusually well-documented commit message
(includes an explicit in-game test note). No gotchas.

### 03b96650 - add NPC weapon reload start/stop callbacks
Adds an additive block inside `CWeapon::OnStateSwitch()` (`Weapon.cpp`)
that fires `npc_on_weapon_reload_start`/`_stop` Lua callbacks on a
stalker's weapon entering/leaving `eReload`, guarded by `smart_cast<
CAI_Stalker*>` so actor reloads are unaffected. Lands right next to
Old World's own reload-DOF rewrite comment ("Swartz: re-written to use
reload empty DOF") in the same function - textually adjacent, so expect a
context-line conflict on cherry-pick even though the two changes don't
overlap logically. No behavior change to existing paths.

### 08b802c9 - bind make_enemy_visible (force seen-class enemy memory)
Adds `CScriptGameObject::make_enemy_visible()` wrapping the engine's own
`CMemoryManager::make_object_visible_somewhen`. Guarded (stalker receiver,
alive-entity argument). Purely additive. No gotchas.

## Batch-summarized commits

**Doc-only (`lua_help_ex.script`/`ltx_help_ex.script` entries, no functional
change) - 16 commits:** df9635cc, 2b029b5c, ba4ef28b, 1989651c, 3ba53323,
b3320afe, b10ae547, 65ec04e3, 2672b97e, 37f513f7, 79b08ca1, 871ada40,
2b5d77de, 350b6e39, 80eb5c1e, 7e0f4e8b. All add or (b10ae547) remove a
manifest line documenting a Lua/engine export; b10ae547 specifically
retracts an entry `b3320afe`-adjacent work had put in the wrong file
(engine-export manifest vs. pure-Lua callback) - self-correcting, fine as
a pair.

**`_g_patches.script` / `callbacks_gameobject.script` monkeypatch tweaks -
9 commits:** 40025d0f, 4e2df81f, bbe22e39, 438092c0, 4c0ee57d, 5cb47823,
cdb292ec, ed34699b, c11a21b5, 5ed131cb, 1e0edf51 (see Gotchas). Small
logging/safety/perf tweaks to existing monkeypatch functions, plus two new
callback fan-outs (`on_map_spot_selected`, `actor_on_item_sell`/`_buy`).

**`modded_exes_migration.script` / options-menu wiring - 8 commits:**
bee0af48, 9c8b2e85, 60dd615a, a77e523d, f7334af7, 2c75c10f, 486917f9,
d0252155. Routine "new option default, migration entry" plumbing; d0252155
adds a new `modxml_fix_faction_spot_size.script` (DXML hook, normalizes
map-spot icon sizes for army/ecolog factions to match other factions -
visual-only, additive).

**Weapon overheat smoke system (`item_weapon.script`) - 6 commits:**
80793709, 1a5bde55, afd5b862, 2b3f837d, 59e46374, 7730697e. A coherent
per-weapon smoke effect: HUD-geometry-aware smoke, framerate-independent
buildup/cooldown, individual data per weapon (NPC path present but
disabled), plus a log-spam fix and two small tuning tweaks. Player-visible
if the mod exposes weapon overheat, but self-contained gamedata-script-only
work.

**Persistent weather cluster (`aaaa_script_fixes_mp.script` + one C++
tail) - 6 commits:** 7a459869 (full read above), 9239dd9d, 1322d882,
50f4fe10, 4f01fa62, 5911cd2a (full read above). Weather state now
survives save/load with interpolation instead of resetting; 50f4fe10
specifically excludes underground-level transitions from persisting
state. → `playtest`.

**Anomaly evasion for monsters (`aaaa_script_fixes_mp.script` +
`obj:is_enabled_anomaly()`) - 8 commits:** f5c0303e (disabled test),
c31fc741 (enables it), fb0bdc87, a9023ea3, 369db897, b3ca0625, f0a5ef74,
17b711c8 (full read above). Dynamically registers anomalies as space
restrictors so monster pathfinding routes around them, with several
follow-up tuning passes (batch size, check radius, active-anomaly
filtering, path invalidation on anomaly movement). Genuine AI-behavior
change for monsters that previously walked through anomalies. → `playtest`.

**CCar Lua bindings (vehicle drive/telemetry) - 3 commits:** 97bf9ffc,
bd61f73d, 88444b7b. Exposes analog drive governor, hit/weapon-fired
callbacks, transmission/door/telemetry, and wheel-friction control to
Lua. Commit message states defaults (ungoverned) reproduce vanilla drive
exactly, so non-scripted cars (including the player's) are unaffected
unless gamedata scripts opt in. Not a documented Old World divergence
area; batch-summarized rather than deep-read on that basis.

**Signal light lighting-flag churn (`mod_system_signal_light.ltx`) -
3 commits:** fc38f41a (add file, enable shadow+volumetric for Zaton/Red
Forest), 4ba1925e (flip to disabled, "conflict with SSS"), 7a35bb63
(delete the file entirely, closes upstream issues #570/#571). Net effect
of taking all three in sequence is a no-op relative to before this
cluster started (file doesn't exist either way) - same "reproduce
upstream's own end state" pattern as the `user_name_og` add/revert pair
already accepted earlier in the ledger. No action needed beyond taking all
three in order.

**Misc single commits:** a395a5a9 (whitespace-only, diff-noise reduction
vs. vanilla), 7a404e13 (`level.object_by_id` → `get_object_by_id`
refactor), 9efa492d (safer `pda.calculate_rankings` patch), fc0feb4c (fix
`ai_move_to_cover` option), d525373e (scheduler_flush timing tweak),
1aa6223b (warfare trader task-generation fix), 048c20a4 (skip
member-less squad update), 08ab22f9 (perf: `iterate_nearest` → pre-filtered
monsters registry), 3009e6a0 + `motion_mark_reload` C++ pair (weapon
reload motion-mark matching, opt-in via ltx), 9c74d1cc (initial
`xr_weapon_jam.script` add, superseded in-batch by f27211ad above),
4dd4131e (options-widget fix, `Co-Authored-By: Claude`), 7b87f068
(pure style/convention alignment, "No functional change" per message),
ddd53796 (7-line DLTX anomaly-ammo fix), 8302b238/dbe00094 (already
covered - MP squad-teleport/actualize fixes), 4289ecf1/f368698d (see
Gotchas - ordering), 3c183f5d (already covered above in monkeypatch
tweaks section by file but is really a caching-bug fix), b0a3f287
(deletes `options_modded_exes_first_person_death.script` - verified no
remaining references to it anywhere in this repo's gamedata, clean
deletion).

## Gotchas / follow-ups

- **Ledger ordering bug, not a commit bug** - `4289ecf1` ("fix(alife):
  route assign_smart... through simulation_board") modifies
  `gamedata/scripts/sim_squad_scripted.script`, but that file doesn't
  exist in this repo yet; it's created by `f368698d` ("feat: add
  sim_squad_scripted.script", 1442 lines). `git merge-base --is-ancestor`
  confirms `f368698d` is genuinely upstream of `4289ecf1` in upstream's own
  history, but the ledger has them in the opposite order (4289ecf1 at
  line 10, added in an earlier triage pass; f368698d at line 185, added in
  a later pass that apparently didn't re-sort against earlier entries).
  Cherry-picking in ledger order would apply the fix to a file that
  doesn't exist yet. → `ordering_after` on `4289ecf1`, prerequisite
  `f368698d72b10299f0df14b457459c9a703429de`.
- **Likely-dead revert** - `1e0edf51` ("Revert '-- Generic callback for
  .../pull/535'") reverts `79c088166c0c80ed4e7db679d69e5aa55f9b0f4e`, but
  that commit's own ledger entry is `verdict: skip` ("Duplicate of
  5ed131cb... taking both would double-define the function"). If
  `79c08816` is never applied, `1e0edf51`'s revert has nothing to revert -
  best case a no-op, worst case a cherry-pick conflict (patch context
  expects lines that were never added). Proposing a verdict flip to `skip`
  for `1e0edf51`, paired rationale: "reverts 79c08816, which is itself
  skipped as a duplicate of 5ed131cb - nothing to revert."
- No `dangling_reference` findings - checked the one deletion in this
  batch (b0a3f287) and confirmed clean.
- No new `hotzone_gap` proposed - the one C++ file this batch touches that
  overlaps a CLAUDE.md divergence area (`Environment.h`, weather) was
  already correctly flagged `hot_zone: true` by triage, so the existing
  detection is working as intended here.
- No `bug_found` beyond the misfire-clamp behavior change already covered
  as `playtest` under f27211ad (that's an intentional-looking change, not
  a slip).
