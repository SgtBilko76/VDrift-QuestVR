/*
 * vr_input.cpp - Meta Quest Touch controllers -> VDrift input.
 *
 * Driving: a virtual joystick (index 0) that VDrift's Android EventSystem reads
 * every frame (VrJoyRead). The layout is fixed and the VR controls.config in
 * templates/ binds it:
 *   AXIS0 left stick X (steer)       AXIS1 right trigger (throttle)
 *   AXIS4 left trigger (brake)       AXIS2/3/5 the other stick axes
 *   BTN0 right grip (shift up)       BTN1 left grip (shift down)
 *   BTN2 A                           BTN3 Y (short press: next camera; hold 1 s: recenter)
 *   BTN4 B                           BTN5 X (handbrake)
 *   BTN6 left stick click            BTN7 right stick click
 *   BTN8..11 left stick up/down/left/right as digital buttons (menu lists)
 *   BTN12 left menu button           BTN13/14 triggers as buttons
 *
 * Menus: the right-hand aim ray drives the mouse cursor on the floating screen,
 * the right trigger is the left mouse button, A = select, B = back (Escape),
 * the left stick sends arrow keys for lists.
 */

#include <math.h>
#include <string.h>
#include <stdint.h>

extern "C" {
#include "tbxr/VrCommon.h"
}
#undef ALOGV
#undef ALOGE
#include "vr_log.h"

#include "vdrift_vr.h"
#include "vr_internal.h"
#include "vr_events.h"
#include "vr_config.h"

/* SDL keycodes, from the header-only SDL3 compat set in ../vdrift/src/android */
#include <SDL3/SDL_keycode.h>

static float sAxes[VR_JOY_AXES];
static int sButtons = 0;

static int sMouseX = 0, sMouseY = 0;
static int sMouseDown = 0;
static bool sCursorInit = false;

static uint32_t sPrevLeftButtons = 0, sPrevRightButtons = 0;
static float sPrevRightTrigger = 0.0f;
static float sPrevLeftTrigger = 0.0f;
static int sPrevStickDir = 0;

static float sSteerExponent = 1.0f;

static float deadzone(float v, float dz)
{
    if (fabsf(v) < dz) return 0.0f;
    float s = (fabsf(v) - dz) / (1.0f - dz);
    return v < 0 ? -s : s;
}

