/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * PC mod: the character select screen for 8-player VS.
 *
 * Replaces the vanilla CSS (mncharsel.c) only when the 8-player toggle is on
 * in regular VS; every other mode that uses GS_CSS keeps the vanilla screen.
 * mncharsel.c's three scene callbacks branch here (mn8Css_Claim/Running).
 *
 * It reuses the vanilla screen's assets, loaded from the disc at runtime:
 * camera, lights, fog, the backdrop, the character grid (`menu`) and the hand.
 * It does not load the four doors; eight panels are drawn from code instead,
 * labelled with the game's own text engine.
 *
 * The whole contract with the rest of the game is the CSSData the scene is
 * entered with: slots 0-3 are written straight into vs.start.players, slots
 * 4-7 into the 8-player CPU panels (gm8player.c), which gm8Player_ConfigureMatch
 * turns into players at match start, the same path the earlier CPU panels used.
 *
 * One hand per connected controller, as on the vanilla screen; the mouse
 * drives P1's. Each hand picks for its own port's panel unless it has grabbed
 * another one (a CPU, or any of P5-P8), which it holds until its next pick.
 *   A on a character          give it to the panel the hand holds; a grabbed
 *                             panel is let go afterwards
 *   A on the panel it holds   cycle HMN/CPU/Off (P5-P8: CPU/Off)
 *   A on another panel        grab it (its own port's panel: take it back)
 *   A on the top-left sign    toggle free-for-all and Teams
 *   X / Y                     next / previous costume; team in Teams mode
 *   L / R                     CPU level down / up
 *   B                         let go of a grabbed panel, else unpick;
 *                             hold B to go back to the main menu
 *   Start                     stage select
 */
#include "mn8css.h"

#include <placeholder.h>

#include <dolphin/gx.h>
#include <dolphin/os.h>

#include "inlines.h"
#include "mncharsel.h"
#include "mncharsel_data.h"
#include "mnmain.h"
#include "mnmouse.h"
#include "types.h"

#include <melee/ft/forward.h>
#include <melee/gm/gm8player.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbvector.h>
#include <melee/lb/lblanguage.h>
#include <melee/lb/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/tobj.h>

#include <pc/pc.h>
#include <pc/widescreen.h>

#include <math.h>
#include <stdio.h>

#define N_SLOTS GM_MAX_PLAYERS
#define N_PORTS 4

/* Panel row, in CSS world units. The grid sits in y -1..20 and the vanilla
 * doors used the space below it; the hand reaches about x +-35, y -22..25. */
#define PANEL_TOP (-3.8F)
/* Lowest a panel may reach; the screen's bottom edge is near y -29. */
#define PANEL_FLOOR (-26.5F)
/* The row: panels share it, centred, with a gap between; as few fighters as
 * are in get panels as wide as PANEL_MAX_W. ROW_HALF_W is at the original
 * 73:60 aspect; widescreen widens it with the frame (mn8Css_RowHalfW). */
#define ROW_CENTER (-0.2F)
#define ROW_HALF_W (31.5F)
#define ROW_GAP (0.6F)
#define PANEL_NARROW_W (7.35F) /* eight across */
#define PANEL_MAX_W (14.0F)
#define ADD_TILE_W (5.6F)
#define PANEL_BORDER (0.3F)
#define PANEL_Z (0.0F)
/* Pseudo-slot for the Add tile in hover/tile lookups. */
#define TILE_ADD N_SLOTS

/* Panel motion: each frame a panel closes this fraction of the gap to where
 * the layout wants it (an ease-out, ~95% there in 11 frames). Panels joining
 * rise from MOTION_DROP below their place; leaving ones sink that far. */
#define MOTION_EASE (0.25F)
#define MOTION_DROP (30.0F)

/* Text inside a panel. font_size is world units per text pixel; box sizes and
 * line offsets below are in text pixels. */
#define TEXT_FONT_X (0.036F)
#define TEXT_FONT_Y (0.042F)
/* Text scales up with the panel, to at most this. */
#define TEXT_MAX_SCALE (1.4F)
/* Line positions in world units at scale 1: the P-tag from the panel top,
 * the rest below the portrait. */
#define LINE_TAG (0.84F)
#define LINE_NAME (0.5F)
#define LINE_STATUS (2.3F)
#define LINE_COLOR (3.85F)
#define TEXT_BLOCK_H (5.05F) /* portrait bottom to panel bottom */

/* Portrait: starts below the P-tag, keeps the door art's 136x188 aspect, and
 * may crop its sides down to PORTRAIT_MIN_CROP of the width on narrow
 * panels rather than shrink. */
#define PORTRAIT_TOP (1.4F)
#define PORTRAIT_MARGIN_X (0.35F)
#define PORTRAIT_ASPECT (136.0F / 188.0F)
#define PORTRAIT_MIN_CROP (0.75F)
/* Portraits shorter than this uncropped get their sides cropped instead. */
#define PORTRAIT_MIN_H (10.0F)

#define HAND_MAX_X (35.0F) /* at 73:60; widened with the frame */
#define HAND_MIN_Y (-27.0F)
#define HAND_MAX_Y (25.0F)
#define HAND_SPEED (0.9F)
#define STICK_DEADZONE (0.25F)
/* Where each hand appears: over its own panel. */
#define HAND_START_Y (-12.0F)
/* Hold B this many frames to leave, as mnCharSel_CursorThink. */
#define HOLD_B_FRAMES 30
/* The top-left Melee / Teams sign; same bounds as mnCharSel_CursorThink. */
#define MODE_SIGN_MAX_X (-25.5F)
#define MODE_SIGN_MIN_Y (22.0F)
#define MODE_SIGN_JOINT 0x24
/* Hand model joints (mnCharSel_CursorThink): pose and colour, both
 * texture-animation frames. */
#define HAND_POSE_JOINT 2
#define HAND_COLOR_JOINT 3
#define HAND_POSE_POINT 0
#define HAND_POSE_OPEN 2
/* Where the pointing fingertip sits relative to the hand model's root: the
 * visible joints hang 2 units towards the camera, and the tip is right of
 * and just below the root (measured on screen in 16:9: 5.2 right, 0.9 down
 * at the menu plane, near the centre). A hand's (x, y) is the spot on the
 * menu plane the fingertip marks; mn8Css_PoseHand places the model so the
 * tip is drawn over it. */
#define HAND_TIP_X (5.3F)
#define HAND_TIP_Y (-0.9F)
#define HAND_TIP_Z (2.0F)

#define N_TEAMS 3

/* Sounds, as mncharsel.c plays them. */
#define SFX_COIN_GRAB 0xB7 /* picking a coin up */
#define SFX_COIN_DROP 0xB8 /* setting a coin down */
#define SFX_DOOR_OPEN 0xB9 /* a slot joins */
#define SFX_DOOR_SHUT 0xBA /* a slot goes off */

/* Coin model (`token`) joints, as fn_80262648: the label (P1-P4 / CPU, a
 * texture frame) and the colour (a material-animation frame). */
#define COIN_LABEL_JOINT 4
#define COIN_COLOR_JOINT 3
#define COIN_LABEL_CPU 16
#define COIN_COLOR_CPU 8

#define LEVEL_DEFAULT 5

/* Grid-model joints that belong to the vanilla doors' row: per-port KO star
 * groups (data2.ko_stars in mncharsel.c) and name tags (mnCharSel_803F0E8C).
 * With no doors under them they would float over the panels. */
static const u8 mn8css_hidden_joints[] = {
    0x57, 0x5D, 0x63, 0x69,             /* KO stars */
    0x70, 0x71, 0x72, 0x73, 0x74,       /* P1 name tag */
    0x75, 0x76, 0x77, 0x78, 0x79,       /* P2 */
    0x7A, 0x7B, 0x7C, 0x7D, 0x7E,       /* P3 */
    0x7F, 0x80, 0x81, 0x82, 0x83,       /* P4 */
    /* The four doors themselves, from mnCharSel_803F0DFC: emblem, portrait,
     * team badge, door frame, background, HMN/CPU toggle, slider name, CPU
     * slider, handicap slider. Some hang below the grid from joints above
     * it, so mn8Css_HideBelowGrid misses them. */
    0x2E, 0x33, 0x38, 0x85, 0x29, 0xA6, 0x3D, 0x41, 0x40, /* door 1 */
    0x2F, 0x34, 0x39, 0x8D, 0x2A, 0xA8, 0x43, 0x47, 0x46, /* door 2 */
    0x30, 0x35, 0x3A, 0x95, 0x2B, 0xAA, 0x49, 0x4D, 0x4C, /* door 3 */
    0x31, 0x36, 0x3B, 0x9D, 0x2C, 0xAC, 0x4F, 0x53, 0x52, /* door 4 */
};

enum { SLOT_OFF, SLOT_HMN, SLOT_CPU };

typedef struct Mn8Slot {
    u8 kind;    ///< SLOT_*
    u8 ckind;   ///< ::CharacterKind, or ChKind_None
    u8 level;   ///< CPU level 1-9
    u8 costume; ///< free-for-all costume
    u8 team;    ///< Teams mode: 0 red, 1 blue, 2 green
    u8 teams;   ///< copy of is_teams, only so the label refresh sees a toggle
} Mn8Slot;

/// The door-portrait image a panel shows, pulled out of the `menu` model's
/// portrait joint (see mn8Css_ResolvePortraits).
typedef struct Mn8Portrait {
    int frame; ///< texture-animation frame resolved, -1 for none
    HSD_ImageDesc* image;
    HSD_Tlut* tlut;
} Mn8Portrait;

typedef struct Mn8Coin {
    HSD_JObj* jobj;
    f32 x;  ///< resting place on the icon
    f32 y;
    f32 dx; ///< drawn position, sliding towards (x, y)
    f32 dy;
    u8 ckind; ///< icon it was put on, to notice a new pick
    u8 color; ///< colour key it was last drawn with, 0xFF to force
    u8 timer;
} Mn8Coin;

typedef struct Mn8Hand {
    HSD_JObj* jobj;
    f32 x;
    f32 y;
    s8 target;     ///< slot this hand picks for
    s8 hover;      ///< panel under the hand, or -1
    bool shown;    ///< controller connected (P1's hand always is)
    bool on_sign;  ///< over the Melee / Teams sign
    bool b_armed;  ///< B released since the screen opened
    u16 b_held;
} Mn8Hand;

static struct {
    bool running;
    CSSData* css;
    HSD_Archive* archive;
    HSD_Archive* archive_ext;
    MnSelectChrDataTable* data;
    HSD_GObj* camera;
    HSD_JObj* background;
    HSD_JObj* menu;
    Mn8Hand hand[N_PORTS];
    Mn8Coin coin[N_SLOTS];
    HSD_JObj* banner;
    u32 banner_timer;
    HSD_TObj* portrait_tobj; ///< the door-1 portrait's animated texture
    Mn8Portrait portrait[N_SLOTS];
    /* Row layout, rebuilt when a slot joins or leaves (mn8Css_Relayout). */
    struct {
        s8 slot; ///< slot, or TILE_ADD
        f32 x0;
        f32 w;
    } tile[N_SLOTS + 1];
    int n_tiles;
    int layout_key; ///< joined-slot mask the layout was built for, -1 none
    f32 layout_scale; ///< widescreen widening it was built for
    f32 panel_h;
    f32 text_scale;
    f32 portrait_h; ///< full (uncropped) portrait height
    HSD_Text* add_text;
    /* What is drawn, easing towards the layout (mn8Css_MotionThink). Indexed
     * by slot, TILE_ADD for the Add tile. */
    struct {
        bool live;    ///< drawn
        bool leaving; ///< sinking out; dropped once off screen
        f32 x0;
        f32 w;
        f32 dy; ///< vertical offset, 0 in place, negative below
        f32 tx0;
        f32 tw;
        /* Last drawn fill and portrait, kept for the way out. */
        GXColor color;
        HSD_ImageDesc* image;
        HSD_Tlut* tlut;
    } motion[N_SLOTS + 1];
    f32 shown_h; ///< drawn panel height, easing towards panel_h
    int text_ctx;
    HSD_Text* text[N_SLOTS];
    Mn8Slot slot[N_SLOTS];
    Mn8Slot shown[N_SLOTS]; ///< what the labels currently say
    u32 frame;
    u8 pending; ///< CSSPendingSceneChange written on exit
    bool leaving;
} mn8css;

/* ---- slot model --------------------------------------------------------- */

static bool mn8Css_IsPlayable(int ckind)
{
    return ckind >= 0 && ckind < CKind_Playable_Count && ckind != CKind_Seak;
}

static const char* mn8Css_KindLabel(const Mn8Slot* s, char* buf, int len)
{
    switch (s->kind) {
    case SLOT_HMN:
        return "HMN";
    case SLOT_CPU:
        snprintf(buf, len, "CPU %d", s->level);
        return buf;
    default:
        return "Off";
    }
}

static bool mn8Css_Teams(void)
{
    return mn8css.css->vs.start.rules.is_teams != 0;
}

/// Human is only possible on the four controller ports.
static void mn8Css_CycleKind(int k)
{
    Mn8Slot* s = &mn8css.slot[k];

    if (k < N_PORTS) {
        s->kind = s->kind == SLOT_HMN   ? SLOT_CPU
                  : s->kind == SLOT_CPU ? SLOT_OFF
                                        : SLOT_HMN;
    } else {
        s->kind = s->kind == SLOT_CPU ? SLOT_OFF : SLOT_CPU;
    }
}

/// Whether a joined slot other than @p k already wears @p costume of @p ckind
/// (free-for-all only: Teams colours come from the team). With @p before_only,
/// only slots before @p k count.
static bool mn8Css_CostumeTaken(int k, u8 ckind, u8 costume, bool before_only)
{
    int i;

    for (i = 0; i < (before_only ? k : N_SLOTS); i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        if (i != k && s->kind != SLOT_OFF && s->ckind == ckind &&
            s->costume == costume)
        {
            return true;
        }
    }
    return false;
}

/// Step slot @p k's costume by @p dir (+1 / -1), skipping costumes another
/// slot of the same character wears, as mnCharSel_CostumeChange. A @p dir of
/// 0 keeps the current one if it is free, else takes the next free one.
static void mn8Css_StepCostume(int k, int dir, bool before_only)
{
    Mn8Slot* s = &mn8css.slot[k];
    int count = gm_GetNumCostumesForCKind(s->ckind);
    int c = s->costume < count ? s->costume : 0;
    int tries;

    if (dir == 0) {
        if (!mn8Css_CostumeTaken(k, s->ckind, (u8) c, before_only)) {
            s->costume = (u8) c;
            return;
        }
        dir = 1;
    }
    for (tries = 0; tries < count; tries++) {
        c = (c + dir + count) % count;
        if (!mn8Css_CostumeTaken(k, s->ckind, (u8) c, before_only)) {
            break;
        }
    }
    s->costume = (u8) c;
}

/// Leaving Teams can leave two of a character in the same costume; move the
/// later ones along, as the vanilla sign toggle does.
static void mn8Css_FixCostumes(void)
{
    int k;

    for (k = 0; k < N_SLOTS; k++) {
        if (mn8css.slot[k].ckind != ChKind_None) {
            mn8Css_StepCostume(k, 0, true);
        }
    }
}

/// Costume slot @p s plays in: its own in free-for-all, its team's colour in
/// Teams (the three getters mnCharSel_8025E284's colour step uses).
static u8 mn8Css_MatchColor(const Mn8Slot* s)
{
    if (!mn8Css_Teams()) {
        return s->costume;
    }
    switch (s->team) {
    case 1:
        return gm_801692BC(s->ckind);
    case 2:
        return gm_80169290(s->ckind);
    default:
        return gm_80169264(s->ckind);
    }
}

static void mn8Css_LoadSlots(void)
{
    StartMeleeData* start = &mn8css.css->vs.start;
    bool any_port = false;
    int i;

    for (i = 0; i < N_PORTS; i++) {
        const PlayerInitData* p = &start->players[i];
        Mn8Slot* s = &mn8css.slot[i];

        s->kind = p->slot_type == Gm_PKind_Human ? SLOT_HMN
                  : p->slot_type == Gm_PKind_Cpu ? SLOT_CPU
                                                 : SLOT_OFF;
        s->ckind = mn8Css_IsPlayable(p->ckind) ? p->ckind : ChKind_None;
        s->level = (p->cpu_level >= 1 && p->cpu_level <= 9) ? p->cpu_level
                                                            : LEVEL_DEFAULT;
        /* In Teams the stored colour is the team's, not a pick. */
        s->costume = start->rules.is_teams ? 0 : p->color;
        s->team = p->team < N_TEAMS ? p->team : (u8) (i % 2);
        any_port |= s->kind != SLOT_OFF;
    }
    for (i = N_PORTS; i < N_SLOTS; i++) {
        Mn8Slot* s = &mn8css.slot[i];
        int k = i - N_PORTS;
        int ckind = gm8Player_PanelCkind(k);
        int color = gm8Player_PanelColor(k);
        int team = gm8Player_PanelTeam(k);

        s->kind = mn8Css_IsPlayable(ckind) ? SLOT_CPU : SLOT_OFF;
        s->ckind = mn8Css_IsPlayable(ckind) ? ckind : ChKind_None;
        s->level = gm8Player_PanelLevel(k);
        s->costume =
            (color == GM8P_COLOR_AUTO || start->rules.is_teams) ? 0 : color;
        s->team = team < N_TEAMS ? team : (u8) (i % 2);
    }
    /* First visit: nobody has joined yet, so seat player 1. */
    if (!any_port) {
        mn8css.slot[0].kind = SLOT_HMN;
    }
    mn8Css_FixCostumes();
}

/// Write the selection back where the rest of the game reads it.
static void mn8Css_CommitSlots(void)
{
    StartMeleeData* start = &mn8css.css->vs.start;
    int i;

    for (i = 0; i < N_PORTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        PlayerInitData* p = &start->players[i];

        /* Kept even for an empty slot, so the next visit gets it back. */
        p->team = s->team;
        if (s->kind == SLOT_OFF || s->ckind == ChKind_None) {
            p->slot_type = Gm_PKind_NA;
            continue;
        }
        p->slot_type = s->kind == SLOT_HMN ? Gm_PKind_Human : Gm_PKind_Cpu;
        p->ckind = (s8) s->ckind;
        p->color = mn8Css_MatchColor(s);
        p->cpu_level = s->level;
        /* The regular VS AI; CpuKind_0 is Training Mode's standing dummy. */
        p->cpu_kind = CpuKind_4;
    }
    /* Slots 4-7 go through the CPU panels, which gm8Player_ConfigureMatch
     * turns into players. */
    for (i = N_PORTS; i < N_SLOTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        int k = i - N_PORTS;
        bool on = s->kind != SLOT_OFF && s->ckind != ChKind_None;

        gm8Player_PanelSetCkind(k, on ? s->ckind : ChKind_None);
        gm8Player_PanelSetLevel(k, s->level);
        gm8Player_PanelSetColor(k, on ? mn8Css_MatchColor(s)
                                      : GM8P_COLOR_AUTO);
        gm8Player_PanelSetTeam(k, s->team);
    }
}

/// Start needs two fighters, every joined slot to have picked, and someone
/// on a controller port (gm8Player_ConfigureMatch copies the match rules
/// from the first of them).
static bool mn8Css_CanStart(void)
{
    int fighters = 0;
    bool port_fighter = false;
    u32 teams_in = 0;
    int i;

    for (i = 0; i < N_SLOTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        if (s->kind == SLOT_OFF) {
            continue;
        }
        if (s->ckind == ChKind_None) {
            return false;
        }
        fighters++;
        port_fighter |= i < N_PORTS;
        teams_in |= 1 << s->team;
    }
    /* Teams also needs two teams, or the match is over before it starts. */
    if (mn8Css_Teams() && (teams_in & (teams_in - 1)) == 0) {
        return false;
    }
    return fighters >= 2 && port_fighter;
}

/* ---- layout ------------------------------------------------------------- */

/// Half the row's width: wider with the frame in widescreen.
static f32 mn8Css_RowHalfW(void)
{
    return ROW_HALF_W * pc_widescreen_frame_scale();
}

/// Tile showing slot @p k (or TILE_ADD), or -1 when it has none.
static int mn8Css_TileOf(int k)
{
    int t;

    for (t = 0; t < mn8css.n_tiles; t++) {
        if (mn8css.tile[t].slot == k) {
            return t;
        }
    }
    return -1;
}

/// Slot (or TILE_ADD) under world (@p x, @p y), or -1.
static int mn8Css_PanelAt(f32 x, f32 y)
{
    int t;

    /* Where panels are drawn, not where they are heading. */
    for (t = 0; t <= N_SLOTS; t++) {
        f32 top = PANEL_TOP + mn8css.motion[t].dy;
        if (!mn8css.motion[t].live || mn8css.motion[t].leaving) {
            continue;
        }
        if (y <= top && y >= top - mn8css.shown_h &&
            x >= mn8css.motion[t].x0 &&
            x <= mn8css.motion[t].x0 + mn8css.motion[t].w)
        {
            return t;
        }
    }
    return -1;
}

/// Centre x of slot @p k's tile, or of where its port's panel would sit.
static f32 mn8Css_SlotCenterX(int k)
{
    int t = mn8Css_TileOf(k);

    if (t >= 0) {
        return mn8css.tile[t].x0 + mn8css.tile[t].w * 0.5F;
    }
    return ROW_CENTER - mn8Css_RowHalfW() +
           (PANEL_NARROW_W + ROW_GAP) * (f32) k + PANEL_NARROW_W * 0.5F;
}

/* ---- drawing ------------------------------------------------------------ */

static const GXColor mn8css_port_color[N_PORTS] = {
    { 0xC0, 0x2E, 0x2E, 0xFF }, /* P1 red */
    { 0x2E, 0x4C, 0xC0, 0xFF }, /* P2 blue */
    { 0xC0, 0xA0, 0x22, 0xFF }, /* P3 yellow */
    { 0x2E, 0x96, 0x40, 0xFF }, /* P4 green */
};

static GXColor mn8Css_PanelColor(int k)
{
    static const GXColor team_color[N_TEAMS] = {
        { 0xC0, 0x2E, 0x2E, 0xFF }, /* red */
        { 0x2E, 0x4C, 0xC0, 0xFF }, /* blue */
        { 0x2E, 0x96, 0x40, 0xFF }, /* green */
    };
    const Mn8Slot* s = &mn8css.slot[k];

    if (s->kind != SLOT_OFF && mn8Css_Teams()) {
        GXColor c = team_color[s->team];
        if (s->kind == SLOT_CPU) { /* CPUs a shade darker */
            c.r = (u8) (c.r * 3 / 4);
            c.g = (u8) (c.g * 3 / 4);
            c.b = (u8) (c.b * 3 / 4);
        }
        return c;
    }
    if (s->kind == SLOT_HMN) {
        return mn8css_port_color[k < N_PORTS ? k : 0];
    }
    if (s->kind == SLOT_CPU) {
        return (GXColor){ 0x5E, 0x60, 0x68, 0xFF };
    }
    return (GXColor){ 0x1C, 0x1E, 0x24, 0xFF };
}

/// Flat vertex-coloured quads (same state as the soccer arena icons): the
/// debug quad helper lights its colour, which looks wrong under menu lights.
static void mn8Css_BeginQuads(void)
{
    Mtx view;

    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_DISABLE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    /* Paint over the vanilla player row whatever depth it sits at, and write
     * no depth, so the labels drawn next are unaffected. Order does the
     * layering instead: see mn8Css_Draw. */
    GXSetZMode(GX_DISABLE, GX_ALWAYS, GX_DISABLE);
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
}

static void mn8Css_Rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 z, GXColor c)
{
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x0, y0, z);
    GXColor4u8(c.r, c.g, c.b, 0xFF);
    GXPosition3f32(x1, y0, z);
    GXColor4u8(c.r, c.g, c.b, 0xFF);
    GXPosition3f32(x1, y1, z);
    GXColor4u8(c.r, c.g, c.b, 0xFF);
    GXPosition3f32(x0, y1, z);
    GXColor4u8(c.r, c.g, c.b, 0xFF);
    GXEnd();
}

