#ifndef MELEE_GM_SOCCER_H
#define MELEE_GM_SOCCER_H

/* Soccer mod (PC only): VS matches on Final Destination with a hit-only ball
 * built on Mr. Saturn's article data, a goal past each ledge, first to
 * GM_SOCCER_GOALS_TO_WIN. Enabled from the F1 menu's Cheats page. */

#include <Runtime/platform.h>

#include <melee/mn/forward.h>

#define GM_SOCCER_GOALS_TO_WIN 3

/// Called when the regular VS mode sets up a match; installs the soccer
/// match-start hook if the mod is enabled and the stage is Final Destination.
void gmSoccer_ConfigureMatch(StartMeleeData* start);

bool pc_soccer_is_active(void);
void pc_soccer_get_score(int* left, int* right);

#endif
