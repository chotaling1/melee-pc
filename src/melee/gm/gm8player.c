/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gm8player.h"

#include <placeholder.h>

#include <dolphin/os.h>

#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <pc/pc.h>

/// First player slot used for the extra fighters: straight after the four
/// controller ports. See GM_MAX_PLAYERS for why there is no gap here.
#define GM8P_EXTRA_BASE 4
#define GM8P_EXTRA_COUNT (GM_MAX_PLAYERS - GM8P_EXTRA_BASE)

#define GM8P_LEVEL_MIN 1
#define GM8P_LEVEL_MAX 9

/// One CPU panel on the character select screen. Panel k always fills player
/// slot GM8P_EXTRA_BASE + k, so "P5" means slot 4 whether or not P6-P8 are on.
typedef struct Gm8Panel {
    u8 ckind; ///< ::CharacterKind, or ChKind_None when the panel is off
    u8 level; ///< CPU level, GM8P_LEVEL_MIN..GM8P_LEVEL_MAX
} Gm8Panel;

/* Persist across visits to the CSS, like the vanilla doors do. */
static Gm8Panel gm8p_panels[GM8P_EXTRA_COUNT] = {
    { CKind_Mario, 5 },
    { CKind_Donkey, 5 },
    { CKind_Link, 5 },
    { CKind_Kirby, 5 },
};
static int gm8p_focus;
static bool gm8p_css_active;
static bool gm8p_padded;
static int gm8p_active_count;

/* Indexed by ::CharacterKind for the playable range. */
static const char* const gm8p_char_names[] = {
    "Captain Falcon", "Donkey Kong", "Fox",        "Mr. Game & Watch",
    "Kirby",          "Bowser",      "Link",       "Luigi",
    "Mario",          "Marth",       "Mewtwo",     "Ness",
    "Peach",          "Pikachu",     "Ice Climbers", "Jigglypuff",
    "Samus",          "Yoshi",       "Zelda",      "Sheik",
    "Falco",          "Young Link",  "Dr. Mario",  "Roy",
    "Pichu",          "Ganondorf",
};

static bool gm8p_OwnsMatch(void);

bool gm8Player_IsEnabled(void)
{
    return pc_is_eight_player_enabled();
}

int gm8Player_ActiveCount(void)
{
    return gm8p_active_count;
}

int gm8Player_HudPlayerCount(int vanilla_count)
{
    /* StartMeleeRules::x0_3 carries the player count in 3 bits, so it cannot
     * say 8. Rather than reflow that bitfield (its neighbours are packed to
     * the retail byte layout), the HUD asks here instead.
     *
     * Always the full eight-wide layout once any extra is in: panel k is
     * pinned to slot 4 + k and the HUD positions by slot, so a narrower
     * layout would leave the high slots with no position. Empty slots simply
     * draw nothing, the same gap vanilla leaves for an empty port.
     *
     * gm8p_padded outlives the match, and this is reached from the VS scene
     * that Classic, Adventure and Event matches share, so only honour it in
     * the mode that set it. */
    return gm8p_OwnsMatch() ? GM_MAX_PLAYERS : vanilla_count;
}

void gm8Player_ClearExtraSlots(StartMeleeData* start)
{
    int i;

    /* The player loops now run to GM_MAX_PLAYERS, so every slot past vanilla
     * has to be a valid empty one before anything walks the array. Zeroed
     * memory is not empty here: Gm_PKind_Human is 0, so an untouched slot
     * reads as a human player and gets a fighter built for it. */
    for (i = GM8P_EXTRA_BASE; i < GM_MAX_PLAYERS; i++) {
        gm_SetupPlayerDefaults(&start->players[i]);
    }
}

/// Whether this match's slots past vanilla belong to the 8-player mod.
static bool gm8p_OwnsMatch(void)
{
    return gm8p_padded && gm_GetCurrentGameMode() == GM_VS;
}

void gm8Player_SanitizeSlots(StartMeleeData* start)
{
    int i;

    /* Slots GM_DISC_MAX_PLAYERS and up did not exist in the retail game, and
     * several modes (Adventure, Event, ...) build their StartMeleeData by hand
     * without gm_SetupAllPlayerDefaults. Their copies of those slots are
     * whatever the storage held, and zeroed reads as a human player. Every
     * mode's match goes through the VS scene setup, so reset them there
     * unless this match is one the 8-player mod set up. */
    if (gm8p_OwnsMatch()) {
        return;
    }
    for (i = GM_DISC_MAX_PLAYERS; i < GM_MAX_PLAYERS; i++) {
        gm_SetupPlayerDefaults(&start->players[i]);
    }
}

/* ---- panel state, edited from the CSS (mncharsel.c) ------------------- */

int gm8Player_PanelCount(void)
{
    return GM8P_EXTRA_COUNT;
}

int gm8Player_PanelFocus(void)
{
    return gm8p_focus;
}

void gm8Player_PanelFocusMove(int dir)
{
    gm8p_focus = (gm8p_focus + dir + GM8P_EXTRA_COUNT) % GM8P_EXTRA_COUNT;
}