/// First texture with an animation in @p jobj's own subtree.
static HSD_TObj* mn8Css_FindAnimTObj(HSD_JObj* jobj)
{
    HSD_JObj* child;

    if (jobj == NULL) {
        return NULL;
    }
    if (union_type_dobj(jobj)) {
        HSD_DObj* dobj;
        for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* tobj;
            if (dobj->mobj == NULL) {
                continue;
            }
            for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
                if (tobj->aobj != NULL && tobj->imagetbl != NULL) {
                    return tobj;
                }
            }
        }
    }
    if (jobj->flags & JOBJ_INSTANCE) {
        return NULL;
    }
    for (child = jobj->child; child != NULL; child = child->next) {
        HSD_TObj* tobj = mn8Css_FindAnimTObj(child);
        if (tobj != NULL) {
            return tobj;
        }
    }
    return NULL;
}

/// Point each panel at its portrait image. The door-1 portrait joint is
/// driven to each slot's frame in turn, as mnCharSel_8025D5AC drives it for
/// its door, and the image and palette it lands on are kept; the joint
/// itself stays hidden.
static void mn8Css_ResolvePortraits(void)
{
    HSD_TObj* tobj = mn8css.portrait_tobj;
    HSD_JObj* jobj;
    int k;

    if (tobj == NULL) {
        return;
    }
    lb_80011E24(mn8css.menu, &jobj, mnCharSel_PcPortraitJoint(), -1);
    for (k = 0; k < N_SLOTS; k++) {
        const Mn8Slot* s = &mn8css.slot[k];
        Mn8Portrait* pt = &mn8css.portrait[k];
        int frame = -1;

        if (s->kind != SLOT_OFF && s->ckind != ChKind_None) {
            frame = mnCharSel_PcPortraitFrame(s->ckind, mn8Css_MatchColor(s));
        }
        if (frame == pt->frame) {
            continue;
        }
        pt->frame = frame;
        pt->image = NULL;
        pt->tlut = NULL;
        if (frame < 0) {
            continue;
        }
        HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim,
                        AOBJ_ARG_AF, (f32) frame);
        HSD_JObjAnimAll(jobj);
        HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                        AOBJ_ARG_AOV, NULL);
        pt->image = tobj->imagedesc;
        pt->tlut = tobj->tlut_no != (u8) -1 && tobj->tluttbl != NULL
                       ? tobj->tluttbl[tobj->tlut_no]
                       : tobj->tlut;
        if (pt->image != NULL) {
            pc_log_line("[8css] P%d portrait frame %d: %dx%d fmt %d tlut %p",
                        k + 1, frame, pt->image->width, pt->image->height,
                        (int) pt->image->format, (void*) pt->tlut);
        }
    }
}

