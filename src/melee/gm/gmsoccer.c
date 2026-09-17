#include "gmsoccer.h"
#include "gmarena.h"

#include <placeholder.h>

#include <dolphin/gx.h>
#include <dolphin/os.h>

#include <melee/cm/camera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/forward.h>
#include <melee/gr/stage.h>
#include <melee/it/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/itzako.h>
#include <melee/it/kinds/itdosei.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcollision.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

#include "pc/pc.h"

#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

/* ---- tuning ----------------------------------------------------------
 * Every value can be overridden from soccer.cfg next to melee.exe, which is
 * re-read at the start of each soccer match (and written with these defaults
 * if missing). Each hit, bounce-to-roll change, goal and kickoff is logged to
 * soccer.log in the same folder. */

#define SOCCER_CFG_PATH "soccer.cfg"
#define SOCCER_LOG_PATH "soccer.log"

typedef struct SoccerParam {
    const char* key;
    f32 value;
    const char* help;
} SoccerParam;

enum {
    P_GOALS_TO_WIN,
    P_BALL_PERCENT,
    P_LAUNCH_SCALE,
    P_MIN_LIFT,
    P_GRAVITY_SCALE,
    P_MAX_FALL_SCALE,
    P_AIR_DRAG,
    P_BOUNCE_KEEP,
    P_BOUNCE_ROLL_KEEP,
    P_BOUNCE_MIN_SPEED,
    P_GROUND_FRICTION,
    P_BALL_SCALE,
    P_BALL_AIR_ANIM,
    P_BALL_GROUND_ANIM,
    P_BALL_ANIM_SPEED,
    P_BALL_RADIUS,
    P_DRAW_BALL,
    P_HIDE_SATURN,
    P_AIR_SPIN_KEEP,
    P_SPAWN_Y,
    P_RESPAWN_DELAY,
    P_END_DELAY,
    P_GOAL_LINE_X,
    P_GOAL_BACK_X,
    P_GOAL_BOTTOM_Y,
    P_GOAL_TOP_Y,
    P_SHOW_GOALS,
    P_ARENA,
    P_KICKOFF_COUNTDOWN,
    P_GOAL_BANNER_FRAMES,
    P_GOAL_SFX,
    P_COUNT,
};

static SoccerParam params[P_COUNT] = {
    { "goals_to_win", GM_SOCCER_GOALS_TO_WIN, "first side to this many goals wins" },
    { "ball_percent", 40, "damage % the ball is reset to after every hit (higher = flies further)" },
    { "launch_scale", 1.0F, "multiplier on launch speed from a hit" },
    { "min_lift", 0.2F, "minimum upward speed when a grounded hit lifts the ball" },
    { "gravity_scale", 1.0F, "multiplier on Mr. Saturn's gravity" },
    { "max_fall_scale", 1.0F, "multiplier on Mr. Saturn's max fall speed" },
    { "air_drag", 0.995F, "horizontal speed kept per frame in the air" },
    { "bounce_keep", 0.6F, "vertical speed kept per floor bounce (0 = no bounce, 1 = perfect)" },
    { "bounce_roll_keep", 0.85F, "horizontal speed kept per floor bounce" },
    { "bounce_min_speed", 0.8F, "landing speed below which the ball stops bouncing and rolls" },
    { "ground_friction", 0.02F, "speed lost per frame while rolling" },
    { "ball_scale", 1.0F, "model size" },
    { "ball_air_anim", -1, "Mr. Saturn animation in the air: -1 none (static pose), 0-3" },
    { "ball_ground_anim", -1, "Mr. Saturn animation on the ground: -1 none, 0-3" },
    { "ball_anim_speed", 1.0F, "animation playback speed" },
    { "ball_radius", 0, "drawn ball radius; 0 = fit Mr. Saturn's hurtbox (the part you can hit)" },
    { "draw_ball", 1, "draw the soccer ball model (0/1)" },
    { "hide_saturn", 1, "hide Mr. Saturn's own model on the ball (0/1)" },
    { "air_spin_keep", 0.99F, "spin rate kept per frame in the air" },
    { "spawn_y", 40.0F, "kickoff drop height" },
    { "respawn_delay", 90, "frames between a goal and the next kickoff" },
    { "end_delay", 150, "frames between the winning goal and the end of the match" },
    { "goal_line_x", 86.0F, "goal mouth distance from center (FD ledge is 85.57)" },
    { "goal_back_x", 125.0F, "back of the net distance from center" },
    { "goal_bottom_y", -40.0F, "bottom of the goal" },
    { "goal_top_y", 35.0F, "crossbar height" },
    { "show_goals", 1, "draw the goal frames and nets (0/1)" },
    { "kickoff_countdown", 3, "seconds of 3-2-1 countdown before each kickoff (0 = drop the ball right away)" },
    { "goal_banner_frames", 90, "frames the GOAL! banner stays up (the kickoff countdown replaces it)" },
    { "goal_sfx", 0x13D, "sound played on a goal (317/318/319 are crowd reactions, 0 = none)" },
    { "arena", 0, "soccer arena on Final Destination: 0 = open FD, 1 Classic Pitch, 2 Sky Box, 3 Wide Field, 4 Tiny Cage (goal_* keys are ignored in arenas)" },
};

