# Mods backlog

Work queued for the `mods` branch. Newest decisions first within each item.

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
- Open questions for Chuck: drawn geometry OK for v1 vs modeled stages; slot
  swap vs new icons.
