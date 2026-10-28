/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_GM_GMTEST_H
#define MELEE_GM_GMTEST_H

#include <Runtime/platform.h>

#include <melee/gm/forward.h>
#include <melee/mn/forward.h>

/// PC test driver: scripted VS matches with no controller input, for
/// automated runs (tools/wintest.sh). Off unless MELEE_TEST_MATCH is set.
///
/// Boot goes straight to VS mode; the character select screen fills in
/// MELEE_TEST_PLAYERS and starts, the stage select takes MELEE_TEST_STAGE,
/// the match runs under the test rules, the results screen is dismissed after
/// a while, and the game exits when it gets back to character select. Every
/// step is logged with a "test:" prefix, ending in "test: PASS".
///
///   MELEE_TEST_MATCH=1          enable
///   MELEE_TEST_PLAYERS=fox,marth:3,falco   1-8 CPUs (name or CharacterKind
///                               number, :N = CPU level, default 9); slots
///                               5-8 need the 8-player mod on
///   MELEE_TEST_STAGE=battlefield  name or StKind number (default Battlefield)
///   MELEE_TEST_TIME=60          time limit in seconds (default 60)
///   MELEE_TEST_STOCKS=N         stock match with N stocks instead of time
///   MELEE_TEST_ITEMS=1          leave items on (default off)
///   MELEE_TEST_RESULTS=300      frames to show results before leaving
///   MELEE_TEST_CAPTURE=match:60,match:600,results:120
///                               frames to capture, counted from entering
///                               the named scene (match, results, css, sss),
///                               so they line up run to run however long
///                               loading took

/// Whether the driver is on.
bool gmTest_IsEnabled(void);

/// Boot: the game mode to enter instead of the title or opening movie.
u8 gmTest_BootMode(u8 mode);

/// Character select, each frame. Writes the test selection into @p css and
/// returns true once when the screen should start the match. A second visit
/// (after results) ends the run.
bool gmTest_CssFrame(CSSData* css, u32 frame);

/// Stage select: the stage to force, or -1 to leave the screen alone.
int gmTest_ForcedStage(void);

/// VS setup, after #gmVsMelee_EnterVs has built the match.
void gmTest_ConfigureVs(StartMeleeData* start);

/// VS teardown: log the outcome and standings.
void gmTest_MatchEnd(const struct MatchEnd* end);

/// A scene was entered (#gm_801A4B88).
void gmTest_SceneEnter(u8 scene_kind);

/// Once per retrace, from the frame boundary.
void gmTest_Frame(void);

#endif