#define PARAM(id) (params[id].value)

/// Out-of-play limits; the ball is reset without a goal past these.
#define OUT_OF_PLAY_X 200.0F
#define OUT_OF_PLAY_Y -120.0F

enum {
    BALL_MS_AIR,
    BALL_MS_GROUND,
};

enum {
    SIDE_NONE = -1,
    SIDE_LEFT = 0,
    SIDE_RIGHT = 1,
};

static struct {
    bool active;
    bool sides_assigned;
    bool finished;
    Item_GObj* ball;
    int score[2];
    int respawn_timer;
    int end_timer;
    s8 side[4]; ///< goal each player defends, by starting position
    f32 pre_coll_vel_y;
    f32 roll;      ///< ball rotation around the camera axis (radians)
    f32 spin_rate; ///< radians/frame, carried into the air
    int ball_age;  ///< frames since kickoff
    int countdown_timer; ///< frames left in the kickoff countdown
    int countdown_shown; ///< number currently on the banner
    int banner_timer;    ///< frames left on the banner (-1 = until replaced)
    int banner_side;     ///< -1 neutral, SIDE_LEFT red, SIDE_RIGHT blue
    char banner[32];
    u32 frame;
    u32 draw_passes_seen;
    FILE* log;
} soccer;

/* ---- banner + sounds -------------------------------------------------- */

/// Menu sound effects (see sfxMove/sfxForward in src/melee/mn/inlines.h).
#define SFX_MENU_FORWARD 1
#define SFX_MENU_MOVE 2

static void soccer_Banner(const char* text, int side, int frames)
{
    snprintf(soccer.banner, sizeof(soccer.banner), "%s", text);
    soccer.banner_side = side;
    soccer.banner_timer = frames;
}

/* ---- log + config ---------------------------------------------------- */

static void soccer_Log(const char* fmt, ...)
{
    va_list args;
    char buf[256];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OSReport("[soccer] f%u %s\n", soccer.frame, buf);
    if (soccer.log != NULL) {
        fprintf(soccer.log, "f%u %s\n", soccer.frame, buf);
        fflush(soccer.log);
    }
}

static void soccer_WriteDefaultConfig(void)
{
    int i;
    FILE* f = fopen(SOCCER_CFG_PATH, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# Soccer mod tuning. Edit and start a new match to apply.\n"
               "# Distances are in stage units (FD's stage is 171 wide).\n"
               "# Delete this file to restore the defaults.\n\n");
    for (i = 0; i < P_COUNT; i++) {
        fprintf(f, "# %s\n%s = %g\n\n", params[i].help, params[i].key,
                params[i].value);
    }
    fclose(f);
}

static void soccer_LoadConfig(void)
{
    static f32 defaults[P_COUNT];
    static bool have_defaults;
    char line[256];
    FILE* f;
    int k;

    // Start from the built-in values so a removed key falls back to them.
    for (k = 0; k < P_COUNT; k++) {
        if (!have_defaults) {
            defaults[k] = params[k].value;
        }
        params[k].value = defaults[k];
    }
    have_defaults = true;

    f = fopen(SOCCER_CFG_PATH, "r");
    if (f == NULL) {
        soccer_WriteDefaultConfig();
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[64];
        float value;
        int i;
        if (line[0] == '#' ||
            sscanf(line, " %63[a-z_] = %f", key, &value) != 2)
        {
            continue;
        }
        for (i = 0; i < P_COUNT; i++) {
            if (strcmp(key, params[i].key) == 0) {
                params[i].value = value;
                break;
            }
        }
        if (i == P_COUNT) {
            soccer_Log("config: unknown key '%s'", key);
        }
    }
    fclose(f);
}

/* ---- ball item ------------------------------------------------------- */

static void ball_EnterAir(Item_GObj* gobj);
static void ball_EnterGround(Item_GObj* gobj);
static void ball_GetShape(Item_GObj* gobj, Vec3* center, f32* radius);

static void ball_KeepHarmless(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    // Mr. Saturn's animations may enable his hitbox; the ball never hurts.
    if (ip->xDC8_word.flags.x16) {
        it_802725D4(gobj);
    }
    ip->xDC8_word.flags.x15 = 0;
    ip->xD44_lifeTimer = 1000000.0F;
    ip->owner = NULL;
    if (PARAM(P_HIDE_SATURN) != 0.0F) {
        HSD_JObjSetFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
    }
}

