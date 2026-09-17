# Mods backlog

Work queued for the `mods` branch. Newest decisions first within each item.
Sections are in priority order.

## Dev tools

### Mouse control for menus (queued 2026-09-17; phases 0-1 done and confirmed 2026-09-17, OS cursor hidden in game; phase 2 done and confirmed 2026-09-17)
Drive in-game menus with the mouse for development. No game-side mouse
handling exists today; aurora forwards SDL mouse events only to ImGui/RmlUi
(the F1 menu already takes the mouse).
- Phase 0, plumbing: `src/pc/mouse.c` with `pc_mouse_event` called next to
  `pc_keyboard_event` (`src/pc/vi.c`), ignored while the F1 menu is open.
  Store the cursor in the game's logical 640x480 space (undo aurora's
  letterbox: `calculate_present_viewport` in `extern/aurora/lib/webgpu/gpu.cpp`,
  content size from `AuroraGetRenderSize`). Merge into the port-0 virtual pad
  in `pc_keyboard_apply` (`src/pc/keyboard.c`, next to the touch merge):
  left click = A, right click = B, wheel = up/down (list menus then scroll via
  `mn_80229624`).
- Phase 1, pointer menus: character select hand (`CSSCursorData` xC/x10,
  `mnCharSel_CursorThink`, write before the stick delta is applied) and stage
  select cursor (JObj translate in `fn_8025A310`) follow the mouse while it is
  moving, so stick/keyboard still work. Mouse to menu world coords: calibrate
  against the menu camera with `lbVector_WorldToScreen` (project two z=0
  points, invert the linear map) rather than hardcoding the projection.
  Clicks then pick characters, drop coins and pick stages through the
  existing A/B logic.
- Phase 2, click-to-select in list menus: main menu first (project option
  JObjs, set `mn_804A04F0.hovered_selection`, run the normal hover refresh);
  other `mn*` screens need per-screen hit tests.
  - Done (confirmed by Chuck): `mnMouse_PickRow` (`src/melee/mn/mnmouse.c`)
    projects option JObjs and hit-tests rows; `mnMouse_FilterMenuInput`
    (`mnmain.c`, via `MN_MENU_INPUT()`) covers the `mnmain.c` list menus
    (MENU_KIND_MAIN, 1P, VS, TOY, SETTINGS, DATA, REG, STADIUM, SPECIAL,
    RECORDS), Rules, Additional Rules, Item switch and Stage switch. Hover follows the mouse; a click
    confirms the row under it, a click on a value row acts as Right, a click
    on empty space is dropped.
  - Cursor (confirmed): the stage select cursor model (`MnSlMap` table
    +0x90) is loaded on first mouse movement in the main menu scene and drawn
    on GX link 7, placed under the mouse on a camera-facing plane and scaled to
    its stage select pixel size (`mnMouse_CursorThink` in `mnmain.c`). Hidden
    on stick/D-pad navigation until the mouse moves again.
  - Not covered yet: character/stage select already use phase 1; other
    screens (sound test, name entry grid, tournament, etc.) still take
    wheel/click only.

## Soccer

### Soccer Melee as a Special Melee mode (queued 2026-09-17)
Goal: Soccer is entered from the Special Melee menu instead of a global
launcher toggle, and the soccer arenas exist only inside it — the Soccer stage
select offers nothing but arenas, and no other screen offers arenas.

Where it stands today:
- Soccer is the launcher/F1 pref `prefs.soccer` (`src/pc/launcher.cpp`,
  `src/pc/launcher_data.cpp`) read as `pc_is_soccer_enabled()` (`src/pc/pc.h:68`)
  in `soccer_ConfigureMatch` (`src/melee/gm/gmsoccer.c:1021`), which also
  requires the stage be Final Destination.
- Arena icons are drawn whenever `gm_GetCurrentGameMode() == GM_VS`
  (`arenaSel_Enabled`, `src/melee/mn/mnstagesel.c:113`), so they show up in
  regular VS, and picking one force-starts a soccer match
  (`gmsoccer.c:1011`-`1019`).

