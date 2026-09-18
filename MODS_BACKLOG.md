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

### Explore a more modern project structure (queued 2026-09-17)
Analyze the codebase for modern practices: interfaces, dependency injection,
separation of concerns, MVVM/MVI/MVC, and better error handling, e.g. a match
that hits a crash ends and returns to the CSS instead of killing the process.
- Touches: ~376k lines of decompiled game code (`src/melee`, `src/sysdolphin`)
  that mirrors the original binary and is synced from upstream; ~14k lines of
  port layer (`src/pc`); ~3k lines of our mod modules (`gmsoccer.c`,
  `gmarena.c`, `gm8player.c`, `mn8css.c`, `mnmouse.c`) plus `#ifdef MELEE_PC`
  hooks in 15 game files.
- Open: which layers are ours to restructure without breaking upstream syncs;
  whether a crashed match can be recovered safely in C, or only logged and
  exited cleanly.

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
- Status 2026-09-17: debug logging removed; **HUD for 8 in a single row,
  confirmed by Chuck** (`eb3df80`); extras now use the real VS AI and the
  match's CPU level (`c68a308`) — they had been Training Mode dummies
  (`cpu_kind = 0`).
- Decision needed from Chuck before the CSS: are players 5-8 human or CPU?
  - CPU: no input work. The CSS needs a way to pick 4 extra CPUs (character
    and level). Small by comparison.
  - Human: input widened at three layers (vendored aurora `PAD_CHANMAX`, SDK
    `PAD_MAX_CONTROLLERS`, `HSD_PadMasterStatus[4]`), which conflicts with
    every upstream merge, plus 8 physical controllers.
  - Either way the vanilla CSS has no art for panels 5-8 (the four doors are
    joints in the CSS model), so extra panels get drawn from code, reusing
    portraits from the disc at runtime.
  - Picking 8 *different* characters also needs ARAM raised past 16 MB; looks
    feasible since MEM1 sits at >= `0x20000000` (see `MEMORY.md`).
- Status 2026-09-17: **CPU panels P5-P8 on the vanilla CSS, confirmed working
  by Chuck** (`46c55d9`). D-pad Left/Right picks a panel, Up/Down cycles the
  character (grid order, Off included), L/R sets level; overlay is
  `resources/eight-player.rml`. ARAM raised to 32 MB (`93a9298`) so the extras
  can be any character. Also added `gm8Player_SanitizeSlots` in the VS scene
  setup so modes that build player lists by hand never see live slots 6-7.
  Confirmed by Chuck the same day: eight *unique* characters load under
  32 MB ARAM, and Classic still works with the slot guard.
- Follow-ups for the panels: hold-to-repeat when cycling characters; the
  mouse wheel sends D-pad taps on the CSS, so scrolling changes the focused
  panel's character.
- Remaining regardless: camera framing for 8; the other ~140 literal `< 6`
  player loops outside `gmvs.c`/`if/`; widescreen HUD spread for 7-8.

### Custom 8-slot character select screen (queued 2026-09-17)
A new CSS for 8-player VS with eight identical panels, each HMN, CPU or off,
replacing the stopgap of four vanilla doors plus four compact CPU panels.
Vanilla feel: everyone grabs characters at once with their own hand.
- Touches: the CSS's whole contract with the game is `CSSData`
  (`src/melee/mn/types.h:307`) wrapping a `VsModeData`; fill
  `vs.start.players[0..7]` and set `pending_scene_change`. `GS_CSS` is shared
  with Classic, Adventure, Event, Giant, Lightning and Camera Mode, so the
  vanilla screen stays and the new one runs only for 8-player VS.
- Open: raise ARAM to 32 MB first so eight different characters load; up to
  four simultaneous hand cursors (input is capped at 4, see the 8-player VS
  ticket); portraits and backdrop reused from the disc at runtime, panels
  drawn in code.
- Status 2026-09-17: **milestone 1 done, confirmed by Chuck** (`7773cc9`..`fc54415`):
  `src/melee/mn/mn8css.c` takes over GS_CSS in 8-player VS only. Vanilla
  backdrop, grid and hand loaded from the disc; an opaque tray covers the old
  player row; eight code-drawn panels labelled with the game's own text; one
  hand (mouse or any stick). A on a panel selects it, A on a portrait assigns
  and advances, X cycles HMN/CPU/Off, L/R level, Start to stage select (full
  matches confirmed), B back to the menu.
- Next: a hand per controller so up to four people pick at once, costumes
  (X/Y like vanilla), teams; then polish (portraits in the panels, hold-B to
  leave).


### Custom CSS: art, animation and sound (queued 2026-09-17)
Make the 8-slot character select screen look and feel like a real Melee menu
instead of a static grid with plain text: character images in the panels,
animations, sounds.
- Touches: the vanilla CSS assets are all loadable from the disc at runtime.
  Door portraits are one texture-animation frame per character on a door-model
  joint (`animateJoint(..., costume_joint, TOBJ_MASK, frame)` in
  `mnCharSel_8025D5AC`); `HSD_JObjLoadJoint` can instance the `door` model
  more than once. Also in `MnSelectChrModels`: `token` (coins), `press_start`
  (Ready to Fight banner). Sounds are the existing `lbAudioAx_800237A8` /
  `80023870` / `80024030` calls in `mncharsel.c`.
- Open: reuse vanilla art (two door-model instances scaled to fit eight) or
  original art (can't ship Nintendo's; new models need an HSDRaw pipeline);
  how much of vanilla's door open/close animation carries over at half size.
- Decided (Chuck, 2026-09-17): do tiers 1 and 2 (sounds, Ready to Fight
  banner, coins, panel motion; real portraits). No original art: the "tier 3"
  look is the vanilla portraits and door animations, just scaled down to fit.
- Decided (Chuck, 2026-09-17): door width scales with how many fighters are
  in, so two players get big doors and eight get narrow ones.
- Open: the door model holds all four doors in one tree, so free per-door
  width and placement may mean positioning each door's joints individually
  rather than scaling two whole instances; width-only scaling would stretch
  the portraits, so decide between uniform scale and cropping.
