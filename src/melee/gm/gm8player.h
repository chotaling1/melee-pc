/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_GM_GM8PLAYER_H
#define MELEE_GM_GM8PLAYER_H

#include <Runtime/platform.h>

#include <melee/mn/forward.h>

/// PC mod: pad a VS match out to 8 fighters with CPUs.
///
/// Player slots 0-3 are controller ports 1-4 and the extra fighters take 4-7.
/// The slots have to be contiguous and stop at 8; see ::GM_MAX_PLAYERS.
///
/// Called from #onEnterVs *before* #gmVsMelee_EnterVs, which copies every slot
/// out of the VsModeData.
void gm8Player_ConfigureMatch(StartMeleeData* start);

/// Reset the slots past vanilla to a valid empty state. Must run before
/// anything iterates the player array, including on the character select
/// screen, which also builds fighters.
void gm8Player_ClearExtraSlots(StartMeleeData* start);

/// Player count the HUD should lay out for. ::StartMeleeRules::x0_3 is only
/// 3 bits wide and cannot represent 8, so HUD setup calls this instead.
int gm8Player_HudPlayerCount(int vanilla_count);

/// Number of fighters the last configured match was padded out to.
int gm8Player_ActiveCount(void);

#endif
