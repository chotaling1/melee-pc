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

#endif