/// Where the drawn ball sits: centered on Mr. Saturn's hurtbox so the model
/// matches what can be hit, sized to it unless ball_radius overrides.
static void ball_GetShape(Item_GObj* gobj, Vec3* center, f32* radius)
{
    Item* ip = GET_ITEM(gobj);
    f32 r = PARAM(P_BALL_RADIUS);
    if (ip->xAC8_hurtboxNum > 0 && ip->xACC_itemHurtbox[0].bone != NULL) {
        HurtCapsule* hurt = &ip->xACC_itemHurtbox[0];
        Vec3 a, b;
        f32 dx, dy;
        // HurtCapsule::a_pos/b_pos are only refreshed while an attack is
        // being tested against the item (lbcollision.c), so they go stale
        // between hits. Resolve the capsule from its bone every time, as the
        // collision code does.
        lb_8000B1CC(hurt->bone, &hurt->a_offset, &a);
        lb_8000B1CC(hurt->bone, &hurt->b_offset, &b);
        dx = b.x - a.x;
        dy = b.y - a.y;
        center->x = 0.5F * (a.x + b.x);
        center->y = 0.5F * (a.y + b.y);
        center->z = 0.0F;
        if (r <= 0.0F) {
            r = hurt->scale * ip->scl + 0.5F * sqrtf(dx * dx + dy * dy);
        }
    } else {
        if (r <= 0.0F) {
            r = 4.0F * PARAM(P_BALL_SCALE);
        }
        center->x = ip->pos.x;
        center->y = ip->pos.y + r;
        center->z = 0.0F;
    }
    *radius = r;
}

static bool ball_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 center;
    f32 radius;

    ball_KeepHarmless(gobj);
    ball_GetShape(gobj, &center, &radius);
    if (radius > 0.0F) {
        if (ip->ground_or_air == GA_Ground) {
            // Rolling without slipping: moving right turns clockwise as seen
            // by the camera, i.e. negative rotation around Z.
            soccer.spin_rate = -ip->x40_vel.x / radius;
        } else {
            soccer.spin_rate *= PARAM(P_AIR_SPIN_KEEP);
        }
        soccer.roll += soccer.spin_rate;
        HSD_JObjSetRotationZ(GET_JOBJ(gobj), soccer.roll);
    }
    return false;
}

static void ball_AirPhys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ItemAttr* attrs = ip->xCC_item_attr;
    it_80272860(gobj, attrs->x10_fall_speed * PARAM(P_GRAVITY_SCALE),
                attrs->x14_fall_speed_max * PARAM(P_MAX_FALL_SCALE));
    ip->x40_vel.x *= PARAM(P_AIR_DRAG);
}

static void ball_Landed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    f32 fall = -soccer.pre_coll_vel_y;
    if (fall > PARAM(P_BOUNCE_MIN_SPEED)) {
        ip->x40_vel.y = fall * PARAM(P_BOUNCE_KEEP);
        ip->x40_vel.x *= PARAM(P_BOUNCE_ROLL_KEEP);
        it_802762BC(ip);
        return;
    }
    soccer_Log("rolling at (%.1f, %.1f) vx %.2f", ip->pos.x, ip->pos.y,
               ip->x40_vel.x);
    ip->x40_vel.y = 0.0F;
    ball_EnterGround(gobj);
}

static bool ball_AirColl(Item_GObj* gobj)
{
    soccer.pre_coll_vel_y = GET_ITEM(gobj)->x40_vel.y;
    it_8026E414(gobj, ball_Landed);
    return false;
}

static void ball_GroundPhys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    f32 friction = PARAM(P_GROUND_FRICTION);
    f32 vx = ip->x40_vel.x;
    if (vx > friction) {
        vx -= friction;
    } else if (vx < -friction) {
        vx += friction;
    } else {
        vx = 0.0F;
    }
    ip->x40_vel.x = vx;
    ip->x40_vel.y = 0.0F;
}

static bool ball_GroundColl(Item_GObj* gobj)
{
    it_8026D62C(gobj, ball_EnterAir);
    return false;
}

static ItemStateTable ball_states[] = {
    /* BALL_MS_AIR */ { -1, ball_Anim, ball_AirPhys, ball_AirColl },
    /* BALL_MS_GROUND */ { -1, ball_Anim, ball_GroundPhys, ball_GroundColl },
};

static void ball_SetState(Item_GObj* gobj, int msid)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Item_80268E5C(gobj, msid, ITEM_ANIM_UPDATE);
    ip->x5D0_animFrameSpeed = PARAM(P_BALL_ANIM_SPEED);
    lb_8000BA0C(jobj, ip->x5D0_animFrameSpeed);
    // The state change re-applies the facing rotation; keep the roll.
    HSD_JObjSetRotationZ(jobj, soccer.roll);
    ball_KeepHarmless(gobj);
}

