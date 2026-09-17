#include "gmsoccer.h"

#include <placeholder.h>

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
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

#include "pc/pc.h"

#include <string.h>

/* ---- tuning ---------------------------------------------------------- */

/// Damage the ball always sits at when hit. Items take knockback like
/// fighters (itcoll.c), so this sets how far every hit sends it; resetting it
/// after each hit keeps long rallies from launching the ball ever harder.
#define BALL_PERCENT 40
/// Extra multiplier on the launch speed from the hit's knockback.
#define BALL_LAUNCH_SCALE 1.0F
/// Minimum upward speed when a grounded hit lifts the ball.
#define BALL_MIN_LIFT 0.2F
/// Fraction of vertical speed kept when bouncing off the floor.
#define BALL_BOUNCE_KEEP 0.6F
/// Fraction of horizontal speed kept on each floor bounce.
#define BALL_BOUNCE_ROLL_KEEP 0.85F
/// Landings slower than this stop bouncing and start rolling.
#define BALL_BOUNCE_MIN_SPEED 0.8F
/// Horizontal air drag per frame.
#define BALL_AIR_DRAG 0.995F
/// Ground friction per frame (units/frame^2).
#define BALL_GROUND_FRICTION 0.02F
#define BALL_LIFETIME 1000000.0F

#define BALL_SPAWN_X 0.0F
#define BALL_SPAWN_Y 40.0F
/// Frames between a goal (or a lost ball) and the next kickoff.
#define RESPAWN_DELAY 90
/// Frames between the winning goal and the end of the match.
#define END_DELAY 150

/* Final Destination: floor edges at x = +-85.5657 (y = 0). A goal is the box
 * just past each ledge; balls that sail over the crossbar or out past the
 * back of the net only reset. */
#define GOAL_LINE_X 86.0F
#define GOAL_BACK_X 125.0F
#define GOAL_BOTTOM_Y -40.0F
#define GOAL_TOP_Y 35.0F
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
} soccer;

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
    ip->xD44_lifeTimer = BALL_LIFETIME;
    ip->owner = NULL;
}

static bool ball_Anim(Item_GObj* gobj)
{
    ball_KeepHarmless(gobj);
    return false;
}

static void ball_AirPhys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ItemAttr* attrs = ip->xCC_item_attr;
    it_80272860(gobj, attrs->x10_fall_speed, attrs->x14_fall_speed_max);
    ip->x40_vel.x *= BALL_AIR_DRAG;
}

static void ball_Landed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    f32 fall = -soccer.pre_coll_vel_y;
    if (fall > BALL_BOUNCE_MIN_SPEED) {
        ip->x40_vel.y = fall * BALL_BOUNCE_KEEP;
        ip->x40_vel.x *= BALL_BOUNCE_ROLL_KEEP;
        it_802762BC(ip);
        return;
    }
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
    f32 vx = ip->x40_vel.x;
    if (vx > BALL_GROUND_FRICTION) {
        vx -= BALL_GROUND_FRICTION;
    } else if (vx < -BALL_GROUND_FRICTION) {
        vx += BALL_GROUND_FRICTION;
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
    /* BALL_MS_AIR */ { 3, ball_Anim, ball_AirPhys, ball_AirColl },
    /* BALL_MS_GROUND */ { -1, ball_Anim, ball_GroundPhys, ball_GroundColl },
};

static void ball_EnterAir(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_802762BC(ip);
    Item_80268E5C(gobj, BALL_MS_AIR, ITEM_ANIM_UPDATE);
    ball_KeepHarmless(gobj);
}

static void ball_EnterGround(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_802762B0(ip);
    Item_80268E5C(gobj, BALL_MS_GROUND, ITEM_ANIM_UPDATE);
    ball_KeepHarmless(gobj);
}

