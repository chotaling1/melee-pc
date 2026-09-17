#include "gmarena.h"

#include <placeholder.h>

#include <dolphin/gx.h>
#include <dolphin/os.h>

#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/lb/lbcollision.h>
#include <melee/mp/forward.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/forward.h>
#include <sysdolphin/baselib/jobj.h>

#include <string.h>

/* ---- arena definitions ------------------------------------------------
 *
 * Side view, right half (the left half mirrors it). W = half_width,
 * D = goal_depth, H = goal_height, C = ceiling_y, floor at y = 0:
 *
 *   (-W,C) ___________________ (W,C)
 *         |                   |
 *         |                   | (W,H)____ (W+D,H)
 *         |                   |          |
 *   ______|___________________|__________| (W+D,0)
 *
 * The inside is one closed loop of collision lines. Each line's normal
 * (-dy, dx) points into the field, which is what gives it its kind: the
 * floor runs left to right, the right-hand walls run up (CollLine_LeftWall:
 * a surface facing left), ceilings run right to left, the left-hand walls
 * run down (CollLine_RightWall). Platforms are separate drop-through floors.
 */

typedef struct ArenaPlatform {
    f32 x0, x1, y;
} ArenaPlatform;

typedef struct ArenaDef {
    const char* name;
    f32 half_width;
    f32 goal_depth;
    f32 goal_height;
    f32 ceiling_y;
    int platform_count;
    ArenaPlatform platforms[4];
    GXColor floor_color;
    GXColor wall_color;
    GXColor platform_color;
} ArenaDef;

static const ArenaDef arenas[] = {
    {
        "Classic Pitch", 90.0F, 16.0F, 30.0F, 80.0F, 0, { { 0 } },
        { 0x3A, 0x9A, 0x3A, 0xFF }, { 0xE8, 0xE8, 0xE8, 0xFF },
        { 0xC8, 0xC8, 0xC8, 0xFF },
    },
    {
        "Sky Box", 80.0F, 14.0F, 28.0F, 100.0F, 3,
        { { -25.0F, 25.0F, 38.0F }, { -65.0F, -35.0F, 20.0F },
          { 35.0F, 65.0F, 20.0F } },
        { 0x40, 0x70, 0xC0, 0xFF }, { 0xD0, 0xE0, 0xF8, 0xFF },
        { 0xF0, 0xF0, 0xFF, 0xFF },
    },
    {
        "Wide Field", 130.0F, 18.0F, 34.0F, 75.0F, 0, { { 0 } },
        { 0x2E, 0x80, 0x30, 0xFF }, { 0xF0, 0xE0, 0xB0, 0xFF },
        { 0xC8, 0xC8, 0xC8, 0xFF },
    },
    {
        "Tiny Cage", 60.0F, 12.0F, 24.0F, 55.0F, 1,
        { { -20.0F, 20.0F, 25.0F } },
        { 0x80, 0x40, 0x30, 0xFF }, { 0x50, 0x50, 0x58, 0xFF },
        { 0xA0, 0xA0, 0xA8, 0xFF },
    },
};

#define ARENA_COUNT ((int) ARRAY_SIZE(arenas))

/// Thickness of the drawn walls, floor slab and ceiling.
#define WALL_THICKNESS 8.0F

static int arena_current;

int gmArena_Count(void)
{
    return ARENA_COUNT;
}

const char* gmArena_Name(int id)
{
    if (id < 1 || id > ARENA_COUNT) {
        return NULL;
    }
    return arenas[id - 1].name;
}

void gmArena_Select(int id)
{
    arena_current = (id >= 1 && id <= ARENA_COUNT) ? id : 0;
}

int gmArena_Current(void)
{
    return arena_current;
}

static const ArenaDef* arena_Def(void)
{
    return arena_current != 0 ? &arenas[arena_current - 1] : NULL;
}

/* ---- collision ------------------------------------------------------- */

#define LOOP_VERTS 8
#define MAX_VERTS (LOOP_VERTS + 2 * 4)
#define MAX_LINES (LOOP_VERTS + 4)
/// FD's DAT binds its own joint ids to the map gobj and Ground_UpdateMapColl
/// walks them every frame, so there must be at least as many joints as FD
/// has; the extras are empty.
#define MAX_JOINTS 16

