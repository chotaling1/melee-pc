#include "mnmouse.h"

#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>
#include <melee/lb/lb_00B0.h>

#include "pc/pc.h"
#include "pc/widescreen.h"

/// Widescreen draws each camera with its horizontal projection divided by
/// the widening (setupNormalCamera), but lbVector_WorldToScreen, which the
/// inversion below runs through, keeps GameCube semantics. Map the mouse's x
/// back into those: stretch it about the viewport's centre by the same
/// factor.
static void mnMouse_Unwiden(HSD_CObj* cobj, f32* mx)
{
    f32 s;
    f32 cx;

    if (HSD_CObjGetFlags(cobj) & PC_COBJ_FILL_FRAME) {
        return;
    }
    s = pc_widescreen_frame_scale();
    if (s <= 1.0F) {
        return;
    }
    cx = 0.5F * (cobj->viewport.xmin + cobj->viewport.xmax);
    *mx = cx + (*mx - cx) * s;
}

bool mnMouse_ScreenToPlane(HSD_CObj* cobj, f32 mx, f32 my, const Vec3* ref,
                           const Vec3* u, const Vec3* v, Vec3* out)
{
    Vec3 guess;
    int iter;

    if (cobj == NULL) {
        return false;
    }
    mnMouse_Unwiden(cobj, &mx);

    // Menu cameras are fixed, but the projection type and parameters come
    // from disc data, so invert lbVector_WorldToScreen numerically: linearize
    // around the current guess with one-unit steps along u and v and take a
    // Newton step. On a plane facing the camera one step is exact; a few more
    // absorb perspective.
    guess = *ref;
    for (iter = 0; iter < 4; iter++) {
        Vec3 p, s0, sx, sy;
        f32 ax, ay, bx, by, det, ex, ey, a, b;

        lbVector_WorldToScreen(cobj, &guess, &s0, 0);
        p.x = guess.x + u->x;
        p.y = guess.y + u->y;
        p.z = guess.z + u->z;
        lbVector_WorldToScreen(cobj, &p, &sx, 0);
        p.x = guess.x + v->x;
        p.y = guess.y + v->y;
        p.z = guess.z + v->z;
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
        a = (ex * by - bx * ey) / det;
        b = (ax * ey - ex * ay) / det;
        guess.x += a * u->x + b * v->x;
        guess.y += a * u->y + b * v->y;
        guess.z += a * u->z + b * v->z;
        if (ex * ex + ey * ey < 0.01F) {
            break;
        }
        // Keep lbVector_WorldToScreen's range asserts satisfied.
        if (guess.x < -10000.0F || guess.x > 10000.0F || guess.y < -10000.0F ||
            guess.y > 10000.0F || guess.z < -10000.0F || guess.z > 10000.0F)
        {
            return false;
        }
    }
    *out = guess;
    return true;
}

bool mnMouse_GetPlanePoint(HSD_CObj* cobj, const Vec3* ref, Vec3* out)
{
    static const Vec3 ux = { 1.0F, 0.0F, 0.0F };
    static const Vec3 uy = { 0.0F, 1.0F, 0.0F };
    f32 mx, my;

    if (cobj == NULL || !pc_mouse_take_motion(&mx, &my)) {
        return false;
    }
    return mnMouse_ScreenToPlane(cobj, mx, my, ref, &ux, &uy, out);
}

int mnMouse_PickRow(HSD_CObj* cobj, HSD_JObj** anchors, const bool* enabled,
                    int n, bool* clicked)
{
    static u32 last_serial;
    Vec3 screen[MN_MOUSE_MAX_ROWS];
    bool valid[MN_MOUSE_MAX_ROWS];
    f32 mx, my, min_dy = 1e9F, min_dx = 1e9F, half_h, half_w;
    f32 best_dist = 1e9F;
    int best = MN_MOUSE_NONE;
    u32 serial;
    bool moved;
    int i, j;

    *clicked = pc_mouse_take_left_click();
    serial = pc_mouse_get(&mx, &my);
    moved = serial != last_serial;
    last_serial = serial;
    if ((!moved && !*clicked) || cobj == NULL) {
        return MN_MOUSE_IDLE;
    }
    mnMouse_Unwiden(cobj, &mx);
    if (n > MN_MOUSE_MAX_ROWS) {
        n = MN_MOUSE_MAX_ROWS;
    }

    for (i = 0; i < n; i++) {
        Vec3 world;
        valid[i] = anchors[i] != NULL && (enabled == NULL || enabled[i]);
        if (!valid[i]) {
            continue;
        }
        lb_8000B1CC(anchors[i], NULL, &world);
        lbVector_WorldToScreen(cobj, &world, &screen[i], 0);
    }
    for (i = 0; i < n; i++) {
        if (!valid[i]) {
            continue;
        }
        for (j = i + 1; j < n; j++) {
            f32 dx, dy;
            if (!valid[j]) {
                continue;
            }
            dx = screen[j].x - screen[i].x;
            dy = screen[j].y - screen[i].y;
            dx = dx < 0 ? -dx : dx;
            dy = dy < 0 ? -dy : dy;
            // Rows may be staggered sideways (the main menu cascades), so
            // row spacing ignores x; only options at the same height count
            // as separate columns.
            if (dy > 4.0F && dy < min_dy) {
                min_dy = dy;
            }
            if (dy <= 4.0F && dx > 4.0F && dx < min_dx) {
                min_dx = dx;
            }
        }
    }
    half_h = min_dy < 1e9F ? 0.5F * min_dy : 24.0F;
    half_w = min_dx < 1e9F ? 0.5F * min_dx : 200.0F;

    for (i = 0; i < n; i++) {
        f32 dx, dy, dist;
        if (!valid[i]) {
            continue;
        }
        dx = mx - screen[i].x;
        dy = my - screen[i].y;
        if (dx < -half_w || dx > half_w || dy < -half_h || dy > half_h) {
            continue;
        }
        dist = dx * dx + 4.0F * dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}