static void ball_EnterAir(Item_GObj* gobj)
{
    it_802762BC(GET_ITEM(gobj));
    ball_SetState(gobj, BALL_MS_AIR);
}

static void ball_EnterGround(Item_GObj* gobj)
{
    it_802762B0(GET_ITEM(gobj));
    ball_SetState(gobj, BALL_MS_GROUND);
}

static bool ball_DmgReceived(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    // Read the hit before the state change clears xCC8_knockback.
    bool lifted = it_8027B798(gobj, &ip->x40_vel);
    ip->x40_vel.x *= PARAM(P_LAUNCH_SCALE);
    ip->x40_vel.y *= PARAM(P_LAUNCH_SCALE);
    ip->x40_vel.z = 0.0F;
    soccer_Log("hit by P%d: kb %.1f angle %d dmg %d %s -> vel (%.2f, %.2f) "
               "at (%.1f, %.1f)",
               ip->xCB0_source_ply + 1, ip->xCC8_knockback, ip->xCAC_angle,
               ip->xCA0, ip->ground_or_air == GA_Air ? "air" : "ground",
               ip->x40_vel.x, ip->x40_vel.y, ip->pos.x, ip->pos.y);
    ip->xC9C = (s32) PARAM(P_BALL_PERCENT);
    if (ip->ground_or_air == GA_Air || lifted) {
        if (ip->ground_or_air == GA_Ground &&
            ip->x40_vel.y < PARAM(P_MIN_LIFT))
        {
            ip->x40_vel.y = PARAM(P_MIN_LIFT);
        }
        ball_EnterAir(gobj);
    } else {
        ball_EnterGround(gobj);
    }
    return false;
}

static void ball_Destroyed(Item_GObj* gobj)
{
    if (soccer.ball == gobj) {
        soccer.ball = NULL;
    }
}

static ItemLogicTable ball_logic = {
    ball_states,
    NULL,             // spawned
    ball_Destroyed,   // destroyed
    NULL,             // picked_up: no pickup, catch, or carry (Item_IsGrabbable)
    NULL,             // dropped
    NULL,             // thrown
    NULL,             // dmg_dealt
    ball_DmgReceived, // dmg_received
    ball_EnterAir,    // entered_air
    NULL,             // reflected
    NULL,             // clanked
    NULL,             // absorbed
    NULL,             // shield_bounced
    NULL,             // hit_shield
    itDosei_Logic7_EvtUnk,
};

static Item_GObj* ball_Spawn(void)
{
    SpawnItem spawn;
    Item_GObj* gobj;
    Item* ip;

    memset(&spawn, 0, sizeof(spawn));
    spawn.kind = It_Kind_Dosei;
    spawn.pos.x = 0.0F;
    spawn.pos.y = PARAM(P_SPAWN_Y);
    spawn.prev_pos = spawn.pos;
    spawn.facing_dir = 1.0F;
    spawn.x44_flag.b0 = 1;
    gobj = Item_80268B18(&spawn);
    if (gobj == NULL) {
        soccer_Log("ball spawn failed");
        return NULL;
    }
    // Mr. Saturn's own spawn callback has run; from here on this instance
    // uses the ball's callbacks and states. Other Mr. Saturns are untouched.
    ip = GET_ITEM(gobj);
    ip->xB8_itemLogicTable = &ball_logic;
    ip->xBC_itemStateContainer = ball_states;
    ip->xC9C = (s32) PARAM(P_BALL_PERCENT);
    itResetVelocity(ip);
    soccer.roll = 0.0F;
    soccer.spin_rate = 0.0F;
    soccer.ball_age = 0;
    it_80274484(gobj, GET_JOBJ(gobj), PARAM(P_BALL_SCALE));
    ball_EnterAir(gobj);
    soccer_Log("kickoff, score %d-%d (base gravity %.3f, max fall %.2f)",
               soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT],
               ip->xCC_item_attr->x10_fall_speed,
               ip->xCC_item_attr->x14_fall_speed_max);
    return gobj;
}

/* ---- ball model ------------------------------------------------------
 * A truncated icosahedron (12 black pentagons, 20 white hexagons) projected
 * onto a sphere, generated at first use. Each face is fanned from its center
 * and each fan triangle split in four, all vertices normalized, so the
 * silhouette reads as round. Shading is a fixed world-space light computed on
 * the CPU into vertex colors. */

#define BALL_MAX_TRIS 720
static f32 ball_mesh_pos[BALL_MAX_TRIS * 3][3];
static u8 ball_mesh_black[BALL_MAX_TRIS * 3];
static int ball_mesh_verts;

