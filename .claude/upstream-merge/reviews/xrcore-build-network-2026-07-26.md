# xrCore/low-level, Build/CI, Network/server entities - reviewed 2026-07-26

## Summary
This session cleared the entire remaining `take` backlog (11 commits) across
three small themes. Ten of the eleven are routine, low-risk internal
refinements (DLTX cache/value-type cleanup, a defensive bounds check, a
non-blocking critical-section guard, build flags, verbose diagnostics). The
one commit worth real caution is `288eb3ca` ("WIP"), which despite the vague
message is a large, complete, and well-executed refactor of the `CAR_NEW`
flying-car code into a new `CCarDrone` class - but it removes several
Lua-exposed `CCar` methods and an ini-driven Lua callback with no scripted
replacement, which is a genuine risk against Old World's private gamedata
tree if it configures any vehicles this way. Overall risk read: routine,
except `288eb3ca` which should get a private-repo grep before/at apply time
(not a reason to skip - the C++ side is internally consistent).

## Hot-zone / deep-read commits

### eb2e669c - dump cvars on engine start and crash
Adds a `dump_cvar` console command (logs every registered cvar's current
status) and wires it into three trigger points: manual invocation, automatic
run at the end of `execUserScript()` (i.e. once at startup after user config
loads), and automatic run from the crash handler via a new
`Debug.set_crashhandler()`/`get_crashhandler()` slot in `xrDebugNew.cpp`.
Touches `xr_ioc_cmd.cpp` (hot zone), but only *adds* a new
`IConsole_Command` subclass and one `CMD1` registration - none of OWA's own
registered cvars (HDR10, color grading, `r3_gi`, etc., also registered in
this file) are touched. The crash-handler slot itself
(`crashhandler`/`set_crashhandler`/`get_crashhandler` in `xrCore/xrDebug.h`)
already existed before this commit, so it's a new consumer of an existing
mechanism, not new plumbing. No gotchas found.

### 017a83ed - feat: incremental build
Mechanical addition of `/LTCG:INCREMENTAL` next to the existing
`/ignore:4099` linker flag across all Release/Release-AVX blocks in
`xrEngine.vcxproj` (hot zone by file, not by content). Verified all 12
occurrences of the target line exist in the current fork's config set
(renamed R1-R4 configs included) and every one gets the flag - link-speed
only, no functional change. No gotchas found.

### 3ef968c3 - DLTX: Root is xr_vector<Sect> (value type instead of Sect*)
Already given a full-repo check at triage time (grepped every
`.sections()`/`CInifile::Root`/`RootIt`/`RootCIt`/`friend`/`.DATA` caller);
re-confirmed here. This is a genuine improvement - drops a heap-allocated
`Sect*` per section plus a manual `xr_delete` loop in favor of value storage,
and fixes the DLTX cache to store already-resolved `DATA` instead of
re-inserting through `InsertIntoDATA` a second time on a cache hit. Touches
12 files across `xrCore`, `xrEngine`, `xrGame`, `xrNetServer`,
`xrServerEntities` - all are mechanical `(*i)->Name` -> `(*i).Name` /
`sec->Name` -> `sec.Name` adaptations to the new value-semantics iterator,
including the one hot-zone hit in `Environment_misc.cpp` (`load_weathers()`/
`load_weather_effects()`) - no weather logic touched, purely the syntax
change. Verdict unchanged: take, no new gotchas.

### 288eb3ca - "WIP" (CCarDrone refactor)
The vague commit message hides a substantial (742 insertions / 770
deletions across 12 files) but complete refactor: the `CAR_NEW`
flying/drone-car logic that used to live inline in `Car.cpp`/`CarNew.cpp` is
split out into a new `CarDrone.cpp`/`CarDrone.h` pair (`CCarDrone` class),
with `.vcxproj`/`.vcxproj.filters` updated in both the VS2019 and vs2022
project files to include the new files. `CAR_NEW` is actively enabled in
this fork (`src/build_config_defines.h:46`), so this is live code, not
behind a disabled compile flag.

Traced every symbol removed from `Car.h`/`Car.cpp`/`CarNew.cpp` to confirm
where it landed:
- `IsCameraZoom()`, `m_control_ele*`, `m_rotor_bones`, `m_drive_bones`,
  `eCarTypeDef`, etc. all correctly reappear either inline in `Car.h` or
  moved into `CarDrone.h`/`.cpp` - not dropped.
- `CarNew.cpp` itself is *not* deleted (git shows it as `M`, not `D`) - just
  gutted of the ~500 lines that moved to `CarDrone.cpp`. The `.vcxproj` still
  references it correctly; no stale build-file entry.
- Checked the other 4 files across the codebase that reference
  `IsCameraZoom` (`Actor.cpp`, `Actor_Weapon.cpp`, `CarCameras.cpp`,
  `WeaponStatMgun.cpp/h`, none touched by this commit) - the symbol survives
  as an inline method in `Car.h`, so these callers are fine.

