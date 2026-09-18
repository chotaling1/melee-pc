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
 * Milestone 1: one hand, driven by the mouse or any controller.
 *   Point at a panel + A      select it (A again cycles HMN/CPU/Off)
 *   Point at a character + A  give it to the selected panel, then select the
 *                             next panel
 *   X                         cycle HMN/CPU/Off on the selected panel
 *   L / R                     CPU level down / up
 *   D-pad Left / Right        select the previous / next panel
 *   Start                     stage select;  B  back to the main menu
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
#define TEXT_LINE_STATUS (265.0F)

#define HAND_MIN_X (-35.0F)
#define HAND_MAX_X (35.0F)
#define HAND_MIN_Y (-22.0F)
#define HAND_MAX_Y (25.0F)
#define HAND_SPEED (0.9F)
#define STICK_DEADZONE (0.25F)

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
    u8 kind;  ///< SLOT_*
    u8 ckind; ///< ::CharacterKind, or ChKind_None
    u8 level; ///< CPU level 1-9
} Mn8Slot;

static struct {
    bool running;
    CSSData* css;
    HSD_Archive* archive;
    HSD_Archive* archive_ext;
    MnSelectChrDataTable* data;
    HSD_GObj* camera;
    HSD_JObj* background;
    HSD_JObj* hand;
    int text_ctx;
    HSD_Text* text[N_SLOTS];
    Mn8Slot slot[N_SLOTS];
    Mn8Slot shown[N_SLOTS]; ///< what the labels currently say
    int active;
    int hover_panel;
    f32 hand_x;
    f32 hand_y;
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
        any_port |= s->kind != SLOT_OFF;
    }
    for (i = N_PORTS; i < N_SLOTS; i++) {
        Mn8Slot* s = &mn8css.slot[i];
        int ckind = gm8Player_PanelCkind(i - N_PORTS);

        s->kind = mn8Css_IsPlayable(ckind) ? SLOT_CPU : SLOT_OFF;
        s->ckind = mn8Css_IsPlayable(ckind) ? ckind : ChKind_None;
        s->level = gm8Player_PanelLevel(i - N_PORTS);
    }
    /* First visit: nobody has joined yet, so seat player 1. */
    if (!any_port) {
        mn8css.slot[0].kind = SLOT_HMN;
    }
}

