# Gamedata (bundled/distribution) - reviewed 2026-08-05

## Summary

Two-commit gamedata-only batch, both touching `gamedata/scripts/aaaa_script_fixes_mp.script` plus a one- or two-line `unlocalizer_modded_exes_*.ltx` config addition each. Neither touches engine code, neither hits a `hotzones.jsonl` entry, and both are in the same file. The first commit (`da8c1dea`) is a *rearrangement-plus-extension* of content we already have from our own `0dca6ec5` ("fix generate available tasks for warfare traders") — it moves that block earlier in the file and bolts on a second, unrelated new function (`game_object_on_net_destroy_ui_debug_launcher`) plus the matching `RegisterScriptCallback` line. The second commit (`5b705ce4`) is a clean additive feature: scopes-granted-via-upgrades, with an actor-menu highlighter. Net risk is low; both are player-visible, both deserve a playtest pass after apply, and there's a real cherry-pick ordering dependency between them (the second commit's hunks are positioned against the post-first-commit state of the shared file, so the first must land first).

## Hot-zone / deep-read commits

### da8c1dea - Fix https://github.com/themrdemonized/xray-monolith/issues/622

Two changes bundled into one commit, both on `aaaa_script_fixes_mp.script`:

1. **Move the existing `axr_task_manager.generate_available_tasks` WARFARE override** from its current position (line 1502 in our tree) up to line ~1228, immediately after the `utils_item.check_cache` block. The body is byte-identical to what we already have on this branch via our own `0dca6ec5`, so the `-` hunk (delete from old position) and the `+` hunk (insert at new position) should both apply cleanly. No content duplication / double-define risk because it's the same content moving, not two copies.
2. **Add a new `game_object_on_net_destroy_ui_debug_launcher` companion function** in the same moved block: when an object is being network-destroyed, if it matches `ui_debug_launcher.o1` or `o2`, nil those slots so the debug launcher's "stored object" reference doesn't dangle. Plus the matching `RegisterScriptCallback("game_object_on_net_destroy", game_object_on_net_destroy_ui_debug_launcher)` line in `on_game_start`.

Also adds a brand-new `gamedata/configs/unlocalizers/unlocalizer_modded_exes_ui_debug_launcher.ltx` (3 lines: section header `ui_debug_launcher` and two keys `o1`/`o2`) — unlocalizer config, additive, no conflict risk.

Symbol check (no dangling references):
- `game_object_on_net_destroy` callback is registered for many other scripts already (`callbacks_gameobject.script:18`, `aaaa_script_fixes_mp.script:1130`, `aaaa_script_fixes_mp.script:1532` for `track_monsters_on_destroy`).
- `ui_debug_launcher` table is a real, used debug utility (`zz_ui_debug_inputs.script` references `ui_debug_launcher.UIDebug_*` namespaces throughout). The `o1`/`o2` slot fields are new, but they get nil'd safely with `if ui_debug_launcher.o1 and ...` guards, so this is defensive even if the slot is never set.
- `WARFARE` global guard (`_G.WARFARE`) and `warfare.is_warfare_trader(npc)` are the same pattern used elsewhere (`sim_squad_scripted.script:709` already gates on `_G.WARFARE`); not a new coupling.

→ `playtest` (two subtle gameplay-visible behavior changes: WARFARE trader NPCs no longer generate sim tasks, and storing-then-destroying an object via the debug launcher no longer leaves busy hands).

### 5b705ce4 - Possibility of adding "scopes" field via upgrades

New feature letting modders grant scopes to a weapon by putting a `scopes = <comma-separated-section-list>` field on an upgrade's referenced section (e.g. an upgrade that references a new addon section that itself lists scopes). Mechanism:

- Wraps `item_weapon.attach_scope` and `item_weapon.on_item_drag_dropped` to stash the current `item`/`weapon` in module-level locals during the call.
- Augments `item_weapon.check_scope` to, *after* the original returns false, walk the weapon's installed upgrades and check whether any of them grants the candidate addon via the new `get_scopes_from_upgrade(upgr_sec, weapon)` helper (reads the upgrade's `section` pointer, then reads `scopes` from that section, splits on comma).
- Adds `ActorMenu_on_item_focus_receive` callback that, when an equipped weapon receives focus in the actor menu, iterates the weapon's installed upgrades and uses `inventory:highlight_section_in_slot(...)` to mark every granted-scope section across the actor's bag, partner-trade bag, dead-body bag, actor-trade, partner-trade slots.