int gm8Player_PanelCkind(int k)
{
    return gm8p_panels[k].ckind;
}

void gm8Player_PanelSetCkind(int k, int ckind)
{
    gm8p_panels[k].ckind = (u8) ckind;
}

int gm8Player_PanelLevel(int k)
{
    return gm8p_panels[k].level;
}

void gm8Player_PanelLevelMove(int k, int dir)
{
    int level = gm8p_panels[k].level + dir;

    if (level < GM8P_LEVEL_MIN) {
        level = GM8P_LEVEL_MIN;
    } else if (level > GM8P_LEVEL_MAX) {
        level = GM8P_LEVEL_MAX;
    }
    gm8p_panels[k].level = (u8) level;
}

void gm8Player_PanelSetLevel(int k, int level)
{
    gm8p_panels[k].level = (u8) (level < GM8P_LEVEL_MIN   ? GM8P_LEVEL_MIN
                                 : level > GM8P_LEVEL_MAX ? GM8P_LEVEL_MAX
                                                          : level);
}

void gm8Player_SetCssActive(bool active)
{
    gm8p_css_active = active;
}

const char* gm8Player_CharName(int ckind)
{
    if (ckind < 0 || ckind >= (int) ARRAY_SIZE(gm8p_char_names)) {
        return "Off";
    }
    return gm8p_char_names[ckind];
}

/* ---- overlay getters (src/pc/launcher.cpp) ---------------------------- */

bool pc_8p_css_active(void)
{
    return gm8p_css_active && pc_is_eight_player_enabled();
}

int pc_8p_focus(void)
{
    return gm8p_focus;
}

const char* pc_8p_panel_name(int k)
{
    return gm8Player_CharName(gm8p_panels[k].ckind);
}

/// CPU level, or 0 when the panel is off.
int pc_8p_panel_level(int k)
{
    return gm8p_panels[k].ckind == ChKind_None ? 0 : gm8p_panels[k].level;
}

/* ---- match setup ------------------------------------------------------ */

/// First costume for @p ckind that no earlier slot of the same character is
/// already wearing, so two Marios never spawn identical.
static u8 gm8p_FreeCostume(const StartMeleeData* start, int upto, u8 ckind)
{
    u8 count = gm_GetNumCostumesForCKind(ckind);
    u8 costume;
    int i;

    for (costume = 0; costume < count; costume++) {
        bool taken = false;

        for (i = 0; i < upto; i++) {
            const PlayerInitData* q = &start->players[i];
            if (q->slot_type != Gm_PKind_NA && q->ckind == ckind &&
                q->color == costume)
            {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return costume;
        }
    }
    return 0;
}

void gm8Player_ConfigureMatch(StartMeleeData* start)
{
    const PlayerInitData* base = NULL;
    int occupied = 0;
    int extras = 0;
    int i;

    gm8p_active_count = 0;
    gm8p_padded = false;

    gm8Player_ClearExtraSlots(start);
    if (!pc_is_eight_player_enabled()) {
        return;
    }

    /* The first real player supplies every field the panels don't set
     * (stocks, handicap, damage/defense ratios, model scale), so the extras
     * play by the same rules as the match the player set up. */
    for (i = 0; i < GM8P_EXTRA_BASE; i++) {
        if (start->players[i].slot_type != Gm_PKind_NA) {
            if (base == NULL) {
                base = &start->players[i];
            }
            occupied++;
        }
    }
    if (base == NULL) {
        pc_log_line("[8p] no players in the match, nothing to pad");
        return;
    }

    for (i = 0; i < GM8P_EXTRA_COUNT; i++) {
        const Gm8Panel* panel = &gm8p_panels[i];
        int slot = GM8P_EXTRA_BASE + i;
        PlayerInitData* p = &start->players[slot];

        if (panel->ckind == ChKind_None) {
            continue; /* already a valid empty slot */
        }

        *p = *base;
        p->ckind = (s8) panel->ckind;
        p->color = gm8p_FreeCostume(start, slot, panel->ckind);
        /* Leave slot at 0 so fn_8016D8AC assigns player_id from the array
         * index, which is what the CSS does for ports 1-4 and what
         * ft/fighter.c:695 expects. */
        p->slot = 0;
        p->slot_type = Gm_PKind_Cpu;
        /* CpuKind_0 is Training Mode's standing dummy (gmtrainingmode.c:125);
         * CpuKind_4 is the regular VS AI that gm_SetupPlayerDefaults gives
         * every CPU (gm_1601.c:3538). */
        p->cpu_kind = CpuKind_4;
        p->cpu_level = panel->level;
        p->nametag = GM_NAMETAG_NONE;
        /* Stages only publish four spawn points, so the extra fighters reuse
         * them. Overlapping spawns push apart on their own. */
        p->spawn_pos = (s8) (i % 4);
        p->team = (u8) (i % GM_MAX_TEAMS);
        extras++;
    }

    gm8p_padded = extras != 0;
    gm8p_active_count = occupied + extras;
    pc_log_line("[8p] %d player(s) + %d CPU panel(s) = %d fighters", occupied,
                extras, gm8p_active_count);
}