extern "C" {

void VrInputInit(void)
{
    /* Steering response: the stick reaches full lock in a few millimetres, so a
     * gentler curve around centre helps. 1.0 is linear; VDrift's own
     * speed-sensitive steering applies on top of this. */
    float sens = VrConfigGetFloat("steer_sensitivity", 0.6f);
    if (sens < 0.2f) sens = 0.2f;
    if (sens > 1.0f) sens = 1.0f;
    sSteerExponent = 1.0f / sens;
    memset(sAxes, 0, sizeof(sAxes));
}

void VrJoyRead(int* buttons, float axes[VR_JOY_AXES])
{
    *buttons = sButtons;
    memcpy(axes, sAxes, sizeof(sAxes));
}

void VrGetMouse(int* x, int* y, int* down)
{
    if (x) *x = sMouseX;
    if (y) *y = sMouseY;
    if (down) *down = sMouseDown;
}

void VrInputUpdate(void)
{
    const ovrInputStateTrackedRemote& L = leftTrackedRemoteState_new;
    const ovrInputStateTrackedRemote& R = rightTrackedRemoteState_new;
    const bool inMenu = VrInMenu() != 0;

    /* ---- virtual joystick ---- */
    float steer = deadzone(L.Joystick.x, 0.08f);
    steer = (steer < 0 ? -1.0f : 1.0f) * powf(fabsf(steer), sSteerExponent);
    sAxes[0] = steer;
    sAxes[1] = R.IndexTrigger;
    sAxes[2] = deadzone(L.Joystick.y, 0.08f);
    sAxes[3] = deadzone(R.Joystick.x, 0.08f);
    sAxes[4] = L.IndexTrigger;
    sAxes[5] = deadzone(R.Joystick.y, 0.08f);
    sAxes[6] = R.GripTrigger;
    sAxes[7] = L.GripTrigger;

    int b = 0;
    if (R.Buttons & xrButton_GripTrigger) b |= 1 << VR_BTN_RGRIP;
    if (L.Buttons & xrButton_GripTrigger) b |= 1 << VR_BTN_LGRIP;
    if (R.Buttons & xrButton_A)           b |= 1 << VR_BTN_A;
    if (R.Buttons & xrButton_B)           b |= 1 << VR_BTN_B;
    if (L.Buttons & xrButton_X)           b |= 1 << VR_BTN_X;
    if (L.Buttons & xrButton_LThumb)      b |= 1 << VR_BTN_LTHUMB;
    if (R.Buttons & xrButton_RThumb)      b |= 1 << VR_BTN_RTHUMB;
    if (L.Buttons & xrButton_Enter)       b |= 1 << VR_BTN_MENU;
    if (R.IndexTrigger > 0.5f)            b |= 1 << VR_BTN_RTRIGGER;
    if (L.IndexTrigger > 0.5f)            b |= 1 << VR_BTN_LTRIGGER;

    double nowS = VrTimeSeconds();
    uint32_t rNew = R.Buttons & ~sPrevRightButtons;
    uint32_t rRel = ~R.Buttons & sPrevRightButtons;
    uint32_t lNew = L.Buttons & ~sPrevLeftButtons;
    uint32_t lRel = ~L.Buttons & sPrevLeftButtons;

    /* Y: short press = next camera (button 3 on release), long press (1 s) = recenter. */
    static double yDownS = 0.0;
    static bool yRecentered = false;
    static int yPulse = 0;
    if (lNew & xrButton_Y) { yDownS = nowS; yRecentered = false; }
    if ((L.Buttons & xrButton_Y) && !yRecentered && nowS - yDownS > 1.0) {
        VrRequestRecenter();
        TBXR_Vibrate(150, 1, 0.5f);
        yRecentered = true;
    }
    if ((lRel & xrButton_Y) && !yRecentered) {
        yPulse = 2;   /* hold the virtual button for two frames so the edge is seen */
    }
    if (yPulse > 0) {
        b |= 1 << VR_BTN_Y;
        yPulse--;
    }

    /* Left stick as four digital buttons with hysteresis (menu lists, and in the
     * race they can be bound to whatever - the analog axes still steer). */
    {
        int dir = sPrevStickDir;
        float ax = L.Joystick.x, ay = L.Joystick.y;
        if (dir != 0) {
            float held = (dir == 1) ? ay : (dir == -1) ? -ay : (dir == 2) ? ax : -ax;
            if (held < 0.35f) dir = 0;
        }
        if (dir == 0) {
            if (ay > 0.75f) dir = 1;
            else if (ay < -0.75f) dir = -1;
            else if (ax > 0.75f) dir = 2;
            else if (ax < -0.75f) dir = -2;
        }
        if (inMenu) {
            if (dir == 1) b |= 1 << VR_BTN_STICK_UP;
            if (dir == -1) b |= 1 << VR_BTN_STICK_DOWN;
            if (dir == 2) b |= 1 << VR_BTN_STICK_RIGHT;
            if (dir == -2) b |= 1 << VR_BTN_STICK_LEFT;
            if (dir != sPrevStickDir) {
                if (sPrevStickDir != 0) {
                    int k = sPrevStickDir == 1 ? SDLK_UP : sPrevStickDir == -1 ? SDLK_DOWN
                          : sPrevStickDir == 2 ? SDLK_RIGHT : SDLK_LEFT;
                    VrQueueKey(k, 0, 0, 0);
                }
                if (dir != 0) {
                    int k = dir == 1 ? SDLK_UP : dir == -1 ? SDLK_DOWN
                          : dir == 2 ? SDLK_RIGHT : SDLK_LEFT;
                    VrQueueKey(k, 1, 0, 0);
                }
            }
        }
        sPrevStickDir = dir;
    }
    sButtons = b;

    /* ---- pointer on the floating menu ---- */
    if (inMenu) {
        int uiW = 4, uiH = 3;
        VrGetUiSize(&uiW, &uiH);
        if (!sCursorInit) {
            sMouseX = uiW / 2;
            sMouseY = uiH / 2;
            sCursorInit = true;
        }
        const ovrTrackedController& c = rightRemoteTracking_new;
        int hx, hy;
        if (c.Active && VrMenuRayHit(&c.Pose, &hx, &hy)) {
            if (hx != sMouseX || hy != sMouseY) {
                sMouseX = hx;
                sMouseY = hy;
                VrQueueMouseMotion(sMouseX, sMouseY, sMouseDown);
            }
        }
        /* right stick moves the cursor too, as a fallback */
        float dx = sAxes[3], dy = -sAxes[5];
        if (dx != 0.0f || dy != 0.0f) {
            const float speed = uiH * 0.45f / 72.0f;
            sMouseX += (int)(dx * fabsf(dx) * speed);
            sMouseY += (int)(dy * fabsf(dy) * speed);
            if (sMouseX < 0) sMouseX = 0;
            if (sMouseY < 0) sMouseY = 0;
            if (sMouseX > uiW - 1) sMouseX = uiW - 1;
            if (sMouseY > uiH - 1) sMouseY = uiH - 1;
            VrQueueMouseMotion(sMouseX, sMouseY, sMouseDown);
        }

        bool trig = R.IndexTrigger > 0.5f;
        bool prevTrig = sPrevRightTrigger > 0.5f;
        if (trig != prevTrig) {
            sMouseDown = trig ? 1 : 0;
            VrQueueMouseButton(1, sMouseDown, sMouseX, sMouseY);
        }

        /* Left trigger goes back, like B. */
        if (L.IndexTrigger > 0.5f && sPrevLeftTrigger <= 0.5f) {
            VrQueueKey(SDLK_ESCAPE, 1, 0, 0);
        }
        if (L.IndexTrigger <= 0.5f && sPrevLeftTrigger > 0.5f) {
            VrQueueKey(SDLK_ESCAPE, 0, 0, 0);
        }
    } else {
        sMouseDown = 0;
    }
    sPrevRightTrigger = R.IndexTrigger;
    sPrevLeftTrigger = L.IndexTrigger;

    /* A = Return, B and the left menu button = Escape (pause menu in a race). */
    if (rNew & xrButton_A) VrQueueKey(SDLK_RETURN, 1, 0, 0);
    if (rRel & xrButton_A) VrQueueKey(SDLK_RETURN, 0, 0, 0);
    if (rNew & xrButton_B) VrQueueKey(SDLK_ESCAPE, 1, 0, 0);
    if (rRel & xrButton_B) VrQueueKey(SDLK_ESCAPE, 0, 0, 0);
    if (lNew & xrButton_Enter) VrQueueKey(SDLK_ESCAPE, 1, 0, 0);
    if (lRel & xrButton_Enter) VrQueueKey(SDLK_ESCAPE, 0, 0, 0);

    sPrevLeftButtons = L.Buttons;
    sPrevRightButtons = R.Buttons;
}

/* Force feedback -> rumble. The hand the wheel pulls towards gets the stronger side. */
void VrSetRumble(float force)
{
    static bool loaded = false;
    static bool enabled = true;
    static float scale = 1.0f;
    static float dz = 0.12f;
    if (!loaded) {
        loaded = true;
        enabled = VrConfigGetInt("ffb_rumble", 1) != 0;
        scale = VrConfigGetFloat("ffb_rumble_scale", 1.0f);
        dz = VrConfigGetFloat("ffb_rumble_deadzone", 0.12f);
    }
    if (!enabled) {
        return;
    }
    float mag = fabsf(force);
    if (mag > 1.0f) mag = 1.0f;
    if (mag <= dz) {
        TBXR_SetRumble(3, 0.0f);
        return;
    }
    mag = (mag - dz) / (1.0f - dz);
    float amp = powf(mag, 0.7f) * scale;
    if (amp > 1.0f) amp = 1.0f;
    const float weak = amp * 0.35f;
    if (force < 0) {
        TBXR_SetRumble(1, amp);
        TBXR_SetRumble(2, weak);
    } else {
        TBXR_SetRumble(1, weak);
        TBXR_SetRumble(2, amp);
    }
}

} /* extern "C" */