/// Alpha-blended textured quads for the portraits.
static void mn8Css_BeginTexQuads(void)
{
    Mtx view;

    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_DISABLE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                   GX_LO_NOOP);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_DISABLE, GX_ALWAYS, GX_DISABLE);
    GXSetNumChans(0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetCullMode(GX_CULL_NONE);
    HSD_ClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetCurrentMtx(0);
    HSD_CObjGetViewingMtx(HSD_CObjGetCurrent(), view);
    GXLoadPosMtxImm(view, 0);
}

static void mn8Css_DrawPortrait(int k)
{
    const Mn8Portrait* pt = &mn8css.portrait[k];
    HSD_ImageDesc* img;
    HSD_Tlut* tlut_desc;
    GXTexObj tex;
    f32 box_w;
    f32 h = mn8css.portrait_h;
    f32 w;
    f32 u0 = 0.0F;
    f32 u1 = 1.0F;
    f32 x0;
    f32 y0;

    if (!mn8css.motion[k].live) {
        return;
    }
    if (mn8css.motion[k].leaving) {
        img = mn8css.motion[k].image;
        tlut_desc = mn8css.motion[k].tlut;
    } else {
        img = pt->image;
        tlut_desc = pt->tlut;
        mn8css.motion[k].image = img;
        mn8css.motion[k].tlut = tlut_desc;
    }
    if (img == NULL || img->width == 0 || img->height == 0) {
        return;
    }
    switch (img->format) {
    case GX_TF_C4:
    case GX_TF_C8:
    case GX_TF_C14X2: {
        GXTlutObj tlut;
        if (tlut_desc == NULL) {
            return;
        }
        GXInitTlutObj(&tlut, tlut_desc->lut, tlut_desc->fmt,
                      tlut_desc->n_entries);
        GXLoadTlut(&tlut, GX_TLUT0);
        GXInitTexObjCI(&tex, DP(void, img->image_ptr), img->width,
                       img->height, img->format, GX_CLAMP, GX_CLAMP,
                       GX_FALSE, GX_TLUT0);
    } break;
    default:
        GXInitTexObj(&tex, DP(void, img->image_ptr), img->width, img->height,
                     img->format, GX_CLAMP, GX_CLAMP, GX_FALSE);
        break;
    }
    GXInitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0.0F, 0.0F, 0.0F, GX_FALSE,
                    GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);

    /* Full height; crop the sides evenly if the panel is narrower. */
    box_w = mn8css.motion[k].w - 2.0F * PORTRAIT_MARGIN_X;
    w = h * (f32) img->width / (f32) img->height;
    if (w > box_w) {
        f32 keep = box_w / w;
        u0 = 0.5F - keep * 0.5F;
        u1 = 0.5F + keep * 0.5F;
        w = box_w;
    }
    x0 = mn8css.motion[k].x0 + (mn8css.motion[k].w - w) * 0.5F;
    y0 = PANEL_TOP + mn8css.motion[k].dy - PORTRAIT_TOP * mn8css.text_scale;

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x0, y0, PANEL_Z);
    GXTexCoord2f32(u0, 0.0F);
    GXPosition3f32(x0 + w, y0, PANEL_Z);
    GXTexCoord2f32(u1, 0.0F);
    GXPosition3f32(x0 + w, y0 - h, PANEL_Z);
    GXTexCoord2f32(u1, 1.0F);
    GXPosition3f32(x0, y0 - h, PANEL_Z);
    GXTexCoord2f32(u0, 1.0F);
    GXEnd();
}