static bool ball_DmgReceived(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    // Read the hit before the state change clears xCC8_knockback.
    bool lifted = it_8027B798(gobj, &ip->x40_vel);
    ip->x40_vel.x *= BALL_LAUNCH_SCALE;
    ip->x40_vel.y *= BALL_LAUNCH_SCALE;
    ip->x40_vel.z = 0.0F;
    OSReport("[soccer] hit by P%d: kb %.1f angle %d dmg %d -> vel (%.2f, %.2f)"
             " at (%.1f, %.1f)\n",
             ip->xCB0_source_ply + 1, ip->xCC8_knockback, ip->xCAC_angle,
             ip->xCA0, ip->x40_vel.x, ip->x40_vel.y, ip->pos.x, ip->pos.y);
    ip->xC9C = BALL_PERCENT;
    if (ip->ground_or_air == GA_Air || lifted) {
        if (ip->ground_or_air == GA_Ground && ip->x40_vel.y < BALL_MIN_LIFT) {
            ip->x40_vel.y = BALL_MIN_LIFT;
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
    spawn.pos.x = BALL_SPAWN_X;
    spawn.pos.y = BALL_SPAWN_Y;
    spawn.prev_pos = spawn.pos;
    spawn.facing_dir = 1.0F;
    spawn.x44_flag.b0 = 1;
    gobj = Item_80268B18(&spawn);
    if (gobj == NULL) {
        OSReport("[soccer] ball spawn failed\n");
        return NULL;
    }
    // Mr. Saturn's own spawn callback has run; from here on this instance
    // uses the ball's callbacks and states. Other Mr. Saturns are untouched.
    ip = GET_ITEM(gobj);
    ip->xB8_itemLogicTable = &ball_logic;
    ip->xBC_itemStateContainer = ball_states;
    ip->xC9C = BALL_PERCENT;
    itResetVelocity(ip);
    ball_EnterAir(gobj);
    OSReport("[soccer] kickoff, score %d-%d\n", soccer.score[SIDE_LEFT],
             soccer.score[SIDE_RIGHT]);
    return gobj;
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
        OSReport("[soccer] P%d defends the %s goal\n", i + 1,
                 soccer.side[i] == SIDE_LEFT ? "left" : "right");
        any = true;
    }
    soccer.sides_assigned = any;
}

static void soccer_Goal(int scoring_side, Vec3* pos)
{
    soccer.score[scoring_side]++;
    OSReport("[soccer] GOAL for %s at (%.1f, %.1f): %d-%d\n",
             scoring_side == SIDE_LEFT ? "left" : "right", pos->x, pos->y,
             soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT]);
    Camera_RequestQuake(QuakeKind_Small, pos);
    if (soccer.score[scoring_side] >= GM_SOCCER_GOALS_TO_WIN) {
        soccer.finished = true;
        soccer.end_timer = END_DELAY;
        OSReport("[soccer] %s side wins\n",
                 scoring_side == SIDE_LEFT ? "left" : "right");
    }
}

static void soccer_Think(HSD_GObj* unused)
{
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
        bool in_goal = ax > GOAL_LINE_X && ax < GOAL_BACK_X &&
                       pos.y > GOAL_BOTTOM_Y && pos.y < GOAL_TOP_Y;
        bool out = ax > OUT_OF_PLAY_X || pos.y < OUT_OF_PLAY_Y;
        if (in_goal || out) {
            if (in_goal && !soccer.finished) {
                // A ball in the left goal scores for the right side.
                soccer_Goal(pos.x < 0.0F ? SIDE_RIGHT : SIDE_LEFT, &pos);
            } else if (out) {
                OSReport("[soccer] ball out of play at (%.1f, %.1f)\n", pos.x,
                         pos.y);
            }
            Item_8026A8EC(soccer.ball);
            soccer.ball = NULL;
            soccer.respawn_timer = RESPAWN_DELAY;
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
    soccer.respawn_timer = RESPAWN_DELAY;
}

static void soccer_OnMatchStart(void)
{
    memset(&soccer, 0, sizeof(soccer));
    soccer.active = true;
    soccer.respawn_timer = 60;
    OSReport("[soccer] match start, first to %d\n", GM_SOCCER_GOALS_TO_WIN);
    HSD_GObj_SetupProc(GObj_Create(0xF, 0x11, 0), soccer_Think, 0x15);
}

static void soccer_OnMatchEnd(u8 outcome)
{
    OSReport("[soccer] match end (outcome %d), final score %d-%d\n", outcome,
             soccer.score[SIDE_LEFT], soccer.score[SIDE_RIGHT]);
    soccer.active = false;
    soccer.ball = NULL;
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
    if (start->rules.on_match_start != NULL || start->rules.on_match_end != NULL) {
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
