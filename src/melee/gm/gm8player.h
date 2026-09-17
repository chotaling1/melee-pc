/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_GM_GM8PLAYER_H
#define MELEE_GM_GM8PLAYER_H

#include <Runtime/platform.h>

#include <melee/mn/forward.h>

/// PC mod: pad a VS match out to 8 fighters with CPUs.
///
/// Player slots 0-3 are controller ports 1-4 and slots 4-5 are reserved by
/// Camera Mode, so the extra fighters occupy slots 6-9. See ::GM_MAX_PLAYERS.
///
/// Called from #onEnterVs before the match reads #StartMeleeData.
void gm8Player_ConfigureMatch(StartMeleeData* start);

/// Reset the slots past vanilla to a valid empty state. Must run before
/// anything iterates the player array, including on the character select
/// screen, which also builds fighters.
void gm8Player_ClearExtraSlots(StartMeleeData* start);

/// Number of fighters the last configured match was padded out to.
int gm8Player_ActiveCount(void);

#endif
