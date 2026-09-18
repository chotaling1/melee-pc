/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_GM_GM8PLAYER_H
#define MELEE_GM_GM8PLAYER_H

#include <Runtime/platform.h>

#include <melee/mn/forward.h>

/// PC mod: up to four extra CPU fighters in a VS match, set up from four CPU
/// panels on the character select screen.
///
/// Player slots 0-3 are controller ports 1-4 and panel k fills slot 4 + k.
/// The slots have to be contiguous and stop at 8; see ::GM_MAX_PLAYERS.

/// Whether the 8-player toggle is on.
bool gm8Player_IsEnabled(void);

/// Called from #onEnterVs *before* #gmVsMelee_EnterVs, which copies every slot
/// out of the VsModeData.
void gm8Player_ConfigureMatch(StartMeleeData* start);

/// Reset the slots past vanilla to a valid empty state. Must run before
/// anything iterates the player array, including on the character select
/// screen, which also builds fighters.
void gm8Player_ClearExtraSlots(StartMeleeData* start);

/// Reset the slots the retail game never had, unless this match is an
/// 8-player one. Called at the top of the VS scene setup, which every mode's
/// match passes through.
void gm8Player_SanitizeSlots(StartMeleeData* start);

/// Player count the HUD should lay out for. ::StartMeleeRules::x0_3 is only
/// 3 bits wide and cannot represent 8, so HUD setup calls this instead.
int gm8Player_HudPlayerCount(int vanilla_count);

/// Number of fighters the last configured match had.
int gm8Player_ActiveCount(void);

#define GM8P_COLOR_AUTO 0xFF

/// @name CPU panels, edited from the character select screen.
/// @{
int gm8Player_PanelCount(void);
int gm8Player_PanelFocus(void);
void gm8Player_PanelFocusMove(int dir);
/// ::CharacterKind, or ChKind_None when the panel is off.
int gm8Player_PanelCkind(int k);
void gm8Player_PanelSetCkind(int k, int ckind);
int gm8Player_PanelLevel(int k);
void gm8Player_PanelLevelMove(int k, int dir);
void gm8Player_PanelSetLevel(int k, int level);
/// Costume, or GM8P_COLOR_AUTO for the first one no earlier slot wears.
int gm8Player_PanelColor(int k);
void gm8Player_PanelSetColor(int k, int color);
/// Team in Teams mode: 0 red, 1 blue, 2 green.
int gm8Player_PanelTeam(int k);
void gm8Player_PanelSetTeam(int k, int team);
void gm8Player_SetCssActive(bool active);
const char* gm8Player_CharName(int ckind);
/// @}

#endif
