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
