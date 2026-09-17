#include "gmsoccer.h"

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
#include <melee/lb/lbcollision.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>

#include "pc/pc.h"

#include <stdarg.h>
#include <stdio.h>
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
    P_ROLL_RADIUS,
    P_AIR_SPIN_KEEP,
    P_SPAWN_Y,
    P_RESPAWN_DELAY,
    P_END_DELAY,
    P_GOAL_LINE_X,
    P_GOAL_BACK_X,
    P_GOAL_BOTTOM_Y,
    P_GOAL_TOP_Y,
    P_SHOW_GOALS,
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
    { "roll_radius", 4.0F, "rolling radius for spin (smaller = spins faster, 0 = no spin)" },
    { "air_spin_keep", 0.99F, "spin rate kept per frame in the air" },
    { "spawn_y", 40.0F, "kickoff drop height" },
    { "respawn_delay", 90, "frames between a goal and the next kickoff" },
    { "end_delay", 150, "frames between the winning goal and the end of the match" },
    { "goal_line_x", 86.0F, "goal mouth distance from center (FD ledge is 85.57)" },
    { "goal_back_x", 125.0F, "back of the net distance from center" },
    { "goal_bottom_y", -40.0F, "bottom of the goal" },
    { "goal_top_y", 35.0F, "crossbar height" },
    { "show_goals", 1, "draw the goal frames and nets (0/1)" },
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
    u32 frame;
    u32 draw_passes_seen;
    FILE* log;
} soccer;

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
}

static bool ball_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    f32 radius = PARAM(P_ROLL_RADIUS) * PARAM(P_BALL_SCALE);

    ball_KeepHarmless(gobj);
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
    it_80274484(gobj, GET_JOBJ(gobj), PARAM(P_BALL_SCALE));
    ball_EnterAir(gobj);
    soccer_Log("kickoff, score %d-%d (base gravity %.3f, max fall %.2f)",
               soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT],
               ip->xCC_item_attr->x10_fall_speed,
               ip->xCC_item_attr->x14_fall_speed_max);
    return gobj;
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

    if (!soccer.active || PARAM(P_SHOW_GOALS) == 0.0F) {
        return;
    }
    if (pass >= 0 && pass < 8 && !(soccer.draw_passes_seen & (1 << pass))) {
        soccer.draw_passes_seen |= 1 << pass;
        soccer_Log("goal draw pass %d", pass);
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
    if (soccer.score[scoring_side] >= (int) PARAM(P_GOALS_TO_WIN)) {
        soccer.finished = true;
        soccer.end_timer = (int) PARAM(P_END_DELAY);
        soccer_Log("%s side wins", scoring_side == SIDE_LEFT ? "left" : "right");
    }
}

static void soccer_Think(HSD_GObj* unused)
{
    soccer.frame++;
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
    if (soccer.respawn_timer > 0) {
        soccer.respawn_timer--;
        return;
    }
    soccer.ball = ball_Spawn();
    soccer.respawn_timer = (int) PARAM(P_RESPAWN_DELAY);
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
    soccer.active = true;
    soccer.respawn_timer = 60;
    ball_states[BALL_MS_AIR].anim_id = (enum_t) PARAM(P_BALL_AIR_ANIM);
    ball_states[BALL_MS_GROUND].anim_id = (enum_t) PARAM(P_BALL_GROUND_ANIM);

    soccer_Log("match start");
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
    if (soccer.log != NULL) {
        fclose(soccer.log);
        soccer.log = NULL;
    }
}

void gmSoccer_ConfigureMatch(StartMeleeData* start)
{
    soccer.active = false;
    if (!pc_is_soccer_enabled()) {
        return;
    }
    if (start->rules.stkind != St_Kind_Last) {
        OSReport("[soccer] enabled, but the stage is not Final Destination\n");
        return;
    }
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

void pc_soccer_get_score(int* left, int* right)
{
    *left = soccer.score[SIDE_LEFT];
    *right = soccer.score[SIDE_RIGHT];
}
