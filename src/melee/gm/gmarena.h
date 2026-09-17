#ifndef MELEE_GM_ARENA_H
#define MELEE_GM_ARENA_H

/* Soccer arenas (PC only): enclosed fields built on Final Destination. The
 * stage keeps FD's kind, DAT, background and music; its collision, general
 * points (spawns, camera, blast zones, item points) and visible geometry come
 * from the tables in gmarena.c. */

#include <Runtime/platform.h>

#include <dolphin/mtx.h>
#include <melee/gr/forward.h>
#include <melee/mp/forward.h>

typedef struct gmArenaGoal {
    f32 line_x; ///< goal mouth (inner wall) distance from center
    f32 back_x; ///< back of the net distance from center
    f32 bottom_y;
    f32 top_y;
} gmArenaGoal;

int gmArena_Count(void);
const char* gmArena_Name(int id); ///< id in 1..gmArena_Count()

/// Selects the arena for the next match; 0 = none (plain Final Destination).
void gmArena_Select(int id);
int gmArena_Current(void);

/// Ground_801C0800 hook: returns the collision to load instead of FD's.
MapCollData* gmArena_BuildColl(MapCollData* fd_coll);
/// Ground_801C2D24 hook: arena general points (spawns, camera, blast zones,
/// item spawns). Returns false for ids the arena leaves to the stage.
bool gmArena_GetPoint(enum_t id, Vec3* out);
/// grLast map gobj init hook: hides FD's platform model.
void gmArena_OnMapInit(HSD_GObj* map_gobj);
void gmArena_GetGoal(gmArenaGoal* goal);
/// Draws the arena's walls, floor, ceiling and platforms (opaque pass).
void gmArena_Draw(void);

#endif