Plus two new keys in the existing `unlocalizer_modded_exes_item_weapon.ltx` (`check_scope`, `on_item_drag_dropped`) so the LTX parser doesn't choke on them — matches the existing `scopes_table` entry in the same file.

Symbol check (no dangling references):
- `iterate_installed_upgrades` is an engine export: `CScriptGameObject::IterateInstalledUpgrades` registered at `src/xrGame/script_game_object_script3.cpp:513`.
- `highlight_section_in_slot` is an engine export: `CUIActorMenu::HighlightSectionInSlot` registered at `src/xrGame/ui/UIActorMenu_script.cpp:349`.
- `EDDListType` enum already in use (`item_weapon.script:1024`, `:1069`).
- `ActorMenu_on_item_focus_receive` callback is already enumerated in `gamedata/scripts/axr_main.script:186` as a known callback.
- `IsWeapon`, `GetActorMenu`, `SYS_GetParam`, `str_explode` — all standard utility/builtins.

No hot-zone overlap. The monkeypatch style (saving original into `_attach_scope`/`_check_scope`/`_on_item_drag_dropped` locals, then reassigning) is identical to the convention used throughout this file (e.g. `item_weapon.detach_scope` two functions above the new code).

→ `playtest` (player-visible: if gamedata authors use the new field, scopes will appear in the actor's inventory highlight; also a feature requiring gamedata modder awareness to opt into).
→ `ordering_after` on this commit, prerequisite `da8c1deac1710a3e65e316161fecdc852ee51f2a`. The commit's hunks are positioned against the file as it will look *after* `da8c1dea` lands — the `+85` line shift from `da8c1dea`'s insertions has to be in place for this commit's context-line matching to apply cleanly. Even though the two commits don't share any line content, sharing a file with a context-sensitive patch format means the first must land first; ledger order already matches, so this is just defensive confirmation.

## Batch-summarized commits

None — both reviewed in depth because they share a file, the second depends on the first having landed, and both are player-visible.

## Gotchas / follow-ups

- **`ordering_after` on 5b705ce4** → after `da8c1dea`. The second commit's patch hunks assume the file is in the post-first-commit state. Ledger ordering already places `da8c1dea` first, so apply will pick them up in the right order; the flag is belt-and-suspenders for `apply`'s plan step to recognize. → `ordering_after`
- **`playtest` on da8c1dea** — two subtle behavior changes: WARFARE trader NPCs no longer generate sim tasks (the original bug being fixed; the change is genuinely invisible unless you're in WARFARE mode and watching task flow), and the busy-hands fix only matters for users of the debug launcher (effectively nobody outside scripted testing). → `playtest`
- **`playtest` on 5b705ce4** — adds a new opt-in gamedata mechanism (scopes via upgrades) plus a UI highlighter for it. Players won't see anything unless a mod uses the new field; modders need to know about it. → `playtest`
- **No `dangling_reference` findings** — symbol check on every new function and callback passed. The `ui_debug_launcher.o1`/`o2` fields are new but accessed defensively; `iterate_installed_upgrades`, `highlight_section_in_slot`, `EDDListType`, `ActorMenu_on_item_focus_receive`, `SYS_GetParam`, `str_explode`, `IsWeapon`, `GetActorMenu` all verified present in this tree.
- **No `hotzone_gap` proposed** — `gamedata/scripts/aaaa_script_fixes_mp.script` is a gamedata file, not engine code; Old World's `hotzones.jsonl` is intentionally engine-only. The protocol's "this repo's `gamedata/` is bundled/distribution only" rule applies: divergence in this file lives in the private gamedata tree, and reconciling it is `upstream-merge-gamedata`'s job, not a hotzone concern.
- **No `bug_found`** in the diffs themselves — both look intentional and well-scoped.
- **One observation worth noting but not flagging** — `da8c1dea` is a "move + add" hybrid commit. It would have been cleaner as two commits (one to relocate the existing WARFARE override, one to add the busy-hands fix), but that's a stylistic upstream choice, not a reason to skip or flip the verdict. Mention only so the next time we see `aaaa_script_fixes_mp.script` in a conflict during apply, the moving-WARFARE-block context is the first thing to look at.