static void vnorm(f32* v)
{
    f32 len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

static void ball_MeshTri(const f32* a, const f32* b, const f32* c, u8 black)
{
    const f32* tri[3];
    int i;
    tri[0] = a;
    tri[1] = b;
    tri[2] = c;
    for (i = 0; i < 3 && ball_mesh_verts < BALL_MAX_TRIS * 3; i++) {
        ball_mesh_pos[ball_mesh_verts][0] = tri[i][0];
        ball_mesh_pos[ball_mesh_verts][1] = tri[i][1];
        ball_mesh_pos[ball_mesh_verts][2] = tri[i][2];
        vnorm(ball_mesh_pos[ball_mesh_verts]);
        ball_mesh_black[ball_mesh_verts] = black;
        ball_mesh_verts++;
    }
}

/// Fan triangle split in four with midpoints pushed onto the sphere.
static void ball_MeshFanTri(const f32* a, const f32* b, const f32* c, u8 black)
{
    f32 ab[3], bc[3], ca[3];
    int i;
    for (i = 0; i < 3; i++) {
        ab[i] = 0.5F * (a[i] + b[i]);
        bc[i] = 0.5F * (b[i] + c[i]);
        ca[i] = 0.5F * (c[i] + a[i]);
    }
    vnorm(ab);
    vnorm(bc);
    vnorm(ca);
    ball_MeshTri(a, ab, ca, black);
    ball_MeshTri(ab, b, bc, black);
    ball_MeshTri(ca, bc, c, black);
    ball_MeshTri(ab, bc, ca, black);
}

static void ball_MeshFace(f32 corners[][3], int n, u8 black)
{
    f32 center[3] = { 0, 0, 0 };
    int i, k;
    for (i = 0; i < n; i++) {
        vnorm(corners[i]);
        for (k = 0; k < 3; k++) {
            center[k] += corners[i][k];
        }
    }
    vnorm(center);
    for (i = 0; i < n; i++) {
        ball_MeshFanTri(center, corners[i], corners[(i + 1) % n], black);
    }
}

static void ball_BuildMesh(void)
{
    const f32 phi = 1.618034F;
    f32 ico[12][3] = {
        { -1, phi, 0 }, { 1, phi, 0 },  { -1, -phi, 0 }, { 1, -phi, 0 },
        { 0, -1, phi }, { 0, 1, phi },  { 0, -1, -phi }, { 0, 1, -phi },
        { phi, 0, -1 }, { phi, 0, 1 },  { -phi, 0, -1 }, { -phi, 0, 1 },
    };
    bool adj[12][12];
    int a, b, c, k;

    // Icosahedron edges have length 2 for these coordinates.
    for (a = 0; a < 12; a++) {
        for (b = 0; b < 12; b++) {
            f32 d = 0;
            for (k = 0; k < 3; k++) {
                d += (ico[a][k] - ico[b][k]) * (ico[a][k] - ico[b][k]);
            }
            adj[a][b] = a != b && d < 4.5F;
        }
    }

    // White hexagons: one per icosahedron face, corners at the edge thirds.
    for (a = 0; a < 12; a++) {
        for (b = a + 1; b < 12; b++) {
            for (c = b + 1; c < 12; c++) {
                const f32* v[3];
                f32 hex[6][3];
                int e;
                if (!adj[a][b] || !adj[b][c] || !adj[a][c]) {
                    continue;
                }
                v[0] = ico[a];
                v[1] = ico[b];
                v[2] = ico[c];
                for (e = 0; e < 3; e++) {
                    const f32* p = v[e];
                    const f32* q = v[(e + 1) % 3];
                    for (k = 0; k < 3; k++) {
                        hex[e * 2][k] = p[k] + (q[k] - p[k]) / 3.0F;
                        hex[e * 2 + 1][k] = p[k] + 2.0F * (q[k] - p[k]) / 3.0F;
                    }
                }
                ball_MeshFace(hex, 6, 0);
            }
        }
    }

    // Black pentagons: one per icosahedron vertex, corners a third of the way
    // to each neighbor, ordered by angle around the vertex.
    for (a = 0; a < 12; a++) {
        f32 pent[5][3];
        f32 ang[5];
        f32 n[3], t1[3], t2[3];
        int count = 0;
        int i, j;

        for (k = 0; k < 3; k++) {
            n[k] = ico[a][k];
        }
        vnorm(n);
        // any vector not parallel to n, crossed with n, gives a tangent
        t1[0] = n[1];
        t1[1] = -n[0];
        t1[2] = 0.0F;
        if (ABS(n[2]) > 0.9F) {
            t1[0] = 0.0F;
            t1[1] = n[2];
            t1[2] = -n[1];
        }
        vnorm(t1);
        t2[0] = n[1] * t1[2] - n[2] * t1[1];
        t2[1] = n[2] * t1[0] - n[0] * t1[2];
        t2[2] = n[0] * t1[1] - n[1] * t1[0];

        for (b = 0; b < 12 && count < 5; b++) {
            f32 d[3];
            if (!adj[a][b]) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                pent[count][k] = ico[a][k] + (ico[b][k] - ico[a][k]) / 3.0F;
                d[k] = pent[count][k] - ico[a][k];
            }
            ang[count] = atan2f(d[0] * t2[0] + d[1] * t2[1] + d[2] * t2[2],
                                d[0] * t1[0] + d[1] * t1[1] + d[2] * t1[2]);
            count++;
        }
        for (i = 1; i < count; i++) {
            for (j = i; j > 0 && ang[j - 1] > ang[j]; j--) {
                f32 ta = ang[j];
                f32 tp[3];
                ang[j] = ang[j - 1];
                ang[j - 1] = ta;
                for (k = 0; k < 3; k++) {
                    tp[k] = pent[j][k];
                    pent[j][k] = pent[j - 1][k];
                    pent[j - 1][k] = tp[k];
                }
            }
        }
        ball_MeshFace(pent, count, 1);
    }
}

