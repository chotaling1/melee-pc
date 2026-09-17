/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Mouse -> menus (development convenience).
 *
 *   Left click = A     Right click = B     Wheel = D-pad up/down
 *   Moving the mouse drives the character select hand (port 1) and the stage
 *   select cursor; see pc_mouse_take_motion() users in src/melee/mn.
 *
 * Buttons merge into the same port-1 virtual pad as the keyboard. Positions are
 * kept in the game's logical 640x480 screen space, the space cobj viewports
 * and lbVector_WorldToScreen use: Aurora letterboxes the content framebuffer
 * (presentation aspect, e.g. 73:60) inside the window and logical 640x480
 * covers that whole framebuffer.
 */
#include <SDL3/SDL_timer.h>
#include <aurora/event.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/pad.h>

#include "pc/pc.h"

/* A press shorter than a couple of game frames could fall between two
 * PADRead calls, so every press is held at least this long. */
#define MIN_PRESS_MS 40
/* Wheel notches become D-pad taps: pressed for this long, then released for
 * this long, so menus with input cooldowns see each notch. */
#define WHEEL_TAP_MS 50

static SDL_Mutex* s_mutex;
static float s_x, s_y;
static bool s_moved;
static bool s_used;
static bool s_held[2];
static Uint64 s_press_until[2];
static float s_wheel_accum;
static int s_wheel_pending; /* +up / -down notches still to emit */
static int s_wheel_dir;
static Uint64 s_wheel_phase_until;
static bool s_wheel_down_phase;

extern bool pc_menu_is_open(void);

static void lock(void) {
    if (s_mutex == NULL)
        s_mutex = SDL_CreateMutex();
    if (s_mutex != NULL)
        SDL_LockMutex(s_mutex);
}

static void unlock(void) {
    if (s_mutex != NULL)
        SDL_UnlockMutex(s_mutex);
}

static bool to_logical(SDL_WindowID id, float wx, float wy, float* lx, float* ly) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    int ww, wh;
    u32 cw, ch;
    float vw, vh, left, top;

    if (window == NULL || !SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0)
        return false;
    AuroraGetRenderSize(&cw, &ch);
    if (cw == 0 || ch == 0)
        return false;
    /* Same fit as aurora's calculate_present_viewport, in window points. */
    vw = (float)ww;
    vh = vw * (float)ch / (float)cw;
    if (vh > (float)wh) {
        vh = (float)wh;
        vw = vh * (float)cw / (float)ch;
    }
    left = ((float)ww - vw) * 0.5f;
    top = ((float)wh - vh) * 0.5f;
    *lx = (wx - left) / vw * 640.0f;
    *ly = (wy - top) / vh * 480.0f;
    if (*lx < 0.0f)
        *lx = 0.0f;
    if (*lx > 640.0f)
        *lx = 640.0f;
    if (*ly < 0.0f)
        *ly = 0.0f;
    if (*ly > 480.0f)
        *ly = 480.0f;
    return true;
}

void pc_mouse_event(const SDL_Event* e) {
    int index;
    if (pc_menu_is_open())
        return;

    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        float lx, ly;
        if (!to_logical(e->motion.windowID, e->motion.x, e->motion.y, &lx, &ly))
            return;
        lock();
        s_x = lx;
        s_y = ly;
        s_moved = true;
        unlock();
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_LEFT)
            index = 0;
        else if (e->button.button == SDL_BUTTON_RIGHT)
            index = 1;
        else
            return;
        lock();
        s_used = true;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            s_held[index] = true;
            s_press_until[index] = SDL_GetTicks() + MIN_PRESS_MS;
        } else {
            s_held[index] = false;
        }
        unlock();
        return;
    case SDL_EVENT_MOUSE_WHEEL: {
        float y = e->wheel.y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            y = -y;
        lock();
        s_used = true;
        s_wheel_accum += y;
        while (s_wheel_accum >= 1.0f) {
            s_wheel_pending++;
            s_wheel_accum -= 1.0f;
        }
        while (s_wheel_accum <= -1.0f) {
            s_wheel_pending--;
            s_wheel_accum += 1.0f;
        }
        unlock();
        return;
    }
    default:
        return;
    }
}

bool pc_mouse_take_motion(float* x, float* y) {
    bool moved;
    lock();
    moved = s_moved;
    s_moved = false;
    *x = s_x;
    *y = s_y;
    unlock();
    return moved;
}

bool pc_mouse_merge(PADStatus* st) {
    static const u16 buttons[2] = {PAD_BUTTON_A, PAD_BUTTON_B};
    Uint64 now = SDL_GetTicks();
    bool used;
    int i;

    lock();
    for (i = 0; i < 2; i++) {
        if (s_held[i] || now < s_press_until[i])
            st->button |= buttons[i];
    }

    if (now >= s_wheel_phase_until) {
        if (s_wheel_down_phase) {
            /* release gap after a tap */
            s_wheel_down_phase = false;
            s_wheel_dir = 0;
            s_wheel_phase_until = now + WHEEL_TAP_MS;
        } else if (s_wheel_pending != 0) {
            s_wheel_dir = s_wheel_pending > 0 ? 1 : -1;
            s_wheel_pending -= s_wheel_dir;
            s_wheel_down_phase = true;
            s_wheel_phase_until = now + WHEEL_TAP_MS;
        }
    }
    if (s_wheel_down_phase)
        st->button |= s_wheel_dir > 0 ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;
    used = s_used;
    unlock();
    return used;
}