static void mn8Css_Draw(HSD_GObj* gobj, int pass)
{
    static const GXColor frame_plain = { 0x0C, 0x0C, 0x10, 0xFF };
    static const GXColor frame_hover = { 0xE0, 0xE2, 0xEA, 0xFF };
    static const GXColor add_fill = { 0x26, 0x2A, 0x36, 0xFF };
    static const GXColor plus_color = { 0xE0, 0xE2, 0xEA, 0xFF };
    int k;
    int p;

    /* Pass 2 only. The labels draw in pass 2 as well (HSD_SisLib_803A84BC
     * skips every other pass) and their gobjs come after this one on the same
     * link, so they land on top; everything the backdrop and grid drew in
     * passes 0 and 2 before this is covered. */
    if (pass != 2) {
        return;
    }
    mn8Css_BeginQuads();
    for (k = 0; k <= N_SLOTS; k++) {
        f32 x0 = mn8css.motion[k].x0;
        f32 x1 = x0 + mn8css.motion[k].w;
        f32 y0 = PANEL_TOP + mn8css.motion[k].dy;
        f32 y1 = y0 - mn8css.shown_h;
        GXColor frame = frame_plain;
        GXColor fill;
        f32 b = PANEL_BORDER;

        if (!mn8css.motion[k].live) {
            continue;
        }
        if (mn8css.motion[k].leaving) {
            fill = mn8css.motion[k].color;
        } else {
            fill = k == TILE_ADD ? add_fill : mn8Css_PanelColor(k);
            mn8css.motion[k].color = fill;
        }
        /* Held by a hand: that hand's colour, thick. Else white on hover. */
        for (p = 0; p < N_PORTS; p++) {
            const Mn8Hand* h = &mn8css.hand[p];
            if (h->shown && h->hover == k) {
                frame = frame_hover;
            }
        }
        for (p = 0; p < N_PORTS; p++) {
            const Mn8Hand* h = &mn8css.hand[p];
            if (h->shown && h->target == k) {
                frame = mn8css_port_color[p];
                b = PANEL_BORDER * 1.8F;
                break;
            }
        }

        if (mn8css.motion[k].leaving) {
            frame = frame_plain;
            b = PANEL_BORDER;
        }
        mn8Css_Rect(x0, y1, x1, y0, PANEL_Z - 0.1F, frame);
        mn8Css_Rect(x0 + b, y1 + b, x1 - b, y0 - b, PANEL_Z, fill);
        if (k == TILE_ADD) {
            /* A plus sign above the "Add" label. */
            f32 cx = (x0 + x1) * 0.5F;
            f32 cy = y0 - mn8css.shown_h * 0.5F + 1.2F;
            f32 arm = 1.3F;
            f32 bar = 0.25F;

            mn8Css_Rect(cx - arm, cy - bar, cx + arm, cy + bar, PANEL_Z,
                        plus_color);
            mn8Css_Rect(cx - bar, cy - arm, cx + bar, cy + arm, PANEL_Z,
                        plus_color);
        }
    }
    mn8Css_BeginTexQuads();
    for (k = 0; k < N_SLOTS; k++) {
        mn8Css_DrawPortrait(k);
    }
}

/* ---- text --------------------------------------------------------------- */

/// A label box over tile @p t, font scaled with the panel.
static HSD_Text* mn8Css_NewTileText(int t)
{
    f32 fx = TEXT_FONT_X * mn8css.text_scale;
    f32 fy = TEXT_FONT_Y * mn8css.text_scale;
    HSD_Text* text = HSD_SisLib_803A6754(0, mn8css.text_ctx);

    text->x4C = 1;
    text->default_fitting = 1;
    text->default_alignment = 1;
    text->default_kerning = 1;
    text->font_size.x = fx;
    text->font_size.y = fy;
    /* The text canvas runs y downward: world y maps to -pos_y, the same flip
     * the vanilla name plates apply. */
    text->pos_x = mn8css.tile[t].x0;
    text->pos_y = -PANEL_TOP;
    text->pos_z = 0.0F;
    text->box_size_x = mn8css.tile[t].w / fx;
    text->box_size_y = mn8css.panel_h / fy;
    return text;
}

/// Text-canvas y (pixels) of a line @p world_y below the panel top.
static f32 mn8Css_LineY(f32 world_y)
{
    return world_y / (TEXT_FONT_Y * mn8css.text_scale);
}

static void mn8Css_CreateText(int t)
{
    int k = mn8css.tile[t].slot;
    HSD_Text* text = mn8Css_NewTileText(t);
    f32 cx = text->box_size_x * 0.5F;
    f32 s = mn8css.text_scale;
    f32 below = PORTRAIT_TOP * s + mn8css.portrait_h;
    char tag[8];

    snprintf(tag, sizeof(tag), "P%d", k + 1);
    HSD_SisLib_803A6B98(text, cx, mn8Css_LineY(LINE_TAG * s), "%s", tag);
    HSD_SisLib_803A6B98(text, cx, mn8Css_LineY(below + LINE_NAME * s), "%s",
                        "-");
    HSD_SisLib_803A6B98(text, cx, mn8Css_LineY(below + LINE_STATUS * s),
                        "%s", "-");
    HSD_SisLib_803A6B98(text, cx, mn8Css_LineY(below + LINE_COLOR * s), "%s",
                        " ");
    mn8css.text[k] = text;
    /* force the first refresh to rewrite every line */
    mn8css.shown[k].kind = 0xFF;
}

static void mn8Css_CreateAddText(int t)
{
    HSD_Text* text = mn8Css_NewTileText(t);
    f32 cx = text->box_size_x * 0.5F;
    f32 mid = mn8css.panel_h * 0.5F;

    /* The "+" above it is drawn as quads: the menu font has no '+'. */
    HSD_SisLib_803A6B98(text, cx, mn8Css_LineY(mid + 1.2F), "%s", "Add");
    mn8css.add_text = text;
}