static void soccer_DrawBall(void)
{
    static const f32 light[3] = { -0.37F, 0.58F, 0.73F };
    Item_GObj* gobj = soccer.ball;
    Mtx view;
    Vec3 center;
    f32 radius, c, s;
    int i;

    if (gobj == NULL || PARAM(P_DRAW_BALL) == 0.0F) {
        return;
    }
    if (ball_mesh_verts == 0) {
        ball_BuildMesh();
        soccer_Log("ball mesh: %d triangles", ball_mesh_verts / 3);
    }
    ball_GetShape(gobj, &center, &radius);
    c = cosf(soccer.roll);
    s = sinf(soccer.roll);

    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_DISABLE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_ENABLE);
    GXSetZCompLoc(GX_ENABLE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    HSD_ClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetCurrentMtx(0);
    HSD_CObjGetViewingMtx(HSD_CObjGetCurrent(), view);
    GXLoadPosMtxImm(view, 0);

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, ball_mesh_verts);
    for (i = 0; i < ball_mesh_verts; i++) {
        const f32* p = ball_mesh_pos[i];
        // roll around the camera (Z) axis; the unit position is the normal
        f32 nx = c * p[0] - s * p[1];
        f32 ny = s * p[0] + c * p[1];
        f32 nz = p[2];
        f32 diffuse = nx * light[0] + ny * light[1] + nz * light[2];
        f32 shade = 0.4F + 0.6F * (diffuse > 0.0F ? diffuse : 0.0F);
        u8 base = ball_mesh_black[i] ? 0x28 : 0xF0;
        u8 v = (u8) (base * shade);
        GXPosition3f32(center.x + radius * nx, center.y + radius * ny,
                       center.z + radius * nz);
        GXColor4u8(v, v, v, 0xFF);
    }
    GXEnd();

    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
}

/* ---- goals ----------------------------------------------------------- */

static void goal_Rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 z, GXColor* clr)
{
    Vec3 v0, v1;
    v0.x = x0 < x1 ? x0 : x1;
    v1.x = x0 < x1 ? x1 : x0;
    v0.y = y0 < y1 ? y0 : y1;
    v1.y = y0 < y1 ? y1 : y0;
    v0.z = v1.z = z;
    lbColl_80009DD4(&v0, &v1, clr);
}

/// Draws both goals: a translucent net in the team color and a white frame.
/// Pass 0 is the opaque pass and pass 2 the translucent one, as for the
/// game's own hitbox display (lbColl_80009F54).
static void soccer_DrawGoals(HSD_GObj* gobj, int pass)
{
    static GXColor nets[2] = { { 0xE0, 0x30, 0x30, 0x60 },
                               { 0x30, 0x60, 0xF0, 0x60 } };
    static GXColor frame = { 0xFF, 0xFF, 0xFF, 0xFF };
    f32 line = PARAM(P_GOAL_LINE_X);
    f32 back = PARAM(P_GOAL_BACK_X);
    f32 bottom = PARAM(P_GOAL_BOTTOM_Y);
    f32 top = PARAM(P_GOAL_TOP_Y);
    const f32 bar = 1.5F;
    int side;

    if (!soccer.active) {
        return;
    }
    if (pass >= 0 && pass < 8 && !(soccer.draw_passes_seen & (1 << pass))) {
        soccer.draw_passes_seen |= 1 << pass;
        soccer_Log("goal draw pass %d", pass);
    }
    if (pass == 0) {
        gmArena_Draw();
        soccer_DrawBall();
    }
    if (PARAM(P_SHOW_GOALS) == 0.0F) {
        return;
    }
    for (side = SIDE_LEFT; side <= SIDE_RIGHT; side++) {
        f32 s = side == SIDE_LEFT ? -1.0F : 1.0F;
        if (pass == 2) {
            goal_Rect(s * line, bottom, s * back, top, -6.0F, &nets[side]);
        } else if (pass == 0) {
            // posts at the goal line and the back of the net, crossbar, base
            goal_Rect(s * line, bottom, s * (line + bar), top + bar, 1.0F,
                      &frame);
            goal_Rect(s * (back - bar), bottom, s * back, top + bar, 1.0F,
                      &frame);
            goal_Rect(s * line, top, s * back, top + bar, 1.0F, &frame);
            goal_Rect(s * line, bottom, s * back, bottom + bar, 1.0F, &frame);
        }
    }
}

