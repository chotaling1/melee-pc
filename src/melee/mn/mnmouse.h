#ifndef MELEE_MN_MOUSE_H
#define MELEE_MN_MOUSE_H

/* PC only: mouse-driven menu cursors (development convenience). The mouse
 * position comes from src/pc/mouse.c in logical 640x480 screen space. */

#include <Runtime/platform.h>

#include <dolphin/mtx.h>
#include <sysdolphin/baselib/forward.h>

/// If the mouse moved since the last call, stores in @p out the point on the
/// plane z = @p ref->z that @p cobj shows under the mouse and returns true.
bool mnMouse_GetPlanePoint(HSD_CObj* cobj, const Vec3* ref, Vec3* out);

#define MN_MOUSE_MAX_ROWS 40
#define MN_MOUSE_IDLE (-2) ///< mouse neither moved nor clicked this frame
#define MN_MOUSE_NONE (-1) ///< mouse active but over no row

/// List menu hit test. @p anchors are the option JObjs (NULL or !enabled
/// rows are skipped). Rows are matched in screen space around each anchor:
/// half the spacing to the nearest neighbour vertically, and horizontally
/// half the column spacing (or a wide band for single-column lists).
/// Returns the row index, MN_MOUSE_NONE or MN_MOUSE_IDLE; @p clicked is set
/// when a mouse left click arrived this frame.
int mnMouse_PickRow(HSD_CObj* cobj, HSD_JObj** anchors, const bool* enabled,
                    int n, bool* clicked);

/// Main menu engine hook, called by mn_80229624 for the all-ports reader:
/// moves the hovered option under the mouse and adjusts the returned
/// MenuInput bits for mouse clicks. Defined in mnmain.c.
u32 mnMouse_FilterMenuInput(u32 buttons);

/* Per-screen row providers: fill anchors/enabled (MN_MOUSE_MAX_ROWS slots),
 * return the row count (0 = not ready); hover moves the selection to a row;
 * value rows turn a click into Right instead of A. */
int mnMainRule_MouseRows(HSD_JObj** anchors, bool* enabled);
void mnMainRule_MouseHover(int row);
int mnRulePlus_MouseRows(HSD_JObj** anchors, bool* enabled);
void mnRulePlus_MouseHover(int row);
int mnItemSw_MouseRows(HSD_JObj** anchors, bool* enabled);
void mnItemSw_MouseHover(int row);
int mnStageSw_MouseRows(HSD_JObj** anchors, bool* enabled);
void mnStageSw_MouseHover(int row);

#ifdef MELEE_PC
#define MN_MENU_INPUT() mnMouse_FilterMenuInput(mn_80229624(4))
#else
#define MN_MENU_INPUT() mn_80229624(4)
#endif

#endif
