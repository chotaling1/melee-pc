#include "mnmouse.h"

#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>

#include "pc/pc.h"

bool mnMouse_GetPlanePoint(HSD_CObj* cobj, const Vec3* ref, Vec3* out)
{
    f32 mx, my;
    Vec3 guess;
    int iter;

    if (cobj == NULL || !pc_mouse_take_motion(&mx, &my)) {
        return false;
    }

    // Menu cameras are fixed, but the projection type and parameters come
    // from disc data, so invert lbVector_WorldToScreen numerically: linearize
    // around the current guess with one-unit steps along x and y and take a
    // Newton step. On a plane facing the camera one step is exact; a few more
    // absorb perspective.
    guess = *ref;
    for (iter = 0; iter < 4; iter++) {
        Vec3 p, s0, sx, sy;
        f32 ax, ay, bx, by, det, ex, ey;

        p = guess;
        lbVector_WorldToScreen(cobj, &p, &s0, 0);
        p.x += 1.0F;
        lbVector_WorldToScreen(cobj, &p, &sx, 0);
        p.x -= 1.0F;
        p.y += 1.0F;
        lbVector_WorldToScreen(cobj, &p, &sy, 0);

        ax = sx.x - s0.x;
        ay = sx.y - s0.y;
        bx = sy.x - s0.x;
        by = sy.y - s0.y;
        det = ax * by - bx * ay;
        if (det > -1e-6F && det < 1e-6F) {
            return false;
        }
        ex = mx - s0.x;
        ey = my - s0.y;
        guess.x += (ex * by - bx * ey) / det;
        guess.y += (ax * ey - ex * ay) / det;
        if (ex * ex + ey * ey < 0.01F) {
            break;
        }
        // Keep lbVector_WorldToScreen's range asserts satisfied.
        if (guess.x < -10000.0F || guess.x > 10000.0F || guess.y < -10000.0F ||
            guess.y > 10000.0F)
        {
            return false;
        }
    }
    *out = guess;
    return true;
}