/// First costume of @p ckind not already worn by an earlier slot.
static u8 mn8Css_FreeCostume(int upto, u8 ckind,
                             const u8 costume_of[N_SLOTS])
{
    u8 count = gm_GetNumCostumesForCKind(ckind);
    u8 c;
    int i;

    for (c = 0; c < count; c++) {
        bool taken = false;
        for (i = 0; i < upto; i++) {
            const Mn8Slot* s = &mn8css.slot[i];
            if (s->kind != SLOT_OFF && s->ckind == ckind && costume_of[i] == c)
            {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return c;
        }
    }
    return 0;
}

/// Write the selection back where the rest of the game reads it.
static void mn8Css_CommitSlots(void)
{
    StartMeleeData* start = &mn8css.css->vs.start;
    u8 costume_of[N_SLOTS] = { 0 };
    int i;

    for (i = 0; i < N_PORTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        PlayerInitData* p = &start->players[i];

        if (s->kind == SLOT_OFF || s->ckind == ChKind_None) {
            p->slot_type = Gm_PKind_NA;
            continue;
        }
        costume_of[i] = mn8Css_FreeCostume(i, s->ckind, costume_of);
        p->slot_type = s->kind == SLOT_HMN ? Gm_PKind_Human : Gm_PKind_Cpu;
        p->ckind = (s8) s->ckind;
        p->color = costume_of[i];
        p->cpu_level = s->level;
        /* The regular VS AI; CpuKind_0 is Training Mode's standing dummy. */
        p->cpu_kind = CpuKind_4;
    }
    /* Slots 4-7 go through the CPU panels; gm8Player_ConfigureMatch picks
     * their costumes against the players above. */
    for (i = N_PORTS; i < N_SLOTS; i++) {
        const Mn8Slot* s = &mn8css.slot[i];
        bool on = s->kind != SLOT_OFF && s->ckind != ChKind_None;

        gm8Player_PanelSetCkind(i - N_PORTS, on ? s->ckind : ChKind_None);
        gm8Player_PanelSetLevel(i - N_PORTS, s->level);
    }
}

/// Start needs two fighters, every joined slot to have picked, and someone
/// on a controller port (gm8Player_ConfigureMatch copies the match rules
/// from the first of them).
static bool mn8Css_CanStart(void)
{
    int fighters = 0;
    bool port_fighter = false;
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

static GXColor mn8Css_PanelColor(int k)
{
    static const GXColor port_color[N_PORTS] = {
        { 0xC0, 0x2E, 0x2E, 0xFF }, /* P1 red */
        { 0x2E, 0x4C, 0xC0, 0xFF }, /* P2 blue */
        { 0xC0, 0xA0, 0x22, 0xFF }, /* P3 yellow */
        { 0x2E, 0x96, 0x40, 0xFF }, /* P4 green */
    };
    const Mn8Slot* s = &mn8css.slot[k];

    if (s->kind == SLOT_HMN) {
        return port_color[k];
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
    static const GXColor frame_active = { 0xFF, 0xD6, 0x2E, 0xFF };
    static const GXColor tray = { 0x12, 0x16, 0x24, 0xFF };
    int k;

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
        GXColor frame = k == mn8css.active        ? frame_active
                        : k == mn8css.hover_panel ? frame_hover
                                                  : frame_plain;
        f32 b = k == mn8css.active ? PANEL_BORDER * 1.6F : PANEL_BORDER;

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
    mn8css.text[k] = text;
    /* force the first refresh to rewrite both lines */
    mn8css.shown[k].kind = 0xFF;
}

static void mn8Css_RefreshText(void)
{
    char buf[24];
    int k;

    for (k = 0; k < N_SLOTS; k++) {
        const Mn8Slot* s = &mn8css.slot[k];
        Mn8Slot* shown = &mn8css.shown[k];

        if (shown->kind == s->kind && shown->ckind == s->ckind &&
            shown->level == s->level)
        {
            continue;
        }
        HSD_SisLib_803A70A0(mn8css.text[k], 1, "%s",
                            s->ckind == ChKind_None
                                ? (s->kind == SLOT_OFF ? "" : "Pick")
                                : gm8Player_CharName(s->ckind));
        HSD_SisLib_803A70A0(mn8css.text[k], 2, "%s",
                            mn8Css_KindLabel(s, buf, sizeof(buf)));
        *shown = *s;
    }
}

/* ---- input -------------------------------------------------------------- */

static void mn8Css_MoveHand(void)
{
    f32 sx = 0.0F;
    f32 sy = 0.0F;
    Vec3 cur;
    Vec3 target;
    int p;

    for (p = 0; p < N_PORTS; p++) {
        sx += HSD_PadCopyStatus[p].nml_stickX;
        sy += HSD_PadCopyStatus[p].nml_stickY;
    }
    if (sx > 1.0F) sx = 1.0F;
    if (sx < -1.0F) sx = -1.0F;
    if (sy > 1.0F) sy = 1.0F;
    if (sy < -1.0F) sy = -1.0F;
    if (sx > STICK_DEADZONE || sx < -STICK_DEADZONE) {
        mn8css.hand_x += sx * HAND_SPEED;
    }
    if (sy > STICK_DEADZONE || sy < -STICK_DEADZONE) {
        mn8css.hand_y += sy * HAND_SPEED;
    }

    /* The mouse wins while it moves, exactly as on the vanilla screen. */
    cur.x = mn8css.hand_x;
    cur.y = mn8css.hand_y;
    cur.z = 0.0F;
    if (mnMouse_GetPlanePoint(GET_COBJ(mn8css.camera), &cur, &target)) {
        mn8css.hand_x = target.x;
        mn8css.hand_y = target.y;
    }

    if (mn8css.hand_x < HAND_MIN_X) mn8css.hand_x = HAND_MIN_X;
    if (mn8css.hand_x > HAND_MAX_X) mn8css.hand_x = HAND_MAX_X;
    if (mn8css.hand_y < HAND_MIN_Y) mn8css.hand_y = HAND_MIN_Y;
    if (mn8css.hand_y > HAND_MAX_Y) mn8css.hand_y = HAND_MAX_Y;

    HSD_JObjSetTranslateX(mn8css.hand, mn8css.hand_x);
    HSD_JObjSetTranslateY(mn8css.hand, mn8css.hand_y);
}

static void mn8Css_Leave(u8 pending)
{
    mn8css.pending = pending;
    mn8css.leaving = true;
    gm_801A4B60();
}

static void mn8Css_HandThink(HSD_GObj* gobj)
{
    u32 trig = 0;
    int p;

    if (mn8css.leaving) {
        return;
    }
    for (p = 0; p < N_PORTS; p++) {
        trig |= HSD_PadCopyStatus[p].trigger;
    }

    mn8Css_MoveHand();
    mn8css.hover_panel = mn8Css_PanelAt(mn8css.hand_x, mn8css.hand_y);

    if (trig & HSD_PAD_A) {
        int ckind = mnCharSel_PcIconAt(mn8css.hand_x, mn8css.hand_y);

        pc_log_line("[8css] A at (%.1f, %.1f): panel=%d icon=%d active=P%d",
                    mn8css.hand_x, mn8css.hand_y, mn8css.hover_panel, ckind,
                    mn8css.active + 1);

        if (mn8css.hover_panel >= 0) {
            if (mn8css.hover_panel != mn8css.active) {
                mn8css.active = mn8css.hover_panel;
            } else {
                mn8Css_CycleKind(mn8css.active);
            }
            sfxMove();
        } else if (mn8Css_IsPlayable(ckind)) {
            Mn8Slot* s = &mn8css.slot[mn8css.active];

            s->ckind = (u8) ckind;
            /* One hand picks for everyone, so an empty slot joins as a CPU;
             * only P1 is assumed to be the person holding it. X switches a
             * port slot to HMN for a friend on that controller. */
            if (s->kind == SLOT_OFF) {
                s->kind = mn8css.active == 0 ? SLOT_HMN : SLOT_CPU;
            }
            /* Move on, so filling eight slots is eight clicks. */
            mn8css.active = (mn8css.active + 1) % N_SLOTS;
            sfxForward();
        }
    }
    if (trig & HSD_PAD_X) {
        mn8Css_CycleKind(mn8css.active);
        sfxMove();
    }
    if (trig & (HSD_PAD_L | HSD_PAD_R)) {
        Mn8Slot* s = &mn8css.slot[mn8css.active];
        int level = s->level + ((trig & HSD_PAD_R) ? 1 : -1);

        s->level = (u8) (level < 1 ? 1 : level > 9 ? 9 : level);
        sfxMove();
    }
    if (trig & HSD_PAD_DPADLEFT) {
        mn8css.active = (mn8css.active + N_SLOTS - 1) % N_SLOTS;
        sfxMove();
    }
    if (trig & HSD_PAD_DPADRIGHT) {
        mn8css.active = (mn8css.active + 1) % N_SLOTS;
        sfxMove();
    }

    mn8Css_RefreshText();

    if (trig & HSD_PAD_START) {
        if (mn8Css_CanStart()) {
            mn8Css_CommitSlots();
            sfxForward();
            mn8Css_Leave(1); /* same value the vanilla screen uses for Start */
        } else {
            sfxBack();
        }
    } else if (trig & HSD_PAD_B) {
        sfxBack();
        mn8Css_Leave(2); /* CSSPendingSceneChange_2: back to the menu */
    }
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
    menu = mn8Css_LoadModel(&models->menu);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, menu);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x80);
    HSD_JObjReqAnimAll(menu, 0.0F);
    HSD_JObjAnimAll(menu);
    HSD_ForeachAnim(menu, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    mnCharSel_PcSetupIcons(menu);
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
         * panels (top y PANEL_TOP). */
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
                            "A on a panel: select it   A on a character: "
                            "give it to the yellow panel   X: HMN/CPU/Off   "
                            "L/R: level   Start: go");
    }

    /* Hand, which also runs the screen's input. */
    gobj = GObj_Create(4, 5, 0x80);
    mn8css.hand = mn8Css_LoadModel(&models->hand);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, mn8css.hand);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 3, 0x80);
    HSD_GObj_SetupProc(gobj, mn8Css_HandThink, 1);
    HSD_JObjReqAnimAll(mn8css.hand, 0.0F);
    HSD_JObjAnimAll(mn8css.hand);
    HSD_ForeachAnim(mn8css.hand, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    mn8css.hand_x = 0.0F;
    mn8css.hand_y = 8.0F;
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
    mn8css.active = 0;
    mn8css.hover_panel = -1;

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
            cache->entries[i].color = 0;
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
