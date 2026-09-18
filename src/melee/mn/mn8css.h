/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_MN_MN8CSS_H
#define MELEE_MN_MN8CSS_H

#include <Runtime/platform.h>

#include <melee/mn/forward.h>

/// PC mod: 8-slot character select screen, used instead of the vanilla one
/// for 8-player VS. mncharsel.c's scene callbacks branch here.

/// Decide on scene entry whether this screen takes over. Regular VS with the
/// 8-player toggle on only; every other GS_CSS user keeps the vanilla screen.
bool mn8Css_Claim(CSSData* css);
/// True between a successful claim and the scene's exit.
bool mn8Css_Running(void);

void mn8Css_OnEnter(CSSData* css);
void mn8Css_OnFrame(void);
void mn8Css_OnExit(void);

#endif