/* mpLibLoad keeps pointers into these (and writes to the lines), so they
 * live for the whole match and are rebuilt before each load. */
static DiscVec2 coll_verts[MAX_VERTS];
static MapLine coll_lines[MAX_LINES];
static MapJoint coll_joints[MAX_JOINTS];
static MapCollData coll_data;

typedef struct LoopLine {
    int v0, v1;
    u16 kind;
} LoopLine;

MapCollData* gmArena_BuildColl(MapCollData* fd_coll)
{
    const ArenaDef* a = arena_Def();
    f32 w, d, h, c;
    int fd_joints;
    int loop_id[LOOP_VERTS]; ///< line id assigned to each loop line
    LoopLine loop[LOOP_VERTS];
    int kinds[4] = { CollLine_Floor, CollLine_Ceiling, CollLine_RightWall,
                     CollLine_LeftWall };
    s16 start[4], count[4];
    int nverts, nlines, i, k, p;

    if (a == NULL) {
        return fd_coll;
    }
    w = a->half_width;
    d = a->goal_depth;
    h = a->goal_height;
    c = a->ceiling_y;

    memset(coll_verts, 0, sizeof(coll_verts));
    memset(coll_lines, 0, sizeof(coll_lines));
    memset(coll_joints, 0, sizeof(coll_joints));
    memset(&coll_data, 0, sizeof(coll_data));

    /* Field outline, counterclockwise from the bottom-left goal corner. */
    {
        f32 px[LOOP_VERTS];
        f32 py[LOOP_VERTS];
        px[0] = -(w + d); py[0] = 0.0F;
        px[1] = w + d;    py[1] = 0.0F;
        px[2] = w + d;    py[2] = h;
        px[3] = w;        py[3] = h;
        px[4] = w;        py[4] = c;
        px[5] = -w;       py[5] = c;
        px[6] = -w;       py[6] = h;
        px[7] = -(w + d); py[7] = h;
        for (i = 0; i < LOOP_VERTS; i++) {
            coll_verts[i].x = px[i];
            coll_verts[i].y = py[i];
        }
    }
    loop[0] = (LoopLine){ 0, 1, CollLine_Floor };     // floor, left to right
    loop[1] = (LoopLine){ 1, 2, CollLine_LeftWall };  // right goal back, up
    loop[2] = (LoopLine){ 2, 3, CollLine_Ceiling };   // right goal roof
    loop[3] = (LoopLine){ 3, 4, CollLine_LeftWall };  // right wall, up
    loop[4] = (LoopLine){ 4, 5, CollLine_Ceiling };   // ceiling, right to left
    loop[5] = (LoopLine){ 5, 6, CollLine_RightWall }; // left wall, down
    loop[6] = (LoopLine){ 6, 7, CollLine_Ceiling };   // left goal roof
    loop[7] = (LoopLine){ 7, 0, CollLine_RightWall }; // left goal back, down
    nverts = LOOP_VERTS;

    /* Lines grouped by kind (floors, ceilings, right walls, left walls);
     * platforms go right after the field floor. */
    nlines = 0;
    for (k = 0; k < 4; k++) {
        start[k] = (s16) nlines;
        for (i = 0; i < LOOP_VERTS; i++) {
            if (loop[i].kind == kinds[k]) {
                loop_id[i] = nlines++;
            }
        }
        if (kinds[k] == CollLine_Floor) {
            for (p = 0; p < a->platform_count; p++) {
                const ArenaPlatform* pl = &a->platforms[p];
                MapLine* line = &coll_lines[nlines++];
                coll_verts[nverts].x = pl->x0;
                coll_verts[nverts].y = pl->y;
                coll_verts[nverts + 1].x = pl->x1;
                coll_verts[nverts + 1].y = pl->y;
                line->v0_idx = (u16) nverts;
                line->v1_idx = (u16) (nverts + 1);
                line->prev_id0 = -1;
                line->next_id0 = -1;
                line->prev_id1 = -1;
                line->next_id1 = -1;
                line->hi_flags = CollLine_Floor;
                line->lo_flags = LINE_FLAG_PLATFORM;
                nverts += 2;
            }
        }
        count[k] = (s16) (nlines - start[k]);
    }
    for (i = 0; i < LOOP_VERTS; i++) {
        MapLine* line = &coll_lines[loop_id[i]];
        line->v0_idx = (u16) loop[i].v0;
        line->v1_idx = (u16) loop[i].v1;
        line->prev_id0 = (s16) loop_id[(i + LOOP_VERTS - 1) % LOOP_VERTS];
        line->next_id0 = (s16) loop_id[(i + 1) % LOOP_VERTS];
        line->prev_id1 = -1;
        line->next_id1 = -1;
        line->hi_flags = loop[i].kind;
        line->lo_flags = 0;
    }

    fd_joints = fd_coll != NULL ? fd_coll->joint_count : 1;
    if (fd_joints < 1) {
        fd_joints = 1;
    }
    if (fd_joints > MAX_JOINTS) {
        OSReport("[arena] FD has %d collision joints, padding only %d\n",
                 fd_joints, MAX_JOINTS);
        fd_joints = MAX_JOINTS;
    }
    {
        MapJoint* j = &coll_joints[0];
        j->floor_start = start[0];
        j->floor_count = count[0];
        j->ceiling_start = start[1];
        j->ceiling_count = count[1];
        j->right_wall_start = start[2];
        j->right_wall_count = count[2];
        j->left_wall_start = start[3];
        j->left_wall_count = count[3];
        j->dynamic_start = 0;
        j->dynamic_count = 0;
        j->left_bound = -(w + d) - 20.0F;
        j->bottom_bound = -20.0F;
        j->right_bound = w + d + 20.0F;
        j->top_bound = c + 20.0F;
        j->vtx_start = 0;
        j->vtx_count = (s16) nverts;
    }

    DP_SET(coll_data.verts, coll_verts);
    DP_SET(coll_data.lines, coll_lines);
    DP_SET(coll_data.joints, coll_joints);
    coll_data.vert_count = nverts;
    coll_data.line_count = nlines;
    coll_data.floor_start = start[0];
    coll_data.floor_count = count[0];
    coll_data.ceiling_start = start[1];
    coll_data.ceiling_count = count[1];
    coll_data.right_wall_start = start[2];
    coll_data.right_wall_count = count[2];
    coll_data.left_wall_start = start[3];
    coll_data.left_wall_count = count[3];
    coll_data.dynamic_start = 0;
    coll_data.dynamic_count = 0;
    coll_data.joint_count = fd_joints;
    coll_data.x2C = 0;

    OSReport("[arena] %s: %d verts, %d lines (floor %d+%d ceil %d+%d rwall %d+%d "
             "lwall %d+%d), %d joints (FD had %d), stage scale %.2f\n",
             a->name, nverts, nlines, start[0], count[0], start[1], count[1],
             start[2], count[2], start[3], count[3], fd_joints,
             fd_coll != NULL ? fd_coll->joint_count : -1, Ground_801C0498());
    return &coll_data;
}