/// Lay the row out again if who is in has changed: one panel per joined
/// slot in slot order, then an Add tile while there is room for more. Panels
/// widen (to PANEL_MAX_W) as there are fewer of them; height, portrait and
/// text follow from the width.
static void mn8Css_Relayout(void)
{
    int key = 0;
    int n_panels = 0;
    bool add;
    f32 avail;
    f32 w;
    f32 total;
    f32 x;
    f32 pw;
    int k;
    int t;

    for (k = 0; k < N_SLOTS; k++) {
        if (mn8css.slot[k].kind != SLOT_OFF) {
            key |= 1 << k;
            n_panels++;
        }
    }
    if (key == mn8css.layout_key &&
        mn8css.layout_scale == pc_widescreen_frame_scale())
    {
        return;
    }
    mn8css.layout_key = key;
    mn8css.layout_scale = pc_widescreen_frame_scale();
    add = n_panels < N_SLOTS;

    /* Widths. */
    avail = 2.0F * mn8Css_RowHalfW() - (add ? ADD_TILE_W + ROW_GAP : 0.0F);
    w = n_panels > 0
            ? (avail - ROW_GAP * (f32) (n_panels - 1)) / (f32) n_panels
            : PANEL_MAX_W;
    if (w > PANEL_MAX_W) {
        w = PANEL_MAX_W;
    }
    mn8css.text_scale = w / PANEL_NARROW_W;
    if (mn8css.text_scale < 1.0F) mn8css.text_scale = 1.0F;
    if (mn8css.text_scale > TEXT_MAX_SCALE) mn8css.text_scale = TEXT_MAX_SCALE;

    /* Portrait as tall as the width allows uncropped; if that is too short,
     * crop the sides (to at most PORTRAIT_MIN_CROP) to reach PORTRAIT_MIN_H.
     * Then no taller than the floor allows; the panel wraps it. */
    pw = w - 2.0F * PORTRAIT_MARGIN_X;
    mn8css.portrait_h = pw / PORTRAIT_ASPECT;
    if (mn8css.portrait_h < PORTRAIT_MIN_H) {
        mn8css.portrait_h = pw / (PORTRAIT_ASPECT * PORTRAIT_MIN_CROP);
        if (mn8css.portrait_h > PORTRAIT_MIN_H) {
            mn8css.portrait_h = PORTRAIT_MIN_H;
        }
    }
    {
        f32 room = (PANEL_TOP - PANEL_FLOOR) -
                   (PORTRAIT_TOP + TEXT_BLOCK_H) * mn8css.text_scale;
        if (mn8css.portrait_h > room) {
            mn8css.portrait_h = room;
        }
    }
    mn8css.panel_h = (PORTRAIT_TOP + TEXT_BLOCK_H) * mn8css.text_scale +
                     mn8css.portrait_h;

    /* Positions, centred on the row. */
    total = w * (f32) n_panels + ROW_GAP * (f32) (n_panels - 1);
    if (add) {
        total += (n_panels > 0 ? ROW_GAP : 0.0F) + ADD_TILE_W;
    }
    x = ROW_CENTER - total * 0.5F;
    mn8css.n_tiles = 0;
    for (k = 0; k < N_SLOTS; k++) {
        if (!(key & (1 << k))) {
            continue;
        }
        t = mn8css.n_tiles++;
        mn8css.tile[t].slot = (s8) k;
        mn8css.tile[t].x0 = x;
        mn8css.tile[t].w = w;
        x += w + ROW_GAP;
    }
    if (add) {
        t = mn8css.n_tiles++;
        mn8css.tile[t].slot = TILE_ADD;
        mn8css.tile[t].x0 = x;
        mn8css.tile[t].w = ADD_TILE_W;
    }

    /* Motion targets. Newcomers rise from below; panels with no tile any more
     * sink out, drawn from what they last showed. */
    for (k = 0; k <= N_SLOTS; k++) {
        t = mn8Css_TileOf(k);
        if (t >= 0) {
            mn8css.motion[k].tx0 = mn8css.tile[t].x0;
            mn8css.motion[k].tw = mn8css.tile[t].w;
            if (!mn8css.motion[k].live) {
                mn8css.motion[k].x0 = mn8css.tile[t].x0;
                mn8css.motion[k].w = mn8css.tile[t].w;
                mn8css.motion[k].dy = -MOTION_DROP;
                mn8css.motion[k].image = NULL;
            }
            mn8css.motion[k].live = true;
            mn8css.motion[k].leaving = false;
        } else if (mn8css.motion[k].live) {
            mn8css.motion[k].leaving = true;
        }
    }

    /* Labels: rebuilt for the new boxes. */
    for (k = 0; k < N_SLOTS; k++) {
        if (mn8css.text[k] != NULL) {
            HSD_SisLib_803A5CC4(mn8css.text[k]);
            mn8css.text[k] = NULL;
        }
    }
    if (mn8css.add_text != NULL) {
        HSD_SisLib_803A5CC4(mn8css.add_text);
        mn8css.add_text = NULL;
    }
    for (t = 0; t < mn8css.n_tiles; t++) {
        if (mn8css.tile[t].slot == TILE_ADD) {
            mn8Css_CreateAddText(t);
        } else {
            mn8Css_CreateText(t);
        }
    }
    pc_log_line("[8css] layout: %d panel(s)%s, %.2f wide, %.2f tall, "
                "frame x%.3f",
                n_panels, add ? " + Add" : "", w, mn8css.panel_h,
                mn8css.layout_scale);
}

/// Panel label for the slot's character. '/' and '&' are control characters
/// in the game's text encoding and swallow what follows, so the one name that
/// contains either is spelled out.
static const char* mn8Css_PanelName(const Mn8Slot* s)
{
    if (s->ckind == ChKind_None) {
        return s->kind == SLOT_OFF ? "" : "Pick";
    }
    if (s->ckind == CKind_GameWatch) {
        return "Game and Watch";
    }
    return gm8Player_CharName(s->ckind);
}

/// Fourth line: the costume, or the team in Teams mode. Costumes are numbered
/// until the panels get the real portraits.
static const char* mn8Css_ColorLabel(const Mn8Slot* s, char* buf, int len)
{
    static const char* const team_name[N_TEAMS] = { "Red", "Blue", "Green" };

    if (s->kind == SLOT_OFF) {
        return " ";
    }
    if (mn8Css_Teams()) {
        return team_name[s->team];
    }
    if (s->ckind == ChKind_None) {
        return " ";
    }
    snprintf(buf, len, "Color %d", s->costume + 1);
    return buf;
}

static void mn8Css_RefreshText(void)
{
    char buf[24];
    int k;

    for (k = 0; k < N_SLOTS; k++) {
        const Mn8Slot* s = &mn8css.slot[k];
        Mn8Slot* shown = &mn8css.shown[k];

        mn8css.slot[k].teams = mn8Css_Teams();
        if (mn8css.text[k] == NULL) {
            continue;
        }
        if (shown->kind == s->kind && shown->ckind == s->ckind &&
            shown->level == s->level && shown->costume == s->costume &&
            shown->team == s->team && shown->teams == s->teams)
        {
            continue;
        }
        HSD_SisLib_803A70A0(mn8css.text[k], 1, "%s", mn8Css_PanelName(s));
        HSD_SisLib_803A70A0(mn8css.text[k], 2, "%s",
                            mn8Css_KindLabel(s, buf, sizeof(buf)));
        HSD_SisLib_803A70A0(mn8css.text[k], 3, "%s",
                            mn8Css_ColorLabel(s, buf, sizeof(buf)));
        *shown = *s;
    }
}

/* ---- input -------------------------------------------------------------- */

static void mn8Css_MoveHand(int port)
{
    Mn8Hand* h = &mn8css.hand[port];
    f32 sx = HSD_PadCopyStatus[port].nml_stickX;
    f32 sy = HSD_PadCopyStatus[port].nml_stickY;

    if (sx > STICK_DEADZONE || sx < -STICK_DEADZONE) {
        h->x += sx * HAND_SPEED;
    }
    if (sy > STICK_DEADZONE || sy < -STICK_DEADZONE) {
        h->y += sy * HAND_SPEED;
    }

    /* The mouse drives P1's hand while it moves, as on the vanilla screen. */
    if (port == 0) {
        Vec3 cur = { h->x, h->y, 0.0F };
        Vec3 target;
        if (mnMouse_GetPlanePoint(GET_COBJ(mn8css.camera), &cur, &target)) {
            h->x = target.x;
            h->y = target.y;
        }
    }

    {
        f32 max_x = HAND_MAX_X * pc_widescreen_frame_scale();
        if (h->x < -max_x) h->x = -max_x;
        if (h->x > max_x) h->x = max_x;
    }
    if (h->y < HAND_MIN_Y) h->y = HAND_MIN_Y;
    if (h->y > HAND_MAX_Y) h->y = HAND_MAX_Y;
}

/// Hand pose, colour and position, as updateCursorDisplay in mncharsel.c.
static void mn8Css_PoseHand(int port)
{
    static const u8 team_hand_color[N_TEAMS] = { 0, 1, 3 };
    Mn8Hand* h = &mn8css.hand[port];
    HSD_JObj* jobj;
    int color;

    if (!h->shown) {
        HSD_JObjSetFlagsAll(h->jobj, JOBJ_HIDDEN);
        return;
    }
    HSD_JObjClearFlagsAll(h->jobj, JOBJ_HIDDEN);

    /* Open over the grid, pointing everywhere else. */
    lb_80011E24(h->jobj, &jobj, HAND_POSE_JOINT, -1);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF,
                    (f32) (h->y < 0.2F || h->y > MODE_SIGN_MIN_Y
                               ? HAND_POSE_POINT
                               : HAND_POSE_OPEN));
    HSD_JObjAnimAll(jobj);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);

    /* Four colours per port: the port's own, or its team's in Teams mode;
     * flashing through all four over the sign. */
    color = mn8Css_Teams() ? team_hand_color[mn8css.slot[port].team] : port;
    if (h->on_sign) {
        color = (int) (mn8css.frame & 3);
    }
    lb_80011E24(h->jobj, &jobj, HAND_COLOR_JOINT, -1);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF,
                    (f32) (color + port * 4));
    HSD_JObjAnimAll(jobj);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);

    /* Put the fingertip over (x, y). The tip is nearer the camera than the
     * menu plane, so find the point at its depth on the same line of sight
     * (screen position of (x, y, 0), back onto the plane z = HAND_TIP_Z) and
     * hang the model from there. The root stays pinned at z 0, as
     * mnCharSel_CursorThink pins it; re-run its animation to refresh the
     * matrices. */
    {
        HSD_CObj* cobj = GET_COBJ(mn8css.camera);
        static const Vec3 ux = { 1.0F, 0.0F, 0.0F };
        static const Vec3 uy = { 0.0F, 1.0F, 0.0F };
        Vec3 spot = { h->x, h->y, 0.0F };
        Vec3 ref = { h->x, h->y, HAND_TIP_Z };
        Vec3 tip = ref;
        Vec3 scr;
        f32 s = pc_widescreen_frame_scale();
        f32 cx = 0.5F * (cobj->viewport.xmin + cobj->viewport.xmax);

        lbVector_WorldToScreen(cobj, &spot, &scr, 0);
        /* mnMouse_ScreenToPlane takes on-screen (widened) coordinates. */
        if (!mnMouse_ScreenToPlane(cobj, cx + (scr.x - cx) / s, scr.y, &ref,
                                   &ux, &uy, &tip))
        {
            tip = ref;
        }
        HSD_JObjSetTranslateX(h->jobj, tip.x - HAND_TIP_X);
        HSD_JObjSetTranslateY(h->jobj, tip.y - HAND_TIP_Y);
        HSD_JObjSetTranslateZ(h->jobj, 0.0F);
    }
    HSD_JObjAnimAll(h->jobj);
}

/// The hand that has grabbed slot @p k away from its home panel, or -1.
static int mn8Css_HolderOf(int k)
{
    int p;

    for (p = 0; p < N_PORTS; p++) {
        if (p != k && mn8css.hand[p].shown && mn8css.hand[p].target == k) {
            return p;
        }
    }
    return -1;
}

/// Hand @p port lets go of a grabbed panel and goes back to its own.
static void mn8Css_Release(int port)
{
    mn8css.hand[port].target = (s8) port;
}