Work:
1. Mode: add `GM_SOCCER_VS` to `GameModeKind` (`src/melee/gm/forward.h:19`-`66`),
   appended past the vanilla values so the scene tables don't shift; then walk
   the mode switches (`gmscdata.c` scene data, `gmvs.c:2085`,
   `gmmenumode.c:204`) and give it the same treatment as `GM_LIGHTNING_VS`.
2. Menu row: `SEL_SPECIAL_VS_SOCCER = 10` in `SpecialVsMenuSelection`
   (`src/melee/mn/forward.h:169`), bump `selection_count` for
   `MENU_KIND_SPECIAL` in `mn_803EB6B0` (`mnmain.c:387`), and add the confirm
   case in `mn_8022C4F4` (`mnmain.c:1926`+) doing the usual
   `data->pending_mode = GM_SOCCER_VS; gm_801A4B60()`.
   - Resolve this first: the ten existing rows are DAT-driven art and animation
     (`start_frame + selection * 2`, plus SIS description indices), so an 11th
     row has no text image or animation frame. Either draw the row from code
     (same approach as the arena icons in `mnstagesel.c`) or find a spare frame
     in the menu DAT. The answer decides how much of the rest is worth doing.
3. Gate on the mode, not the pref: `soccer_ConfigureMatch` keys on
   `gm_GetCurrentGameMode() == GM_SOCCER_VS`. Keep `prefs.soccer` as a dev
   shortcut if it's still useful, but it stops being the user-facing path, and
   the "an arena implies soccer" special case at `gmsoccer.c:1011` can go once
   the mode carries the intent.
4. Stage select, both directions:
   - `arenaSel_Enabled()` → `gm_GetCurrentGameMode() == GM_SOCCER_VS`, so the
     arena row disappears from regular VS and everywhere else.
   - In Soccer Melee only arenas are pickable: block cursor movement onto the
     vanilla slots and ignore confirm there (`mnStageSel_804D6CAE`, the slot
     table `mnStageSel_803F06D0`, and Random `0x1D`). v1 can leave the vanilla
     grid drawn but unreachable; nicer is to hide it and lay the arena row out
     on its own.
   - Entering the screen must land the cursor on an arena, not Final
     Destination.

Verify: `ninja -C build-win melee`, then Chuck checks that Special Melee lists
Soccer, its stage select offers only the arenas and starts a soccer match on
each, and regular VS/other modes show no arena icons at all.

### Match rules (queued 2026-09-17)
- Results screen should name the side with more goals as the winner, not the
  stock/KO leader.
- Falling off respawns instead of costing a stock; no eliminations.
- Ignore the VS timer/stock settings so nothing ends the match before the goal
  limit.

### Custom enclosed arenas (queued 2026-09-17)
Goal: a set of soccer fields where the ball can never leave play (floor, side
walls with goal mouths, ceiling, optional platforms), selectable on the stage
select screen.
- Collision + stage params built in code (precedent: `port/src/stage_line.c`
  on chotaling1/melee `pc-port`, a DAT-less stage with native GroundParam and
  MapCollData). melee-pc stores disc structs big-endian, so build them through
  the DISC_STRUCT accessors.
- Visuals, v1: reuse an existing stage background and draw walls, goal frames
  and pitch lines from code (all original, no art pipeline). Later option:
  modeled stages authored with HSDRaw and loaded from the loose `files/`
  directory (`src/pc/file_cache.cpp`).
- Stage select (`src/melee/mn/mnstagesel.c`, icons from `MnSlMap.usd`, slot to
  stage table `mnStageSel_803F06D0`): v1 swaps some slots to soccer fields while
  Soccer is on, with icon art through the texture-pack loader; truly new icons
  in the grid is the larger follow-up.
- Order: one arena behind FD + Soccer on, then an arena kit (each field a
  small data table), then stage select.
- Decided (Chuck, 2026-09-17): drawn geometry is fine for v1; the fields get
  their own new stage select icons, in a row under Final Destination (like
  pre-decomp Melee mods that added icons). No slot swapping.
