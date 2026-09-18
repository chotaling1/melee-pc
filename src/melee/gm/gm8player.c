/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gm8player.h"

#include <placeholder.h>

#include <dolphin/os.h>

#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <pc/pc.h>

/// First player slot used for the extra fighters: straight after the four
/// controller ports. See GM_MAX_PLAYERS for why there is no gap here.
#define GM8P_EXTRA_BASE 4
#define GM8P_EXTRA_COUNT (GM_MAX_PLAYERS - GM8P_EXTRA_BASE)

/// Deliberately boring picks: no transforming characters (Zelda/Sheik) and no
/// Ice Climbers, either of which spawns extra entities and would confuse a
/// first look at whether 8 plain fighters can coexist.
static const u8 gm8p_fill_chars[GM8P_EXTRA_COUNT] = {
    CKind_Mario,
    CKind_Donkey,
    CKind_Link,
    CKind_Kirby,
};

static int gm8p_active_count;

int gm8Player_ActiveCount(void)
{
    return gm8p_active_count;
}

int gm8Player_HudPlayerCount(int vanilla_count)
{
    /* StartMeleeRules::x0_3 carries the player count in 3 bits, so it cannot
     * say 8. Rather than reflow that bitfield (its neighbours are packed to
     * the retail byte layout), the HUD asks here instead. */
    return gm8p_active_count != 0 ? gm8p_active_count : vanilla_count;
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

void gm8Player_ConfigureMatch(StartMeleeData* start)
{
    const PlayerInitData* occupied_slot[GM8P_EXTRA_BASE];
    int occupied = 0;
    u8 cpu_level;
    int i;

    gm8p_active_count = 0;

    pc_log_line("[8p] hook reached, enabled=%d\n",
             pc_is_eight_player_enabled() ? 1 : 0);

    if (!pc_is_eight_player_enabled()) {
        gm8Player_ClearExtraSlots(start);
        return;
    }

    /* Collect who the CSS actually put in the match. Cloning one of these
     * keeps every unlisted field (ratios, scale, stocks, rumble) at whatever
     * this match was set up with. */
    for (i = 0; i < GM8P_EXTRA_BASE; i++) {
        if (start->players[i].slot_type != Gm_PKind_NA) {
            occupied_slot[occupied++] = &start->players[i];
        }
    }

    if (occupied == 0) {
        pc_log_line("[8p] no players in the match, nothing to pad\n");
        return;
    }

    /* Match the difficulty of the CPUs the player already set up, so a level
     * 9 match gets level 9 extras. With no CPUs in the match there is nothing
     * to copy; 5 is the middle of the CSS's 1-9 range. */
    cpu_level = 5;
    for (i = 0; i < occupied; i++) {
        if (occupied_slot[i]->slot_type == Gm_PKind_Cpu) {
            cpu_level = occupied_slot[i]->cpu_level;
            break;
        }
    }

    for (i = 0; i < GM8P_EXTRA_COUNT; i++) {
        PlayerInitData* p = &start->players[GM8P_EXTRA_BASE + i];
        /* Clone a character that is already in the match rather than adding a
         * new one. Melee keeps character data in ARAM, which the port caps at
         * 16 MB (PC_ARAM_SIZE), and four more unique fighters overruns it. */
        const PlayerInitData* donor = occupied_slot[i % occupied];

        *p = *donor;
        /* Leave slot at 0 so fn_8016D8AC assigns player_id from the array
         * index, which is what the CSS does for ports 1-4 and what
         * ft/fighter.c:695 expects. */
        p->slot = 0;
        p->slot_type = Gm_PKind_Cpu;
        /* CpuKind_0 is Training Mode's standing dummy (gmtrainingmode.c:125);
         * CpuKind_4 is the regular VS AI that gm_SetupPlayerDefaults gives
         * every CPU (gm_1601.c:3538). */
        p->cpu_kind = CpuKind_4;
        p->cpu_level = cpu_level;
        p->nametag = GM_NAMETAG_NONE;
        /* Stages only publish four spawn points, so the extra fighters reuse
         * them. Overlapping spawns push apart on their own. */
        p->spawn_pos = (s8) (i % 4);
        p->team = (u8) (i % GM_MAX_TEAMS);
    }

    gm8p_active_count = occupied + GM8P_EXTRA_COUNT;
    pc_log_line("[8p] padded %d player(s) out to %d fighters (slots %d-%d)\n",
             occupied, gm8p_active_count, GM8P_EXTRA_BASE,
             GM8P_EXTRA_BASE + GM8P_EXTRA_COUNT - 1);
}