**What does not survive, with no replacement:**
- `m_on_key_board_callback` (an `LPCSTR` read from the `on_key_board` ini key
  per car section, used to call a named Lua function on
  `OnKeyboardPress`/`Release`) is deleted outright in `CarInput.cpp` -
  replaced by a direct `m_car_drone` dispatch to `CCarDrone_OnKeyboard*`.
  There is no scripted-callback equivalent in the new code.
- Six Lua-exposed `CCar` methods are removed from the `luabind` registration
  in `CarScript.cpp`: `GetFlyWeightAdd`, `SetFlyWeightAdd`, `GetControlEle`,
  `GetControlYaw`, `GetControlPit`, `GetControlRol` - replaced by two new
  ones, `CCarDrone_GetPowerEfficiency`/`CCarDrone_SetPowerEfficiency`, which
  are not drop-in equivalents (different semantics, not just a rename).

This engine repo has no gamedata/scripts to check the private Old World
mod tree from here, and `Car.cpp`/`CAR_NEW` isn't listed as an OWA-specific
divergence area in `CLAUDE.md` (unlike first-person body, GI, Steam Audio,
etc.), so this is plausibly a pure upstream feature Old World's own vehicle
configs never touch. But if any private-repo car/vehicle `.ltx` sets
`on_key_board = <script_function>`, or any Lua script calls
`GetFlyWeightAdd`/`SetFlyWeightAdd`/`GetControlEle`/`GetControlYaw`/
`GetControlPit`/`GetControlRol` on a car object, it will silently stop firing
(the ini key) or hard-error (the removed script methods) after this lands.
→ `dangling_reference` + `playtest` flags. Not a reason to skip - the C++
side is internally consistent and this is a genuine upstream improvement -
but worth a private-repo grep for `on_key_board` and the six removed method
names before or right after applying.

(Side note, not a gotcha: this commit only touches vehicle/Car files plus
`.vcxproj`s, so the `group` heuristic's Build/CI bucket for it is a
by-file-touched artifact, not a real Build/CI change - flagging for anyone
reading the theme label literally.)

## Batch-summarized commits

| Hash | Subject | Take |
|---|---|---|
| `ed5172ac` | `[CSXML_IdToIndex::GetById]` more verbose printing | Adds a "not found" message before the existing assert dump; also swaps `*str_id` for `str_id.c_str()` in the `R_ASSERT3` call (same string, no behavior change). Diagnostics only. |
| `884fae13` | DLTX more accurate cache stats | Reworks `GetCacheStats()` to dedupe `shared_str` interning overhead via a `xr_unordered_flat_set` instead of double-counting section/item string sizes. Accounting only. |
| `0f714ca5` | DLTX: `GetCacheStats` is more accurate | Same function, second refinement pass (fixes which sizeof leaves the set-dedup loop vs. inline). Accounting only. |
| `c20197c4` | DLTX: Remove extra typedef | Drops the redundant `ItemsVec` typedef (alias of `Items`); updates the one caller (`level_sounds.cpp`) to a straight copy-assignment instead of a manual push_back loop. No behavior change. |
| `3a6dbfde` | safer randI | `randI(s32 max)` returns 0 for `max <= 0` instead of `VERIFY(max)`-asserting/UB. Defensive only - only changes behavior for a previously-invalid call. |
| `cd051163` | /Ob3 for giflib on release | Adds `/Ob3` (aggressive inlining) to the vendored giflib project's Release and Release-AVX `ClCompile` blocks. Build flag only. |
| `8427c77e` | xrCriticalSectionTryGuard from mt branch | Purely additive: a new non-blocking RAII guard (`TryEnter`, never blocks) alongside the existing blocking `xrCriticalSectionGuard`. No existing callers changed - nothing currently uses it. |

## Gotchas / follow-ups
- `288eb3ca`: removed `on_key_board` ini-driven Lua callback and 6 renamed/
  removed Lua-exposed `CCar` methods (`GetFlyWeightAdd`, `SetFlyWeightAdd`,
  `GetControlEle`, `GetControlYaw`, `GetControlPit`, `GetControlRol`) with no
  scripted equivalent → `dangling_reference` (check private gamedata tree
  before/at apply) + `playtest` (if any vehicle uses these, behavior
  silently changes or a script errors).
- No `ordering_after` dependencies found - all 11 commits are independent of
  each other and of anything else still pending; the ledger's chronological
  order already matches a safe cherry-pick order.
- No new hot-zone gaps proposed - `xr_ioc_cmd.cpp` and `xrEngine.vcxproj`
  are already covered by the existing hot-zone set, and the `Car`/`CarDrone`
  files aren't a recurring OWA divergence area per `CLAUDE.md`, so no
  registry addition proposed there.
