#include "mnmouse.h"

#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>
#include <melee/lb/lb_00B0.h>

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