/// A on panel @p k.
static void mn8Css_ClickPanel(int port, int k)
{
    Mn8Hand* h = &mn8css.hand[port];
    int holder;

    if (k == h->target) {
        mn8Css_CycleKind(k);
        if (mn8css.slot[k].kind == SLOT_OFF) {
            if (k != port) {
                mn8Css_Release(port);
            }
            lbAudioAx_800237A8(SFX_DOOR_SHUT, 0x7F, 0x40);
        } else if (mn8css.slot[k].kind == SLOT_HMN || k >= N_PORTS) {
            /* just came on (HMN from Off; P5-P8 CPU from Off) */
            lbAudioAx_800237A8(SFX_DOOR_OPEN, 0x7F, 0x40);
        } else {
            sfxMove();
        }
        return;
    }
    if (k == port) {
        /* Your own panel: always yours to take back. */
        holder = mn8Css_HolderOf(k);
        if (holder >= 0) {
            mn8Css_Release(holder);
        }
        h->target = (s8) port;
        sfxMove();
        return;
    }
    /* Someone else's: free unless another hand holds it, and a port's panel
     * only while no human sits there. */
    holder = mn8Css_HolderOf(k);
    if (holder >= 0 ||
        (k < N_PORTS && mn8css.hand[k].shown && mn8css.slot[k].kind == SLOT_HMN))
    {
        sfxBack();
        return;
    }
    h->target = (s8) k;
    if (mn8css.slot[k].kind == SLOT_OFF) {
        mn8css.slot[k].kind = SLOT_CPU;
        lbAudioAx_800237A8(SFX_DOOR_OPEN, 0x7F, 0x40);
    } else {
        lbAudioAx_800237A8(SFX_COIN_GRAB, 0x7F, 0x40);
    }
}

/// A on the Add tile: join, if this hand's own port is not in yet; else a
/// CPU in the lowest free slot, skipping ports that have a controller in
/// (their player's seat), which the hand grabs so its next pick lands there.
static void mn8Css_ClickAdd(int port)
{
    Mn8Hand* h = &mn8css.hand[port];
    int i;

    if (mn8css.slot[port].kind == SLOT_OFF) {
        mn8css.slot[port].kind = SLOT_HMN;
        h->target = (s8) port;
        lbAudioAx_800237A8(SFX_DOOR_OPEN, 0x7F, 0x40);
        return;
    }
    for (i = 0; i < N_SLOTS; i++) {
        int k = i;
        if (mn8css.slot[k].kind != SLOT_OFF) {
            continue;
        }
        if (k < N_PORTS && mn8css.hand[k].shown) {
            continue; /* that controller's own seat */
        }
        mn8css.slot[k].kind = SLOT_CPU;
        mn8css.slot[k].ckind = ChKind_None;
        h->target = (s8) k;
        lbAudioAx_800237A8(SFX_DOOR_OPEN, 0x7F, 0x40);
        return;
    }
    lbAudioAx_80024030(3);
}

/// A on character @p ckind.
static void mn8Css_Pick(int port, int ckind)
{
    Mn8Hand* h = &mn8css.hand[port];
    int k = h->target;
    Mn8Slot* s = &mn8css.slot[k];

    if (s->kind == SLOT_OFF) {
        /* Picking for your own panel joins you; a grabbed one joins as CPU. */
        s->kind = k == port ? SLOT_HMN : SLOT_CPU;
    }
    if (s->ckind != ckind) {
        s->ckind = (u8) ckind;
        s->costume = 0;
    }
    mn8Css_StepCostume(k, 0, false);
    if (k != port) {
        mn8Css_Release(port);
    }
    mnCharSel_PcAnnounce(ckind);
    lbAudioAx_800237A8(SFX_COIN_DROP, 0x7F, 0x40);
}

static void mn8Css_ToggleTeams(void)
{
    HSD_JObj* sign;
    u8* is_teams = &mn8css.css->vs.start.rules.is_teams;

    *is_teams = (*is_teams + 1) & 1;
    if (!*is_teams) {
        mn8Css_FixCostumes();
    }
    lb_80011E24(mn8css.menu, &sign, MODE_SIGN_JOINT, -1);
    HSD_ForeachAnim(sign, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF,
                    mnCharSel_PcModeFrame(mn8css.css->match_type, *is_teams));
    HSD_JObjAnimAll(sign);
    HSD_ForeachAnim(sign, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    sfxMove();
}

static void mn8Css_Leave(u8 pending)
{
    mn8css.pending = pending;
    mn8css.leaving = true;
    gm_801A4B60();
}

/// One hand's frame. Returns true when it asked to leave.
static bool mn8Css_HandInput(int port)
{
    Mn8Hand* h = &mn8css.hand[port];
    u32 trig = HSD_PadCopyStatus[port].trigger;
    u32 held = HSD_PadCopyStatus[port].button;
    Mn8Slot* s;

    /* Every hand is on screen for P1 (mouse); the others only with a pad. */
    h->shown = port == 0 || HSD_PadCopyStatus[port].err == 0;
    if (!h->shown) {
        h->target = (s8) port;
        h->hover = -1;
        return false;
    }
    mn8Css_MoveHand(port);
    h->hover = (s8) mn8Css_PanelAt(h->x, h->y);
    h->on_sign = h->x < MODE_SIGN_MAX_X && h->y > MODE_SIGN_MIN_Y;
    s = &mn8css.slot[h->target];

    if (trig & HSD_PAD_A) {
        int ckind = mnCharSel_PcIconAt(h->x, h->y);

        pc_log_line("[8css] P%d A at (%.1f, %.1f): panel=%d icon=%d holds=P%d",
                    port + 1, h->x, h->y, h->hover, ckind, h->target + 1);
        if (h->on_sign) {
            mn8Css_ToggleTeams();
        } else if (h->hover == TILE_ADD) {
            mn8Css_ClickAdd(port);
        } else if (h->hover >= 0) {
            mn8Css_ClickPanel(port, h->hover);
        } else if (mn8Css_IsPlayable(ckind)) {
            mn8Css_Pick(port, ckind);
        }
    }
    if ((trig & (HSD_PAD_X | HSD_PAD_Y)) && s->kind != SLOT_OFF) {
        int dir = (trig & HSD_PAD_X) ? 1 : -1;

        if (mn8Css_Teams()) {
            s->team = (u8) ((s->team + dir + N_TEAMS) % N_TEAMS);
            sfxMove();
        } else if (s->ckind != ChKind_None) {
            u8 before = s->costume;
            mn8Css_StepCostume(h->target, dir, false);
            if (s->costume != before) {
                sfxMove();
            }
        }
    }
    if ((trig & (HSD_PAD_L | HSD_PAD_R)) && s->kind == SLOT_CPU) {
        int level = s->level + ((trig & HSD_PAD_R) ? 1 : -1);

        s->level = (u8) (level < 1 ? 1 : level > 9 ? 9 : level);
        sfxMove();
    }

    /* B: let go, else unpick; held, back to the menu. A B still held from the
     * previous screen does not count until it is let go. */
    if (trig & HSD_PAD_B) {
        if (h->target != port) {
            mn8Css_Release(port);
            sfxBack();
        } else if (s->ckind != ChKind_None) {
            s->ckind = ChKind_None;
            lbAudioAx_800237A8(SFX_COIN_GRAB, 0x7F, 0x40);
        }
    }
    if (!(held & HSD_PAD_B)) {
        h->b_armed = true;
        h->b_held = 0;
    } else if (h->b_armed && ++h->b_held > HOLD_B_FRAMES) {
        sfxBack();
        mn8Css_Leave(2); /* CSSPendingSceneChange_2: back to the menu */
        return true;
    }

    if (trig & HSD_PAD_START) {
        if (mn8Css_CanStart()) {
            mn8Css_CommitSlots();
            sfxForward();
            mn8Css_Leave(1); /* same value the vanilla screen uses for Start */
            return true;
        }
        lbAudioAx_80024030(3); /* the vanilla screen's refusal buzz */
    }
    return false;
}

/// Colour key for slot @p k's coin, as fn_80262648's model->x6: the port's
/// colour for a human, grey for a CPU; in Teams the team's (+4 for a CPU).
static u8 mn8Css_CoinColor(int k)
{
    static const u8 team_coin[N_TEAMS] = { 0, 1, 3 };
    const Mn8Slot* s = &mn8css.slot[k];

    if (mn8Css_Teams()) {
        return (u8) (team_coin[s->team] + (s->kind == SLOT_CPU ? 4 : 0));
    }
    return s->kind == SLOT_HMN ? (u8) k : COIN_COLOR_CPU;
}

/// Coins sit on their character's icon, nudged apart from each other and
/// kept inside the icon, sliding in at 3 units a frame: fn_80262648 without
/// the carried-in-hand state.
static void mn8Css_CoinsThink(void)
{
    int k;
    int j;

    for (k = 0; k < N_SLOTS; k++) {
        const Mn8Slot* s = &mn8css.slot[k];
        Mn8Coin* c = &mn8css.coin[k];
        f32 l, r, u, d;
        u8 color;

        if (s->kind == SLOT_OFF || s->ckind == ChKind_None ||
            !mnCharSel_PcIconBounds(s->ckind, &l, &r, &u, &d))
        {
            HSD_JObjSetFlagsAll(c->jobj, JOBJ_HIDDEN);
            c->ckind = ChKind_None;
            continue;
        }
        if (c->ckind != s->ckind) {
            /* New pick: drop it in the middle of the icon, flying in from
             * the panel. */
            if (c->ckind == ChKind_None) {
                c->dx = mn8Css_SlotCenterX(k);
                c->dy = PANEL_TOP;
            }
            c->x = (l + r) * 0.5F;
            c->y = (u + d) * 0.5F;
            c->ckind = s->ckind;
        }
        HSD_JObjClearFlagsAll(c->jobj, JOBJ_HIDDEN);

        /* Push apart from the other coins resting close by. */
        for (j = 0; j < N_SLOTS; j++) {
            const Mn8Coin* o = &mn8css.coin[j];
            f32 dx;
            f32 dy;
            f32 dist;

            if (j == k || o->ckind == ChKind_None) {
                continue;
            }
            dx = c->x - o->x + 0.1F * (HSD_Randf() - 0.5F);
            dy = c->y - o->y + 0.1F * (HSD_Randf() - 0.5F);
            dist = dx * dx + dy * dy;
            if (dist < 8.0F && dist > 0.0F) {
                dist = sqrtf(dist);
                c->x += 0.05F * dx / dist;
                c->y += 0.05F * dy / dist;
            }
        }
        if (c->x < l + 1.5F) c->x = l + 1.5F;
        if (c->x > r - 1.5F) c->x = r - 1.5F;
        if (c->y > u - 1.5F) c->y = u - 1.5F;
        if (c->y < d + 1.5F) c->y = d + 1.5F;

        {
            f32 dx = c->x - c->dx;
            f32 dy = c->y - c->dy;
            f32 dist = dx * dx + dy * dy;

            if (dist < 9.0F) {
                c->dx = c->x;
                c->dy = c->y;
            } else {
                dist = sqrtf(dist);
                c->dx += 3.0F * dx / dist;
                c->dy += 3.0F * dy / dist;
            }
        }

        /* Label and colour; the colour animation replays every 40 frames. */
        color = mn8Css_CoinColor(k);
        if (color != c->color || ++c->timer > 0x27) {
            HSD_JObj* jobj;

            lb_80011E24(c->jobj, &jobj, COIN_LABEL_JOINT, -1);
            HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim,
                            AOBJ_ARG_AF,
                            (f32) (s->kind == SLOT_HMN ? k * 4
                                                       : COIN_LABEL_CPU));
            HSD_JObjAnimAll(jobj);
            HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                            AOBJ_ARG_AOV, NULL);
            lb_80011E24(c->jobj, &jobj, COIN_COLOR_JOINT, -1);
            HSD_ForeachAnim(jobj, JOBJ_TYPE, MOBJ_MASK, HSD_AObjReqAnim,
                            AOBJ_ARG_AF, (f32) (color * 0x28));
            c->color = color;
            c->timer = 0;
        }
        HSD_JObjSetTranslateX(c->jobj, c->dx);
        HSD_JObjSetTranslateY(c->jobj, c->dy);
        HSD_JObjSetTranslateZ(c->jobj, 1.0F);
        HSD_JObjAnimAll(c->jobj);
    }
}

