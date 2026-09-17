/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gm8player.h"

#include <placeholder.h>

#include <dolphin/os.h>

#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <pc/pc.h>

/// First player slot used for the extra fighters. Slots 0-3 are controller
/// ports and slots 4-5 belong to Camera Mode (cm/camera.c:1628, :1644).
#define GM8P_EXTRA_BASE 6
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

void gm8Player_ConfigureMatch(StartMeleeData* start)
{
    PlayerInitData* proto = NULL;
    int occupied = 0;
    int i;

    gm8p_active_count = 0;

    if (!pc_is_eight_player_enabled()) {
        return;
    }

    /* Count who the CSS actually put in the match, and keep the first of them
     * as a proto so every unlisted field (ratios, scale, stocks, rumble)
     * stays whatever this match was set up with. */
    for (i = 0; i < GM8P_EXTRA_BASE; i++) {
        if (start->players[i].slot_type != Gm_PKind_NA) {
            occupied++;
            if (proto == NULL) {
                proto = &start->players[i];
            }
        }
    }

    if (proto == NULL) {
        OSReport("[8p] no players in the match, nothing to pad\n");
        return;
    }

    for (i = 0; i < GM8P_EXTRA_COUNT; i++) {
        PlayerInitData* p = &start->players[GM8P_EXTRA_BASE + i];

        *p = *proto;
        p->slot = GM8P_EXTRA_BASE + i;
        p->slot_type = Gm_PKind_Cpu;
        p->ckind = (s8) gm8p_fill_chars[i];
        p->color = 0;
        p->cpu_kind = 0;
        p->cpu_level = 5;
        p->nametag = GM_NAMETAG_NONE;
        /* Stages only publish four spawn points, so the extra fighters reuse
         * them. Overlapping spawns push apart on their own. */
        p->spawn_pos = (s8) (i % 4);
        p->team = (u8) (i % GM_MAX_TEAMS);
    }

    gm8p_active_count = occupied + GM8P_EXTRA_COUNT;
    OSReport("[8p] padded %d player(s) out to %d fighters (slots %d-%d)\n",
             occupied, gm8p_active_count, GM8P_EXTRA_BASE,
             GM8P_EXTRA_BASE + GM8P_EXTRA_COUNT - 1);
}
