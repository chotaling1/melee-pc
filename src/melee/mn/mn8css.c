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
#include <melee/lb/lblanguage.h>
#include <melee/lb/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

#include <pc/pc.h>

#include <stdio.h>

#define N_SLOTS GM_MAX_PLAYERS
#define N_PORTS 4

/* Panel row, in CSS world units. The grid sits in y -1..20 and the vanilla
 * doors used the space below it; the hand reaches about x +-35, y -22..25. */
#define PANEL_TOP (-3.8F)
#define PANEL_BOTTOM (-19.6F)
#define PANEL_LEFT (-31.7F)
#define PANEL_PITCH (7.95F)
#define PANEL_WIDTH (7.35F)
#define PANEL_BORDER (0.3F)
#define PANEL_Z (0.0F)
/* Opaque tray behind the panels, covering the vanilla player row from just
 * under the grid to past the bottom of the screen. */
#define TRAY_TOP (-1.1F)
#define TRAY_BOTTOM (-32.0F)

/* Text inside a panel. font_size is world units per text pixel; box sizes and
 * line offsets below are in text pixels. */
#define TEXT_FONT_X (0.036F)
#define TEXT_FONT_Y (0.042F)
#define TEXT_LINE_TAG (20.0F)
#define TEXT_LINE_NAME (150.0F)
#define TEXT_LINE_STATUS (250.0F)
#define TEXT_LINE_COLOR (320.0F)

#define HAND_MIN_X (-35.0F)
#define HAND_MAX_X (35.0F)
#define HAND_MIN_Y (-22.0F)
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

#define N_TEAMS 3

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

static f32 mn8Css_PanelLeft(int k)
{
    return PANEL_LEFT + PANEL_PITCH * (f32) k;
}

static int mn8Css_PanelAt(f32 x, f32 y)
{
    int k;

    if (y > PANEL_TOP || y < PANEL_BOTTOM) {
        return -1;
    }
    for (k = 0; k < N_SLOTS; k++) {
        f32 x0 = mn8Css_PanelLeft(k);
        if (x >= x0 && x <= x0 + PANEL_WIDTH) {
            return k;
        }
    }
    return -1;
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

static void mn8Css_Draw(HSD_GObj* gobj, int pass)
{
    static const GXColor frame_plain = { 0x0C, 0x0C, 0x10, 0xFF };
    static const GXColor frame_hover = { 0xE0, 0xE2, 0xEA, 0xFF };
    static const GXColor tray = { 0x12, 0x16, 0x24, 0xFF };
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
    /* Tray over the whole vanilla player row, whichever model draws it. */
    mn8Css_Rect(-45.0F, TRAY_BOTTOM, 45.0F, TRAY_TOP, 0.0F, tray);
    for (k = 0; k < N_SLOTS; k++) {
        f32 x0 = mn8Css_PanelLeft(k);
        f32 x1 = x0 + PANEL_WIDTH;
        GXColor frame = frame_plain;
        f32 b = PANEL_BORDER;

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

        mn8Css_Rect(x0, PANEL_BOTTOM, x1, PANEL_TOP, PANEL_Z - 0.1F, frame);
        mn8Css_Rect(x0 + b, PANEL_BOTTOM + b, x1 - b, PANEL_TOP - b, PANEL_Z,
                    mn8Css_PanelColor(k));
    }
}

/* ---- text --------------------------------------------------------------- */

static void mn8Css_CreateText(int k)
{
    f32 box_w = PANEL_WIDTH / TEXT_FONT_X;
    f32 box_h = (PANEL_TOP - PANEL_BOTTOM) / TEXT_FONT_Y;
    HSD_Text* text = HSD_SisLib_803A6754(0, mn8css.text_ctx);
    char tag[8];

    text->x4C = 1;
    text->default_fitting = 1;
    text->default_alignment = 1;
    text->default_kerning = 1;
    text->font_size.x = TEXT_FONT_X;
    text->font_size.y = TEXT_FONT_Y;
    /* The text canvas runs y downward: world y maps to -pos_y, the same flip
     * the vanilla name plates apply. */
    text->pos_x = mn8Css_PanelLeft(k);
    text->pos_y = -PANEL_TOP;
    text->pos_z = 0.0F;
    text->box_size_x = box_w;
    text->box_size_y = box_h;

    snprintf(tag, sizeof(tag), "P%d", k + 1);
    HSD_SisLib_803A6B98(text, box_w * 0.5F, TEXT_LINE_TAG, "%s", tag);
    HSD_SisLib_803A6B98(text, box_w * 0.5F, TEXT_LINE_NAME, "%s", "-");
    HSD_SisLib_803A6B98(text, box_w * 0.5F, TEXT_LINE_STATUS, "%s", "-");
    HSD_SisLib_803A6B98(text, box_w * 0.5F, TEXT_LINE_COLOR, "%s", " ");
    mn8css.text[k] = text;
    /* force the first refresh to rewrite both lines */
    mn8css.shown[k].kind = 0xFF;
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

    if (h->x < HAND_MIN_X) h->x = HAND_MIN_X;
    if (h->x > HAND_MAX_X) h->x = HAND_MAX_X;
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

    /* The model's own Z is not on the menu plane, so pin it, and re-run its
     * animation to refresh the matrices. */
    HSD_JObjSetTranslateX(h->jobj, h->x);
    HSD_JObjSetTranslateY(h->jobj, h->y);
    HSD_JObjSetTranslateZ(h->jobj, 0.0F);
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
        if (mn8css.slot[k].kind == SLOT_OFF && k != port) {
            mn8Css_Release(port);
        }
        sfxMove();
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
    }
    sfxMove();
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
    sfxForward();
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
            sfxBack();
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
        sfxBack();
    }
    return false;
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
    for (i = 0; i < N_SLOTS; i++) {
        mn8Css_CreateText(i);
    }
    {
        /* How-to line in the gap between the grid (bottom y -1) and the
         * panels (top y PANEL_TOP). No '/' or '&': see mn8Css_PanelName. */
        HSD_Text* hint = HSD_SisLib_803A6754(0, mn8css.text_ctx);
        f32 w = 2.0F * -PANEL_LEFT;

        hint->x4C = 1;
        hint->default_fitting = 1;
        hint->default_alignment = 1;
        hint->default_kerning = 1;
        hint->font_size.x = TEXT_FONT_X * 0.8F;
        hint->font_size.y = TEXT_FONT_Y * 0.8F;
        hint->pos_x = PANEL_LEFT;
        hint->pos_y = 1.6F;
        hint->pos_z = 0.0F;
        hint->box_size_x = w / hint->font_size.x;
        hint->box_size_y = 60.0F;
        HSD_SisLib_803A6B98(hint, hint->box_size_x * 0.5F, 0.0F, "%s",
                            "A on a character: pick   A on a panel: grab it "
                            "or HMN, CPU, Off   X and Y: color or team   "
                            "L and R: level   hold B: back   Start: go");
    }

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
        h->x = mn8Css_PanelLeft(i) + PANEL_WIDTH * 0.5F;
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