/* ---- general points -------------------------------------------------- */

bool gmArena_GetPoint(enum_t id, Vec3* out)
{
    const ArenaDef* a = arena_Def();
    f32 w, d, c;
    static const f32 spawn_frac[4] = { -0.5F, 0.5F, -0.2F, 0.2F };

    if (a == NULL) {
        return false;
    }
    w = a->half_width;
    d = a->goal_depth;
    c = a->ceiling_y;
    out->z = 0.0F;

    if (id >= 0 && id <= 3) { // spawns
        out->x = w * spawn_frac[id];
        out->y = 10.0F;
        return true;
    }
    if (id >= 4 && id <= 7) { // respawn platforms, inside the ceiling
        out->x = w * spawn_frac[id - 4];
        out->y = c - 25.0F;
        return true;
    }
    if (id >= 0x7F && id <= 0x93) { // item spawn points along the floor
        out->x = -0.8F * w + (id - 0x7F) * (1.6F * w / 20.0F);
        out->y = 8.0F;
        return true;
    }
    switch (id) {
    case 0x94: // camera center
        out->x = 0.0F;
        out->y = 0.5F * c;
        return true;
    case 0x95: // camera range corner
        out->x = -(w + d) - 20.0F;
        out->y = c + 20.0F;
        return true;
    case 0x96:
        out->x = w + d + 20.0F;
        out->y = -25.0F;
        return true;
    case 0x97: // blast zone corners, well outside the enclosure
        out->x = -(w + d) - 120.0F;
        out->y = c + 140.0F;
        return true;
    case 0x98:
        out->x = w + d + 120.0F;
        out->y = -140.0F;
        return true;
    }
    return false;
}

