#ifndef VR_EVENTS_H
#define VR_EVENTS_H

/*
 * Synthetic input events produced by the VR layer and consumed by the patched
 * EventSystem (src/android/eventsystem_android.cpp).
 *
 * On the Quest there is no SDL window, so SDL_PollEvent() never returns anything.
 * vr_input.cpp translates the Touch controllers into these events instead; the
 * event loop pops them and feeds injectKeyboardEvent / injectMouseButtonEvent /
 * injectMouseMotionEvent exactly as the SDL path would.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum VrEventType {
    VR_EV_KEY_DOWN = 0,   /* a = SDL keycode, b = modifiers, c = unicode */
    VR_EV_KEY_UP,         /* a = SDL keycode, b = modifiers              */
    VR_EV_MOUSE_BUTTON,   /* a = button (1=left), b = 1 down / 0 up, c = x, d = y */
    VR_EV_MOUSE_MOTION,   /* a = x, b = y, c = button state              */
    VR_EV_QUIT
};

typedef struct {
    int type;
    int a, b, c, d;
} VrEvent;

/* Producer side (vr_input.cpp / vr_keyboard.cpp). */
void VrQueueEvent(int type, int a, int b, int c, int d);
void VrQueueKey(int keycode, int down, int modifiers, int unicode);
void VrQueueMouseButton(int button, int down, int x, int y);
void VrQueueMouseMotion(int x, int y, int state);

/* Consumer side (eventsystem_android.cpp). Returns 1 and fills *ev, or 0 when empty. */
int VrPollEvent(VrEvent* ev);

/* Current VR mouse cursor position, in window pixels, y down (the event system uses it
 * to place the cursor without SDL_GetMouseState). */
void VrGetMousePos(int* x, int* y);
void VrSetMousePos(int x, int y);

#ifdef __cplusplus
}
#endif

#endif