/* ---- match ----------------------------------------------------------- */

static void soccer_AssignSides(void)
{
    int i;
    bool any = false;
    for (i = 0; i < 4; i++) {
        HSD_GObj* gobj;
        soccer.side[i] = SIDE_NONE;
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        gobj = Player_GetEntity(i);
        if (gobj == NULL) {
            continue;
        }
        soccer.side[i] =
            GET_FIGHTER(gobj)->cur_pos.x < 0.0F ? SIDE_LEFT : SIDE_RIGHT;
        soccer_Log("P%d defends the %s goal", i + 1,
                   soccer.side[i] == SIDE_LEFT ? "left" : "right");
        any = true;
    }
    soccer.sides_assigned = any;
}

static void soccer_Goal(int scoring_side, Vec3* pos)
{
    soccer.score[scoring_side]++;
    soccer_Log("GOAL for %s at (%.1f, %.1f): %d-%d",
               scoring_side == SIDE_LEFT ? "left" : "right", pos->x, pos->y,
               soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT]);
    Camera_RequestQuake(QuakeKind_Small, pos);
    if (PARAM(P_GOAL_SFX) > 0.0F) {
        lbAudioAx_8002411C((int) PARAM(P_GOAL_SFX));
    }
    if (soccer.score[scoring_side] >= (int) PARAM(P_GOALS_TO_WIN)) {
        soccer.finished = true;
        soccer.end_timer = (int) PARAM(P_END_DELAY);
        soccer_Log("%s side wins", scoring_side == SIDE_LEFT ? "left" : "right");
        soccer_Banner(scoring_side == SIDE_LEFT ? "RED WINS!" : "BLUE WINS!",
                      scoring_side, -1);
    } else {
        soccer_Banner("GOAL!", scoring_side, (int) PARAM(P_GOAL_BANNER_FRAMES));
    }
}

static void soccer_Kickoff(void)
{
    soccer.ball = ball_Spawn();
    soccer.respawn_timer = (int) PARAM(P_RESPAWN_DELAY);
    if (soccer.ball != NULL && PARAM(P_KICKOFF_COUNTDOWN) > 0.0F) {
        soccer_Banner("KICK OFF!", -1, 45);
        lbAudioAx_80024030(SFX_MENU_FORWARD);
    }
}

static void soccer_Think(HSD_GObj* unused)
{
    soccer.frame++;
    if (soccer.banner_timer > 0 && --soccer.banner_timer == 0) {
        soccer.banner[0] = '\0';
    }
    if (!soccer.sides_assigned) {
        soccer_AssignSides();
    }

    if (soccer.finished) {
        if (soccer.end_timer > 0 && --soccer.end_timer == 0) {
            gm_8016B33C(5);
            gm_8016B328();
        }
    }

    if (soccer.ball != NULL) {
        Item* ip = GET_ITEM(soccer.ball);
        Vec3 pos = ip->pos;
        if (++soccer.ball_age == 10 || soccer.ball_age == 120) {
            Vec3 center;
            f32 radius;
            ball_GetShape(soccer.ball, &center, &radius);
            soccer_Log("ball age %d: pos (%.1f, %.1f) hurtboxes %d, drawn at "
                       "(%.1f, %.1f) r %.2f",
                       soccer.ball_age, pos.x, pos.y, ip->xAC8_hurtboxNum,
                       center.x, center.y, radius);
        }
        f32 ax = ABS(pos.x);
        bool in_goal = ax > PARAM(P_GOAL_LINE_X) &&
                       ax < PARAM(P_GOAL_BACK_X) &&
                       pos.y > PARAM(P_GOAL_BOTTOM_Y) &&
                       pos.y < PARAM(P_GOAL_TOP_Y);
        bool out = ax > OUT_OF_PLAY_X || pos.y < OUT_OF_PLAY_Y;
        if (in_goal || out) {
            if (in_goal && !soccer.finished) {
                // A ball in the left goal scores for the right side.
                soccer_Goal(pos.x < 0.0F ? SIDE_RIGHT : SIDE_LEFT, &pos);
            } else if (out) {
                soccer_Log("ball out of play at (%.1f, %.1f)", pos.x, pos.y);
            }
            Item_8026A8EC(soccer.ball);
            soccer.ball = NULL;
            soccer.respawn_timer = (int) PARAM(P_RESPAWN_DELAY);
        }
        return;
    }

    if (soccer.finished) {
        return;
    }
    if (soccer.countdown_timer > 0) {
        int n = (soccer.countdown_timer + 59) / 60;
        if (n != soccer.countdown_shown) {
            char text[4];
            snprintf(text, sizeof(text), "%d", n);
            soccer_Banner(text, -1, -1);
            soccer.countdown_shown = n;
            lbAudioAx_80024030(SFX_MENU_MOVE);
        }
        if (--soccer.countdown_timer == 0) {
            soccer_Kickoff();
        }
        return;
    }
    if (soccer.respawn_timer > 0) {
        soccer.respawn_timer--;
        return;
    }
    if (PARAM(P_KICKOFF_COUNTDOWN) > 0.0F) {
        soccer.countdown_timer = (int) (PARAM(P_KICKOFF_COUNTDOWN) * 60.0F);
        soccer.countdown_shown = 0;
        return;
    }
    soccer_Kickoff();
}