- Status 2026-09-17: done and confirmed in game. 4 arenas (Classic Pitch, Sky
  Box, Wide Field, Tiny Cage) with drawn icons under the Battlefield/FD row in
  regular VS; picking one starts a soccer match there.
- Follow-ups: nicer arena visuals (textures/backgrounds), arena name on the
  game's own name plate, more fields.

## Unsorted

### 8-player VS (queued 2026-09-17)
VS mode with up to 8 players at once. The character select screen would need to
be updated to match.
- Touches: `GM_MAX_PLAYERS` is 6 (`src/melee/gm/forward.h:12`, with
  `Gm_Player_NumMax` at `:194`) and is baked into the match structs
  (`src/melee/gm/types.h:623`, `:680`, `:683`); the CSS hardcodes 4 ports in
  places (`src/melee/mn/mncharsel.c:3447`, `:5470`).
- Open: how the 5th-8th players are controlled (4 GameCube ports exist, so
  keyboard/extra SDL pads?); whether the CSS gets 8 panels or another layout;
  what happens to team colors, the HUD and the results screen.
- Status 2026-09-17: slot capacity widened, build green (`0d1bd88`). Nothing
  populates the new slots yet, and the game has not been run since the change.
- Findings 2026-09-17:
  - Slots 4-5 are not spare: slot 4 merges every port's input for Camera Mode
    and slot 5 reads none (`src/melee/cm/camera.c:1628`, `:1644`). The four
    extra players are appended at 6-9 so those keep working.
  - `GM_MAX_PLAYERS` also sized an on-disc array (`struct gm_evstage_table`,
    `src/melee/gm/gmevent.c:95`); the DISC_STRUCT assert in `src/pc/disc.h:146`
    caught it. On-disc layouts now use `GM_DISC_MAX_PLAYERS` (still 6).
  - Memory is not the constraint: MEM1 is 96 MB (`src/pc/pc.h:14`) against the
    GameCube's 24 MB, which is what makes 8 loaded characters plausible.
  - Most engine loops already run `i < GM_MAX_PLAYERS` and skip inactive slots
    via `Gm_PKind_NA` (e.g. `src/melee/gm/gmvs.c:563`), so they scale for free.
  - Input is the hard wall: `PAD_CHANMAX` is 4 in vendored aurora
    (`extern/aurora/include/dolphin/pad.h:17`) with ~10 arrays sized by it in
    `pad.cpp`. Players 5-8 need either a widened PAD layer (conflicts with
    upstream merges) or a separate port-side input path.
  - Presentation still assumes 4: HUD (`src/melee/if/ifall.c:132`, `:220`) and
    CSS (`src/melee/mn/mncharsel.c:3447`, `:5470`).
- Status 2026-09-17: **8 fighters run a full match, confirmed in game**
  (`06d6503`). Toggle `eight-player` on the Cheats tab pads a VS match out to 8
  by cloning the characters already selected. Four crashes on the way there;
  the causes are written up in `MEMORY.md`.
- Ceiling found: 8 is the engine's limit, not a choice. A fighter's `player_id`
  is its slot index (`ft/fighter.c:695`) and `pltrick.c:341` asserts it is < 8
  because the hit table is an 8-bit mask, so the slots are 0-7 with no gaps.
  That costs Camera Mode its slots 4-5; knowingly broken for now.
- Also found: character data lives in a 16 MB ARAM pool, so 8 *unique*
  characters will not load. Cloning selected characters avoids it; raising the
  cap means changing how the port distinguishes ARAM from MEM1 pointers
  (`PC_IS_ARAM_ADDR`, `src/pc/disc.h:84`).
- Next, in order:
  1. Remove the per-slot debug logging in `gmvs.c` `fn_8016DCC0`.
  2. HUD for 8 (`src/melee/if/ifall.c:132`, `:220`) — players 5-8 have no
     damage display.
  3. Camera framing for 8 subjects.
  4. The other ~144 literal `< 6` player loops outside `gmvs.c`.
  5. CSS for 8 (`mncharsel.c:3447`, `:5470`) and input past `PAD_CHANMAX 4`
     in vendored aurora — the largest item, and the one that makes the extras
     human-playable rather than CPUs.