/* ---- map model ------------------------------------------------------- */

void gmArena_OnMapInit(HSD_GObj* map_gobj)
{
    if (arena_current == 0 || map_gobj == NULL || map_gobj->hsd_obj == NULL) {
        return;
    }
    // FD's platform is the map gobj's model. Its collision joint binding is
    // skipped (Ground_InitMapColl), so hiding it no longer affects collision.
    HSD_JObjSetFlagsAll(map_gobj->hsd_obj, JOBJ_HIDDEN);
}

void gmArena_GetGoal(gmArenaGoal* goal)
{
    const ArenaDef* a = arena_Def();
    if (a == NULL) {
        return;
    }
    goal->line_x = a->half_width;
    goal->back_x = a->half_width + a->goal_depth;
    goal->bottom_y = -5.0F;
    goal->top_y = a->goal_height;
}

/* ---- drawing ---------------------------------------------------------- */

static void arena_Rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 z, GXColor clr)
{
    Vec3 v0, v1;
    v0.x = x0 < x1 ? x0 : x1;
    v1.x = x0 < x1 ? x1 : x0;
    v0.y = y0 < y1 ? y0 : y1;
    v1.y = y0 < y1 ? y1 : y0;
    v0.z = v1.z = z;
    lbColl_80009DD4(&v0, &v1, &clr);
}

static GXColor arena_Shade(GXColor c, f32 k)
{
    GXColor out = c;
    out.r = (u8) (c.r * k);
    out.g = (u8) (c.g * k);
    out.b = (u8) (c.b * k);
    return out;
}

void gmArena_Draw(void)
{
    const ArenaDef* a = arena_Def();
    const f32 t = WALL_THICKNESS;
    f32 w, d, h, c;
    int side, p;

    if (a == NULL) {
        return;
    }
    w = a->half_width;
    d = a->goal_depth;
    h = a->goal_height;
    c = a->ceiling_y;

    // floor slab with a lighter top edge and a center mark
    arena_Rect(-(w + d + t), -t, w + d + t, 0.0F, -1.0F, a->floor_color);
    arena_Rect(-(w + d + t), -1.2F, w + d + t, 0.0F, -0.5F,
               arena_Shade(a->floor_color, 1.25F));
    arena_Rect(-0.6F, -t, 0.6F, 0.0F, -0.2F, (GXColor){ 0xFF, 0xFF, 0xFF, 0xFF });
    // ceiling
    arena_Rect(-(w + t), c, w + t, c + t, -1.0F, a->wall_color);

    for (side = -1; side <= 1; side += 2) {
        f32 s = (f32) side;
        GXColor dark = arena_Shade(a->wall_color, 0.8F);
        // wall above the goal mouth
        arena_Rect(s * w, h, s * (w + t), c + t, -1.0F, a->wall_color);
        // goal roof and back wall
        arena_Rect(s * w, h, s * (w + d + t), h + t, -1.0F, dark);
        arena_Rect(s * (w + d), -t, s * (w + d + t), h + t, -1.0F, dark);
    }

    for (p = 0; p < a->platform_count; p++) {
        const ArenaPlatform* pl = &a->platforms[p];
        arena_Rect(pl->x0, pl->y - 2.5F, pl->x1, pl->y, -1.0F,
                   a->platform_color);
    }
}