/// "Ready to fight" whenever Start would go through; the same loop as
/// fn_80262F44 (play from 0, then repeat 10-100).
static void mn8Css_BannerThink(void)
{
    if (!mn8Css_CanStart()) {
        HSD_JObjSetFlagsAll(mn8css.banner, JOBJ_HIDDEN);
        mn8css.banner_timer = 0;
        return;
    }
    if (mn8css.banner_timer == 0) {
        HSD_ForeachAnim(mn8css.banner, JOBJ_TYPE, ALL_TYPE_MASK,
                        HSD_AObjReqAnim, AOBJ_ARG_AF, 0.0);
    } else if (mn8css.banner_timer > 100) {
        HSD_ForeachAnim(mn8css.banner, JOBJ_TYPE, ALL_TYPE_MASK,
                        HSD_AObjReqAnim, AOBJ_ARG_AF, 10.0);
        mn8css.banner_timer = 10;
    }
    mn8css.banner_timer++;
    HSD_JObjClearFlagsAll(mn8css.banner, JOBJ_HIDDEN);
    HSD_JObjAnimAll(mn8css.banner);
}

/// Put a label box over where its panel is drawn this frame. The box keeps
/// the layout's width; it is centred on the panel as that eases in.
static void mn8Css_PlaceText(HSD_Text* text, int k)
{
    f32 box_w;

    if (text == NULL) {
        return;
    }
    box_w = text->box_size_x * text->font_size.x;
    text->pos_x =
        mn8css.motion[k].x0 + mn8css.motion[k].w * 0.5F - box_w * 0.5F;
    text->pos_y = -(PANEL_TOP + mn8css.motion[k].dy);
}

static f32 mn8Css_Ease(f32 cur, f32 target)
{
    f32 d = target - cur;

    if (d < 0.01F && d > -0.01F) {
        return target;
    }
    return cur + d * MOTION_EASE;
}

static void mn8Css_MotionThink(void)
{
    int k;

    mn8css.shown_h = mn8Css_Ease(mn8css.shown_h, mn8css.panel_h);
    for (k = 0; k <= N_SLOTS; k++) {
        if (!mn8css.motion[k].live) {
            continue;
        }
        if (mn8css.motion[k].leaving) {
            mn8css.motion[k].dy =
                mn8Css_Ease(mn8css.motion[k].dy, -MOTION_DROP);
            if (mn8css.motion[k].dy <= -MOTION_DROP + 0.5F) {
                mn8css.motion[k].live = false;
            }
            continue;
        }
        mn8css.motion[k].x0 =
            mn8Css_Ease(mn8css.motion[k].x0, mn8css.motion[k].tx0);
        mn8css.motion[k].w = mn8Css_Ease(mn8css.motion[k].w, mn8css.motion[k].tw);
        mn8css.motion[k].dy = mn8Css_Ease(mn8css.motion[k].dy, 0.0F);
        mn8Css_PlaceText(k == TILE_ADD ? mn8css.add_text : mn8css.text[k], k);
    }
}

static void mn8Css_InputThink(HSD_GObj* gobj)
{
    int p;

    if (mn8css.leaving) {
        return;
    }
    for (p = 0; p < N_PORTS; p++) {
        if (mn8Css_HandInput(p)) {
            return;
        }
    }
    for (p = 0; p < N_PORTS; p++) {
        mn8Css_PoseHand(p);
    }
    mn8Css_Relayout();
    mn8Css_MotionThink();
    mn8Css_CoinsThink();
    mn8Css_BannerThink();
    mn8Css_ResolvePortraits();
    mn8Css_RefreshText();
}

static void mn8Css_BackgroundThink(HSD_GObj* gobj)
{
    /* Same 200-frame loop as fn_80263354, on this screen's own frame count. */
    if (mn8css.frame % 200 == 0) {
        HSD_JObjReqAnimAll(mn8css.background, 0.0F);
    }
    HSD_JObjAnimAll(mn8css.background);
}

/* ---- scene -------------------------------------------------------------- */

/* Anything in the grid model whose origin sits below this is part of the
 * vanilla player row (door frames, name plates, stars), not the grid, whose
 * lowest icons end at y -1 (ICONROWHT_BTM_BTM in mncharsel.c). */
#define GRID_FLOOR_Y (-3.0F)

/// Hide every subtree of @p jobj rooted below GRID_FLOOR_Y. Returns how many
/// subtrees were hidden.
static int mn8Css_HideBelowGrid(HSD_JObj* jobj)
{
    int hidden = 0;

    for (; jobj != NULL; jobj = jobj->next) {
        Vec3 pos;

        lb_8000B1CC(jobj, NULL, &pos);
        if (pos.y < GRID_FLOOR_Y) {
            HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
            hidden++;
        } else if (!(jobj->flags & JOBJ_INSTANCE)) {
            hidden += mn8Css_HideBelowGrid(jobj->child);
        }
    }
    return hidden;
}

static HSD_JObj* mn8Css_LoadModel(StaticModelDesc* desc)
{
    HSD_JObj* jobj = HSD_JObjLoadJoint(DP(HSD_Joint, desc->joint));

    HSD_JObjAddAnimAll(jobj, DP(HSD_AnimJoint, desc->animjoint),
                       DP(HSD_MatAnimJoint, desc->matanim_joint),
                       DP(HSD_ShapeAnimJoint, desc->shapeanim_joint));
    return jobj;
}