static void soccer_OnMatchStart(void)
{
    HSD_GObj* gobj;
    int i;

    if (soccer.log != NULL) {
        fclose(soccer.log);
    }
    memset(&soccer, 0, sizeof(soccer));
    soccer.log = fopen(SOCCER_LOG_PATH, "w");
    soccer_LoadConfig();
    if (gmArena_Current() != 0) {
        gmArenaGoal goal;
        gmArena_GetGoal(&goal);
        PARAM(P_GOAL_LINE_X) = goal.line_x;
        PARAM(P_GOAL_BACK_X) = goal.back_x;
        PARAM(P_GOAL_BOTTOM_Y) = goal.bottom_y;
        PARAM(P_GOAL_TOP_Y) = goal.top_y;
    }
    soccer.active = true;
    soccer.respawn_timer = 60;
    ball_states[BALL_MS_AIR].anim_id = (enum_t) PARAM(P_BALL_AIR_ANIM);
    ball_states[BALL_MS_GROUND].anim_id = (enum_t) PARAM(P_BALL_GROUND_ANIM);

    soccer_Log("match start, arena %d (%s)", gmArena_Current(),
               gmArena_Current() != 0 ? gmArena_Name(gmArena_Current())
                                      : "open Final Destination");
    for (i = 0; i < P_COUNT; i++) {
        soccer_Log("  %s = %g", params[i].key, params[i].value);
    }

    gobj = GObj_Create(0xF, 0x11, 0);
    HSD_GObj_SetupProc(gobj, soccer_Think, 0x15);
    GObj_SetupGXLink(gobj, soccer_DrawGoals, 6, 0);
}

static void soccer_OnMatchEnd(u8 outcome)
{
    soccer_Log("match end (outcome %d), final score %d-%d", outcome,
               soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT]);
    soccer.active = false;
    soccer.ball = NULL;
    gmArena_Select(0);
    if (soccer.log != NULL) {
        fclose(soccer.log);
        soccer.log = NULL;
    }
}

void gmSoccer_ConfigureMatch(StartMeleeData* start)
{
    int picked_arena = gmArena_TakePending();

    soccer.active = false;
    gmArena_Select(0);
    // An arena picked on the stage select screen is always a soccer match.
    if (picked_arena != 0 && start->rules.stkind == St_Kind_Last &&
        start->rules.on_match_start == NULL && start->rules.on_match_end == NULL)
    {
        soccer_LoadConfig();
        gmArena_Select(picked_arena);
        start->rules.on_match_start = soccer_OnMatchStart;
        start->rules.on_match_end = soccer_OnMatchEnd;
        return;
    }
    if (!pc_is_soccer_enabled()) {
        return;
    }
    if (start->rules.stkind != St_Kind_Last) {
        OSReport("[soccer] enabled, but the stage is not Final Destination\n");
        return;
    }
    // The arena must be known before the stage loads its collision.
    soccer_LoadConfig();
    gmArena_Select((int) PARAM(P_ARENA));
    if (start->rules.on_match_start != NULL ||
        start->rules.on_match_end != NULL)
    {
        return;
    }
    start->rules.on_match_start = soccer_OnMatchStart;
    start->rules.on_match_end = soccer_OnMatchEnd;
}

bool pc_soccer_is_active(void)
{
    return soccer.active;
}

const char* pc_soccer_banner(int* side)
{
    *side = soccer.banner_side;
    return soccer.active && soccer.banner[0] != '\0' ? soccer.banner : NULL;
}

void pc_soccer_get_score(int* left, int* right)
{
    *left = soccer.score[SIDE_LEFT];
    *right = soccer.score[SIDE_RIGHT];
}
