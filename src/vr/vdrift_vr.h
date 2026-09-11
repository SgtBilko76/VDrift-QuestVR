#ifndef VDRIFT_VR_H
#define VDRIFT_VR_H

/*
 * vdrift_vr.h - the VR layer as seen from the game.
 *
 * Plain C interface between the OpenXR glue (src/vr) and the Android branches
 * of VDrift (../vdrift/src/android, and the __ANDROID__ blocks in game.cpp).
 *
 * Coordinate conventions: OpenXR is right-handed, +Y up, -Z forward, metres -
 * exactly the axes of an OpenGL view space. So an eye pose "relative to the
 * seated base" can be applied to VDrift's camera in view space without any
 * axis shuffling: view_eye = inverse(eyeRot) * view_camera, and the eye sits at
 * camera_pos + inverse(view_rot) * eyeOffset.
 *
 * Frame structure (Game::Draw, Android branch):
 *   VrFrameBegin()                  xrWaitFrame/xrBeginFrame, head+controller poses, input events
 *   for eye in 0,1:
 *     VrEyeBegin(eye) ... draw the 3D scene ... VrEyeEnd(eye)
 *   VrUiBegin() ... draw the 2D layers (menus / HUD) ... VrUiEnd()
 *   VrFrameEnd(uiMode)              xrEndFrame with a projection layer + a quad (or cylinder) layer
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- frame driver ---- */
void VrFrameBegin(void);
void VrEyeBegin(int eye);
void VrEyeEnd(int eye);
void VrUiBegin(void);
void VrUiEnd(void);

enum {
    VR_UI_NONE = 0,   /* no 2D layer this frame */
    VR_UI_MENU = 1,   /* floating screen in front of the player, world-anchored, pointer-driven */
    VR_UI_HUD  = 2    /* head-locked panel: the in-race HUD */
};
void VrFrameEnd(int uiMode);

/* ---- geometry ---- */
void VrGetEyeSize(int* w, int* h);
void VrGetUiSize(int* w, int* h);
/* tangents of the half angles: [0]=left (<0), [1]=right, [2]=up, [3]=down (<0) */
void VrGetEyeFov(int eye, float tan4[4]);
/* eye pose relative to the seated base, in GL view axes; quat is x,y,z,w */
void VrGetEyePose(int eye, float quat[4], float pos[3]);
/* Re-anchor the seated pose (and the menu screen) on the next frame. */
void VrRequestRecenter(void);

/* ---- input ----
 * One virtual joystick (index 0): 8 axes in -1..1 (triggers 0..1), 16 buttons.
 *   axis 0  left stick X (steer)      axis 1  right trigger (throttle)
 *   axis 2  left stick Y              axis 3  right stick X
 *   axis 4  left trigger (brake)      axis 5  right stick Y
 *   axis 6  right grip                axis 7  left grip
 *   buttons: see vr_input.cpp (VR_BTN_*) */
enum {
    VR_BTN_RGRIP = 0, VR_BTN_LGRIP, VR_BTN_A, VR_BTN_Y, VR_BTN_B, VR_BTN_X,
    VR_BTN_LTHUMB, VR_BTN_RTHUMB, VR_BTN_STICK_UP, VR_BTN_STICK_DOWN,
    VR_BTN_STICK_LEFT, VR_BTN_STICK_RIGHT, VR_BTN_MENU, VR_BTN_RTRIGGER, VR_BTN_LTRIGGER,
    VR_BTN_COUNT
};
enum { VR_JOY_AXES = 8, VR_JOY_BUTTONS = 16 };
void VrJoyRead(int* buttons, float axes[VR_JOY_AXES]);

/* Pointer on the 2D layer, in UI pixels (y down), and the left button state. */
void VrGetMouse(int* x, int* y, int* down);

/* The game tells the VR layer whether the floating menu is up (pointer + arrow
 * keys) or a race is running (the sticks drive). */
void VrSetInMenu(int inMenu);
int  VrInMenu(void);

/* Force feedback -> controller rumble, force in -1..1 */
void VrSetRumble(float force);

/* ---- misc ---- */
double VrTimeSeconds(void);
const char* VrBaseDir(void);      /* "/sdcard/VDriftVR" */
const char* VrDataDir(void);      /* "<base>/data" */
const char* VrHomeDir(void);      /* "<base>" - VDrift puts ".vdrift" under $HOME */
int  VrConfigGetIntC(const char* key, int def);
float VrConfigGetFloatC(const char* key, float def);

/* Per-frame statistics: called by the game once per rendered frame. */
void VrPerfFrame(int drawCalls);

#ifdef __cplusplus
}
#endif

#endif