static void mn8Css_BuildScene(void)
{
    MnSelectChrDataTable* data = mn8css.data;
    MnSelectChrModels* models = &data->models;
    HSD_GObj* gobj;
    HSD_JObj* menu;
    int i;

    /* Camera, text canvas, lights and fog: as mnCharSel_802640A0. */
    gobj = mn8css.camera = GObj_Create(2, 3, 0x80);
    /* mn_8022BA1C (the parallax proc below) re-inits the camera from this
     * global every frame. Left alone it still points into the previous
     * scene's archive, which has been freed. */
    MenMain_cam = DP(HSD_CObjDesc, data->cam);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind,
                            HSD_CObjLoadDesc(MenMain_cam));
    GObj_SetupGXLinkMax(gobj, HSD_GObj_803910D8, 0);
    gobj->gxlink_prios = 0x1F;
    HSD_GObj_SetupProc(gobj, mn_8022BA1C, 5); /* menu C-stick parallax */
    mn8css.text_ctx = HSD_SisLib_803A611C(0, gobj, 7, 8, 0x80, 1, 0x80, 0);

    gobj = GObj_Create(3, 4, 0x80);
    {
        HSD_LObj* l0 = HSD_LObjLoadDesc(DP(HSD_LightDesc, data->light0));
        HSD_LObj* l1 = HSD_LObjLoadDesc(DP(HSD_LightDesc, data->light1));
        HSD_LObjSetNext(l0, l1);
        HSD_GObjObject_80390A70(gobj, HSD_GObj_LightKind, l0);
    }
    GObj_SetupGXLink(gobj, HSD_GObj_LObjCallback, 0, 0x80);

    gobj = GObj_Create(0xE, 2, 0);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_FogKind,
                            HSD_FogLoadDesc(DP(HSD_FogDesc, data->fog)));
    GObj_SetupGXLink(gobj, (GObj_RenderFunc) (Event) fn_8026407C, 0, 0x80);

    /* Backdrop. */
    gobj = GObj_Create(4, 5, 0x80);
    mn8css.background = mn8Css_LoadModel(&models->background);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, mn8css.background);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x80);
    HSD_GObj_SetupProc(gobj, mn8Css_BackgroundThink, 4);
    HSD_JObjReqAnimAll(mn8css.background, 0.0F);
    HSD_JObjAnimAll(mn8css.background);

    /* Character grid, held at frame 0 like the vanilla screen. */
    gobj = GObj_Create(4, 5, 0x80);
    menu = mn8css.menu = mn8Css_LoadModel(&models->menu);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, menu);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x80);
    HSD_JObjReqAnimAll(menu, 0.0F);
    HSD_JObjAnimAll(menu);
    HSD_ForeachAnim(menu, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    mnCharSel_PcSetupIcons(menu);
    {
        HSD_JObj* sign;
        lb_80011E24(menu, &sign, MODE_SIGN_JOINT, -1);
        HSD_ForeachAnim(sign, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim,
                        AOBJ_ARG_AF,
                        mnCharSel_PcModeFrame(mn8css.css->match_type,
                                              mn8Css_Teams()));
        HSD_JObjAnimAll(sign);
        HSD_ForeachAnim(sign, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                        AOBJ_ARG_AOV, NULL);
    }
    {
        HSD_JObj* src;

        lb_80011E24(menu, &src, mnCharSel_PcPortraitJoint(), -1);
        mn8css.portrait_tobj = mn8Css_FindAnimTObj(src);
        pc_log_line("[8css] portrait joint 0x%X: jobj %p, animated tobj %p",
                    mnCharSel_PcPortraitJoint(), (void*) src,
                    (void*) mn8css.portrait_tobj);
        for (i = 0; i < N_SLOTS; i++) {
            mn8css.portrait[i].frame = -1;
            mn8css.portrait[i].image = NULL;
        }
    }
    /* The grid model carries the vanilla player row too; clear it out so
     * only the panels below are left there. Children only: the root spans
     * the whole screen. */
    pc_log_line("[8css] hid %d grid-model subtrees below the grid",
                mn8Css_HideBelowGrid(menu->child));
    for (i = 0; i < (int) ARRAY_SIZE(mn8css_hidden_joints); i++) {
        HSD_JObj* jobj;
        lb_80011E24(menu, &jobj, mn8css_hidden_joints[i], -1);
        if (jobj != NULL) {
            HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        }
    }

    /* Panels, on the grid's link and created before the text so the labels
     * land on top of them. */
    gobj = GObj_Create(4, 5, 0x80);
    GObj_SetupGXLink(gobj, mn8Css_Draw, 1, 0x80);
    /* The previous visit's labels went with its text context. */
    for (i = 0; i < N_SLOTS; i++) {
        mn8css.text[i] = NULL;
    }
    mn8css.add_text = NULL;
    mn8css.layout_key = -1;
    for (i = 0; i <= N_SLOTS; i++) {
        mn8css.motion[i].live = false;
        mn8css.motion[i].leaving = false;
    }
    mn8Css_Relayout();
    mn8css.shown_h = mn8css.panel_h;
    /* The whole row rises in on entry, as the vanilla doors open. */
    mn8Css_MotionThink();
    {
        /* How-to line in the gap between the grid (bottom y -1) and the
         * panels (top y PANEL_TOP). No '/' or '&': see mn8Css_PanelName. */
        HSD_Text* hint = HSD_SisLib_803A6754(0, mn8css.text_ctx);
        f32 w = 70.0F;

        hint->x4C = 1;
        hint->default_fitting = 1;
        hint->default_alignment = 1;
        hint->default_kerning = 1;
        hint->font_size.x = TEXT_FONT_X * 0.7F;
        hint->font_size.y = TEXT_FONT_Y * 0.7F;
        hint->pos_x = -35.0F;
        hint->pos_y = 1.6F;
        hint->pos_z = 0.0F;
        hint->box_size_x = w / hint->font_size.x;
        hint->box_size_y = 60.0F;
        HSD_SisLib_803A6B98(hint, hint->box_size_x * 0.5F, 0.0F, "%s",
                            "A: pick, grab a panel, or Add   A again on a panel: "
                            "HMN, CPU or Off   X and Y: color or team   "
                            "L and R: level   hold B: back");
    }

    /* A coin per slot, on the grid, under the hands. */
    for (i = 0; i < N_SLOTS; i++) {
        Mn8Coin* c = &mn8css.coin[i];

        gobj = GObj_Create(4, 5, 0x80);
        c->jobj = mn8Css_LoadModel(&models->token);
        HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, c->jobj);
        GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 2, 0x80);
        HSD_JObjReqAnimAll(c->jobj, 0.0F);
        HSD_ForeachAnim(c->jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                        AOBJ_ARG_AOV, NULL);
        HSD_JObjSetFlagsAll(c->jobj, JOBJ_HIDDEN);
        c->ckind = ChKind_None;
        c->color = 0xFF;
        c->timer = 0;
    }

    /* Ready to fight banner, over everything but the hands' link. */
    gobj = GObj_Create(4, 5, 0x80);
    mn8css.banner = mn8Css_LoadModel(&models->press_start);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, mn8css.banner);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x80);
    HSD_JObjReqAnimAll(mn8css.banner, 0.0F);
    HSD_ForeachAnim(mn8css.banner, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    HSD_JObjSetFlagsAll(mn8css.banner, JOBJ_HIDDEN);
    mn8css.banner_timer = 0;

    /* Input, then one hand per port, drawn over everything. */
    gobj = GObj_Create(4, 5, 0x80);
    HSD_GObj_SetupProc(gobj, mn8Css_InputThink, 1);
    for (i = 0; i < N_PORTS; i++) {
        Mn8Hand* h = &mn8css.hand[i];

        gobj = GObj_Create(4, 5, 0x80);
        h->jobj = mn8Css_LoadModel(&models->hand);
        HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, h->jobj);
        GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 3, 0x80);
        HSD_JObjReqAnimAll(h->jobj, 0.0F);
        HSD_JObjAnimAll(h->jobj);
        HSD_ForeachAnim(h->jobj, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                        AOBJ_ARG_AOV, NULL);
        h->x = mn8Css_SlotCenterX(i);
        h->y = HAND_START_Y;
        h->target = (s8) i;
        h->hover = -1;
        h->shown = i == 0 || HSD_PadCopyStatus[i].err == 0;
        h->on_sign = false;
        h->b_armed = false;
        h->b_held = 0;
        mn8Css_PoseHand(i);
    }
}

bool mn8Css_Claim(CSSData* css)
{
    mn8css.running = css->match_type == VS_MELEE &&
                     gm_GetCurrentGameMode() == GM_VS &&
                     gm8Player_IsEnabled();
    return mn8css.running;
}

bool mn8Css_Running(void)
{
    return mn8css.running;
}

void mn8Css_OnEnter(CSSData* css)
{
    mn8css.css = css;
    mn8css.frame = 0;
    mn8css.pending = 0;
    mn8css.leaving = false;

    /* Mirrors mnCharSel_Scene_OnEnter for VS. */
    lbCardNew_AllocWorkArea();
    lbCardGame_LoadArchive(0);
    lbAudioAx_80026F2C(0x1E);
    lbAudioAx_8002702C(2, 8);
    lbAudioAx_80027168();
    lbAudioAx_80027648();
    if (lbLang_IsSavedLanguageJP()) {
        mn8css.archive = lbArchive_LoadArchive("MnSlChr.dat");
        mn8css.archive_ext = lbArchive_LoadArchive("MnExtAll.dat");
        HSD_SisLib_803A62A0(0, "SdSlChr.dat", "SIS_SelCharData");
    } else {
        mn8css.archive = lbArchive_LoadArchive("MnSlChr.usd");
        mn8css.archive_ext = lbArchive_LoadArchive("MnExtAll.usd");
        HSD_SisLib_803A62A0(0, "SdSlChr.usd", "SIS_SelCharData");
    }
    mn8css.data =
        HSD_ArchiveGetPublicAddress(mn8css.archive, "MnSelectChrDataTable");

    mn8Css_LoadSlots();
    mn8Css_BuildScene();
    mnCharSel_PcEnterSfx(css->match_type);
    pc_log_line("[8css] 8-slot character select screen entered");
}

void mn8Css_OnFrame(void)
{
    struct GameCache* cache;
    int i;

    mn8css.frame++;
    if (mn8css.leaving) {
        return;
    }
    /* Start streaming the port players' characters in, like the vanilla
     * screen. The extras load at match start, which is how the earlier CPU
     * panels already ran. */
    cache = &lbDvd_GetPreloadCacheScene()->game_cache;
    for (i = 0; i < N_PORTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        if (s->kind != SLOT_OFF && s->ckind != ChKind_None) {
            cache->entries[i].char_id = s->ckind;
            cache->entries[i].color = mn8Css_MatchColor(s);
        } else {
            cache->entries[i].char_id = ChKind_None;
        }
    }
    lbDvd_80018254();
}

void mn8Css_OnExit(void)
{
    HSD_SisLib_803A5FBC();
    if (mn8css.archive != NULL) {
        lbArchive_80016EFC(mn8css.archive);
        mn8css.archive = NULL;
    }
    if (mn8css.archive_ext != NULL) {
        lbArchive_80016EFC(mn8css.archive_ext);
        mn8css.archive_ext = NULL;
    }
    mn8css.css->pending_scene_change = mn8css.pending;
    mn8css.running = false;
    pc_log_line("[8css] left with pending_scene_change=%d", mn8css.pending);
}
