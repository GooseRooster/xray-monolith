# Player / first-person body - reviewed 2026-07-26

## Summary
7 commits, 4 hot-zone. Two small independent bugfixes to `player_hud.cpp`
(script-anim part bounds), a two-commit `r__actor_shadow_in_demo_record`
feature in `Actor.cpp` that will **not cherry-pick cleanly** against this
fork's rewritten `AllowActorShadow()`/first-person-body section and needs
manual reapplication, a clean new `CHudMotionCamEffector` API for
script-controlled HUD camera effector cleanup, a trivial one-line `GetAlcohol`
Lua binding, and a HUD-item inertia toggle binding with a swapped-name doc
comment. Overall risk: routine, but flag the two `Actor.cpp` commits for
apply-time care - conceptually compatible, mechanically conflicting.

## Hot-zone / deep-read commits

### aac71290 - `r__actor_shadow_in_demo_record` cvar to disable actor shadow when demo_record 1
Adds a cvar (default `TRUE`, so no default-behavior change) that lets demo
recording suppress the actor's R2 shadow. Touches `CActor::AllowActorShadow()`
in `Actor.cpp` and moves the `FDemoRecord.h` include/`pDemoRecords` extern up
next to it.

Gotcha: this fork's `AllowActorShadow()` region has been substantially
rewritten by the first-person-body work (`dba226d8`, `152784f6` - see
CLAUDE.md's Player/first-person body divergence). The upstream diff's second
hunk touches context (`legs_in_demo_record`, `legs_in_low_crouch`,
`legs_render_attachments_shadow`) that **no longer exists anywhere in this
repo** - that legacy legs system was fully removed/superseded. Confirmed via
grep: zero hits for any of those three globals. The first hunk's context
(the `AllowActorShadow()` body itself) still matches verbatim, so the *idea*
applies cleanly, but the cherry-pick itself will conflict on the second hunk.
Already correctly anticipated in the triage rationale - not a new finding,
just confirmed by full read. → `playtest` (shadow-suppression-during-demo-record
is a small but player/streamer-visible behavior change once the cvar is
flipped) is not needed since default is off-effect (`TRUE` = unchanged
behavior); no flag needed here beyond noting the manual-reapply requirement,
which the ledger rationale already documents in prose.

Verdict: unchanged (take).

### 3d420763 - disable shadow when set_cam_custom_position_direction if engaged and r__actor_shadow_in_demo_record = TRUE
One-line follow-up to aac71290: adds `if (!r__actor_shadow_in_demo_record &&
m_FPCam) return false;` to the same function. Depends entirely on aac71290
having landed first (needs the variable and the function's new first line).
`m_FPCam` / `CFPCamEffector` and the `set_cam_custom_position_direction` Lua
API it refers to both already exist in this fork's `Actor.h`/`Actor.cpp`/
`level_script.cpp`, confirmed by grep - conceptually compatible, same
manual-reapply caveat as its parent. → `ordering_after` (aac71290).

Verdict: unchanged (take).

### d586e563 - add level.remove_hud_motion_cam_effectors
Clean, well-scoped new API: a `IsHudMotionEffector()` virtual on the
`Effector` base (default `false`), a `CHudMotionCamEffector` subclass of
`CAnimatorCamEffector` that overrides it, a `CCameraManager::RemoveHudMotionEffectors()`
sweep over both effector lists, and a Lua binding
(`level.remove_hud_motion_cam_effectors()`). `player_hud.cpp`'s `anim_play`
is changed to instantiate the new subclass instead of the base type - context
matches the current file verbatim (grep-confirmed), so this should cherry-pick
without conflict despite touching a fork-heavy file. No gotchas found.

Verdict: unchanged (take).

### 924762dd - feat(HudItem): lua binding for toggle fl_inertion_enable
Adds a setter (`HudItem.h`'s existing `HudInertionEnabled()` getter gets a
`SetHudInertionEnabled()` counterpart) and Lua bindings
(`hud_inertion_enabled()` / `set_hud_inertion_enabled(bool)`) on
`CScriptGameObject`. Existing `fl_inertion_enable` flag/getter context matches
verbatim - clean apply expected.

Bug in the commit itself (not a reason to skip): the `lua_help_ex.script` doc
comment has the two signatures swapped - it documents
`bool set_hud_inertion_enabled()` (no args, returning bool) and
`void hud_inertion_enabled(bool value)`, backwards from the actual bindings
(`hud_inertion_enabled()` returns bool with no args; `set_hud_inertion_enabled(bool)`
returns void). Doc-only, doesn't affect the binding itself. → `bug_found`.

Verdict: unchanged (take).

## Batch-summarized commits

| hash | subject | take |
|---|---|---|
| 580b91fa | Fix potential "heavy busy hands" on game load (part index OOB) | Real bug fix: old code checked `m_attached_items[part]` for any `part != 2`, including out-of-range values; new code clamps to `part < 2` and warns (unconditionally) on `part > 2`. Context matches current file. |
| e05b6b44 | player_hud::StopScriptAnim() hide warnings under print_bone_warnings flag | Direct follow-up to 580b91fa in the same function; gates the new warning behind the existing `print_bone_warnings` global (confirmed already defined in `script_game_object.cpp`/`console_commands.cpp` - no dangling reference). |
| 5b117b88 | db.actor:cast_Actor():conditions():GetAlcohol() to get alcohol | One-line Lua binding for an already-existing `CActorCondition::GetAlcohol()` getter, plus a trailing-newline fix. Trivial. |

## Gotchas / follow-ups

- **aac71290 / 3d420763 (Actor.cpp, `AllowActorShadow`)**: will not cherry-pick
  cleanly - the removed-legs-globals context these commits patch around no
  longer exists in this fork. Manual reapplication needed against the current
  `AllowActorShadow()` (which sits right before the fork's "OWA: First-Person
  Body" block). The feature itself (opt-out shadow during demo record / custom
  FP cam) is compatible with current code. → `ordering_after` recorded on
  3d420763 (after aac71290).
- **924762dd**: `lua_help_ex.script` doc comment has swapped signatures for
  `hud_inertion_enabled()` / `set_hud_inertion_enabled(bool)`. Not worth
  skipping over, but worth a one-line doc fix whenever this lands. → `bug_found`.
- No new hot-zone registry gaps found - `Actor.cpp` is already correctly
  detected as hot-zone via CLAUDE.md's first-person-body commit citations
  (`dba226d8`, `152784f6`), so no registry addition needed.
- No `dangling_reference` or additional `playtest` flags found in this batch.
