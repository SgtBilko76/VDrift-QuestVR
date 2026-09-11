/*
 * vr_events.cpp - the synthetic event queue between the VR input layer and the
 * VDrift event system. Single producer / single consumer, both on the app
 * thread (VrInputUpdate runs inside VrFrameBegin, the loop pops right after).
 */

#include <string.h>

#include "vr_events.h"

#define VR_MAX_EVENTS 256

static VrEvent sEvents[VR_MAX_EVENTS];
static int sHead = 0;   /* next to read  */
static int sTail = 0;   /* next to write */

static int sMouseX = 0;
static int sMouseY = 0;

extern "C" {

void VrQueueEvent(int type, int a, int b, int c, int d)
{
    int next = (sTail + 1) % VR_MAX_EVENTS;
    if (next == sHead) {
        return;   /* full: drop (never happens in practice, 1 frame of input) */
    }
    VrEvent& e = sEvents[sTail];
    e.type = type;
    e.a = a; e.b = b; e.c = c; e.d = d;
    sTail = next;
}

void VrQueueKey(int keycode, int down, int modifiers, int unicode)
{
    VrQueueEvent(down ? VR_EV_KEY_DOWN : VR_EV_KEY_UP, keycode, modifiers, unicode, 0);
}

void VrQueueMouseButton(int button, int down, int x, int y)
{
    sMouseX = x;
    sMouseY = y;
    VrQueueEvent(VR_EV_MOUSE_BUTTON, button, down, x, y);
}

void VrQueueMouseMotion(int x, int y, int state)
{
    sMouseX = x;
    sMouseY = y;
    VrQueueEvent(VR_EV_MOUSE_MOTION, x, y, state, 0);
}

int VrPollEvent(VrEvent* ev)
{
    if (sHead == sTail) {
        return 0;
    }
    *ev = sEvents[sHead];
    sHead = (sHead + 1) % VR_MAX_EVENTS;
    return 1;
}

void VrGetMousePos(int* x, int* y)
{
    if (x) *x = sMouseX;
    if (y) *y = sMouseY;
}

void VrSetMousePos(int x, int y)
{
    sMouseX = x;
    sMouseY = y;
}

} /* extern "C" */
