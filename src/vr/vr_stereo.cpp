/*
 * vr_stereo.cpp - the frame driver: eye poses, eye/UI framebuffers, layer submission.
 *
 * OpenXR gives us, per eye, a pose in the (recentred) stage space: right-handed,
 * +Y up, -Z forward, metres - the same axes as an OpenGL view space, which is
 * how VDrift's renderer sees its camera. A seated "base" pose (position + yaw)
 * is captured on the first frame and on VrRequestRecenter(); the eye poses the
 * game gets are expressed relative to it (VrGetEyePose), so the game can compose
 * them onto whatever camera it selected: view_eye = eyeRot^-1 * view_cam.
 *
 * Frame structure (see vdrift_vr.h):
 *   VrFrameBegin -> TBXR_FrameSetup (xrWaitFrame/xrBeginFrame, poses, input)
 *   VrEyeBegin/End x2, VrUiBegin/End, VrFrameEnd -> VrSubmitFrame (xrEndFrame)
 */

#include <math.h>
#include <string.h>
#include <time.h>

extern "C" {
#include "tbxr/VrCommon.h"
}
#undef ALOGV
#undef ALOGE
#include "vr_log.h"

#include "vdrift_vr.h"
#include "vr_config.h"

/* The game's GL state manager routes "framebuffer 0" here (glcore_gles.h). */
extern "C" unsigned int glcDefaultFramebuffer;
extern "C" int glcDefaultFramebufferWidth;
extern "C" int glcDefaultFramebufferHeight;

/* per-eye pose relative to the seated base, in XR/GL view axes */
static float sEyeQuat[2][4];  /* x, y, z, w */
static float sEyePos[2][3];
static float sEyeTan[2][4];   /* tan(left) <0, tan(right), tan(up), tan(down) <0 */

static bool sHaveBase = false;
static bool sRecenterRequested = true;
static float sBaseYaw = 0.0f;       /* radians, rotation about +Y */
static float sBasePos[3] = {0, 0, 0};

static bool sEyeDrawn[2] = {false, false};
static bool sUiDrawn = false;
static bool sUiActive = false;
static int  sCurEye = -1;

static int  sInMenu = 1;

/* Floating menu screen: anchored in stage space in front of the head when
 * first shown and after a recenter, so it stays put while you point at it. */
static XrPosef sMenuPose;
static bool sMenuPoseValid = false;

/* Tunables (vr.cfg) */
static float sMenuDistance = 2.2f;   /* metres */
static float sMenuHeight = 1.7f;     /* metres */
static float sHudDistance = 1.4f;    /* metres, head-locked */
static float sHudHeight = 1.15f;     /* metres */
static float sHudDrop = 0.05f;       /* metres below eye level */

/* --------------------------------------------------------------------- math */
static void quatMulXr(const XrQuaternionf& a, const XrQuaternionf& b, XrQuaternionf& out)
{
    out.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    out.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    out.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    out.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
}

static XrVector3f rot3(const XrQuaternionf& q, const float* v)
{
    XrVector3f in = {v[0], v[1], v[2]};
    return XrQuaternionf_Rotate(q, in);
}

static float headYaw(const XrQuaternionf& q)
{
    /* yaw of the -Z forward vector projected on the XZ plane */
    float fx = -(2.0f * (q.x * q.z + q.y * q.w));
    float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    return atan2f(-fx, -fz);   /* 0 when looking down -Z */
}

static void yawRotate(float yaw, const float* v, float* out)
{
    float c = cosf(yaw), s = sinf(yaw);
    out[0] = c * v[0] + s * v[2];
    out[1] = v[1];
    out[2] = -s * v[0] + c * v[2];
}

/* ------------------------------------------------------------- eye poses */
static void computeEyePoses(void)
{
    /* Views for this frame's predicted display time (fills gAppState.Projections). */
    TBXR_updateProjections();

    const XrPosef& head = gAppState.xfStageFromHead;

    if (sRecenterRequested || !sHaveBase) {
        sBaseYaw = headYaw(head.orientation);
        sBasePos[0] = head.position.x;
        sBasePos[1] = head.position.y;
        sBasePos[2] = head.position.z;
        sHaveBase = true;
        sRecenterRequested = false;
        sMenuPoseValid = false;
        ALOGI("VR recenter: yaw=%.1f deg pos=(%.2f %.2f %.2f)", sBaseYaw * 180.0f / (float)M_PI,
              sBasePos[0], sBasePos[1], sBasePos[2]);
    }

    /* rotation about +Y by -baseYaw, as a quaternion */
    XrQuaternionf unyaw;
    unyaw.x = 0.0f;
    unyaw.y = sinf(-sBaseYaw * 0.5f);
    unyaw.z = 0.0f;
    unyaw.w = cosf(-sBaseYaw * 0.5f);

    for (int eye = 0; eye < 2; eye++) {
        XrPosef stageFromEye = XrPosef_Multiply(head, gAppState.Projections[eye].pose);

        float rel[3] = {stageFromEye.position.x - sBasePos[0],
                        stageFromEye.position.y - sBasePos[1],
                        stageFromEye.position.z - sBasePos[2]};
        yawRotate(-sBaseYaw, rel, sEyePos[eye]);

        XrQuaternionf q;
        quatMulXr(unyaw, stageFromEye.orientation, q);
        sEyeQuat[eye][0] = q.x;
        sEyeQuat[eye][1] = q.y;
        sEyeQuat[eye][2] = q.z;
        sEyeQuat[eye][3] = q.w;

        const XrFovf& f = gAppState.Projections[eye].fov;
        sEyeTan[eye][0] = tanf(f.angleLeft);
        sEyeTan[eye][1] = tanf(f.angleRight);
        sEyeTan[eye][2] = tanf(f.angleUp);
        sEyeTan[eye][3] = tanf(f.angleDown);
    }
}

/* ------------------------------------------------------------- UI layer */
static void menuQuadPose(XrPosef* pose)
{
    if (!sMenuPoseValid) {
        const float yaw = headYaw(gAppState.xfStageFromHead.orientation);
        const float d = sMenuDistance;
        const XrVector3f hp = gAppState.xfStageFromHead.position;
        sMenuPose.position.x = hp.x - sinf(yaw) * d;
        sMenuPose.position.y = hp.y - 0.10f;
        sMenuPose.position.z = hp.z - cosf(yaw) * d;
        sMenuPose.orientation.x = 0.0f;
        sMenuPose.orientation.y = sinf(yaw * 0.5f);
        sMenuPose.orientation.z = 0.0f;
        sMenuPose.orientation.w = cosf(yaw * 0.5f);
        sMenuPoseValid = true;
        ALOGI("Menu screen anchored at (%.2f %.2f %.2f) yaw %.1f deg", sMenuPose.position.x,
              sMenuPose.position.y, sMenuPose.position.z, yaw * 180.0f / (float)M_PI);
    }
    *pose = sMenuPose;
}

static void uiSizeMetres(int mode, float* sx, float* sy)
{
    int w = 4, h = 3;
    VrGetUiSize(&w, &h);
    const float aspect = (float)w / (float)h;
    if (mode == VR_UI_HUD) {
        *sy = sHudHeight;
    } else {
        *sy = sMenuHeight;
    }
    *sx = *sy * aspect;
}

extern "C" {

void VrStereoLoadConfig(void)
{
    sMenuDistance = VrConfigGetFloat("screen_distance", 2.2f);
    sMenuHeight = VrConfigGetFloat("screen_height", 1.7f);
    sHudDistance = VrConfigGetFloat("hud_distance", 1.4f);
    sHudHeight = VrConfigGetFloat("hud_height", 1.15f);
    sHudDrop = VrConfigGetFloat("hud_drop", 0.05f);
}

/* Aim-ray hit test against the menu screen, used by vr_input.cpp. Returns 1 and
 * the hit in UI pixels (y down) when the ray from the controller meets the panel. */
int VrMenuRayHit(const XrPosef* aim, int* px, int* py)
{
    if (!sMenuPoseValid) {
        return 0;
    }
    float sx, sy;
    uiSizeMetres(VR_UI_MENU, &sx, &sy);

    XrPosef quad = sMenuPose;
    const float fwd[3] = {0, 0, -1};
    XrVector3f D = rot3(aim->orientation, fwd);
    int uiW = 4, uiH = 3;
    VrGetUiSize(&uiW, &uiH);

    float u, v;
    XrPosef cyl;
    float radius = 0.0f, angle = 0.0f;
    if (TBXR_HasCylinderLayer) {
        /* Curved screen: intersect with the cylinder around the viewer, or the
         * cursor drifts from the ray towards the edges. */
        const float zAxis[3] = {0, 0, 1};
        XrVector3f back = rot3(quad.orientation, zAxis);
        radius = sMenuDistance;
        cyl.orientation = quad.orientation;
        cyl.position.x = quad.position.x + back.x * radius;
        cyl.position.y = quad.position.y + back.y * radius;
        cyl.position.z = quad.position.z + back.z * radius;
        angle = sx / radius;

        const float up[3] = {0, 1, 0}, right[3] = {1, 0, 0};
        XrVector3f Y = rot3(cyl.orientation, up);
        XrVector3f X = rot3(cyl.orientation, right);
        XrVector3f F = {-back.x, -back.y, -back.z};
        const float O[3] = {aim->position.x - cyl.position.x, aim->position.y - cyl.position.y,
                            aim->position.z - cyl.position.z};
        const float oy = O[0] * Y.x + O[1] * Y.y + O[2] * Y.z;
        const float dy = D.x * Y.x + D.y * Y.y + D.z * Y.z;
        const float ox = O[0] * X.x + O[1] * X.y + O[2] * X.z;
        const float oz = O[0] * F.x + O[1] * F.y + O[2] * F.z;
        const float dx = D.x * X.x + D.y * X.y + D.z * X.z;
        const float dz = D.x * F.x + D.y * F.y + D.z * F.z;
        const float a = dx * dx + dz * dz;
        if (a < 1e-8f) return 0;
        const float b = 2.0f * (ox * dx + oz * dz);
        const float cq = ox * ox + oz * oz - radius * radius;
        const float disc = b * b - 4.0f * a * cq;
        if (disc < 0.0f) return 0;
        const float t = (-b + sqrtf(disc)) / (2.0f * a);
        if (t <= 0.0f) return 0;
        const float hx = ox + t * dx, hz = oz + t * dz, hy = oy + t * dy;
        if (hz <= 0.0f) return 0;
        u = 0.5f + atan2f(hx, hz) / angle;
        v = 0.5f + hy / sy;
    } else {
        const float zAxis[3] = {0, 0, 1}, xAxis[3] = {1, 0, 0}, yAxis[3] = {0, 1, 0};
        XrVector3f N = rot3(quad.orientation, zAxis);
        XrVector3f X = rot3(quad.orientation, xAxis);
        XrVector3f Y = rot3(quad.orientation, yAxis);
        const float O[3] = {aim->position.x, aim->position.y, aim->position.z};
        const float C[3] = {quad.position.x, quad.position.y, quad.position.z};
        const float denom = D.x * N.x + D.y * N.y + D.z * N.z;
        if (denom > -1e-4f) return 0;
        const float CO[3] = {C[0] - O[0], C[1] - O[1], C[2] - O[2]};
        const float t = (CO[0] * N.x + CO[1] * N.y + CO[2] * N.z) / denom;
        if (t <= 0.0f) return 0;
        const float P[3] = {O[0] + t * D.x - C[0], O[1] + t * D.y - C[1], O[2] + t * D.z - C[2]};
        u = (P[0] * X.x + P[1] * X.y + P[2] * X.z) / sx + 0.5f;
        v = (P[0] * Y.x + P[1] * Y.y + P[2] * Y.z) / sy + 0.5f;
    }
    if (u < -0.05f || u > 1.05f || v < -0.05f || v > 1.05f) {
        return 0;
    }
    if (u < 0) u = 0; if (u > 1) u = 1;
    if (v < 0) v = 0; if (v > 1) v = 1;
    *px = (int)(u * uiW);
    *py = (int)((1.0f - v) * uiH);
    return 1;
}

void VrRequestRecenter(void)
{
    sRecenterRequested = true;
    sMenuPoseValid = false;
}

void VrSetInMenu(int inMenu)
{
    sInMenu = inMenu ? 1 : 0;
}

int VrInMenu(void)
{
    return sInMenu;
}

void VrGetEyeSize(int* w, int* h)
{
    *w = (int)gAppState.Width;
    *h = (int)gAppState.Height;
}

void VrGetEyeFov(int eye, float tan4[4])
{
    memcpy(tan4, sEyeTan[eye & 1], sizeof(float) * 4);
}

void VrGetEyePose(int eye, float quat[4], float pos[3])
{
    memcpy(quat, sEyeQuat[eye & 1], sizeof(float) * 4);
    memcpy(pos, sEyePos[eye & 1], sizeof(float) * 3);
}

/* ----------------------------------------------------------- frame driver */
void VrFrameBegin(void)
{
    if (gAppState.FrameSetup) {
        return;
    }
    TBXR_FrameSetup();
    computeEyePoses();
    sEyeDrawn[0] = sEyeDrawn[1] = false;
    sUiDrawn = false;
}

static void setTarget(ovrFramebuffer* fb)
{
    glcDefaultFramebuffer = fb->FrameBuffers[fb->TextureSwapChainIndex];
    glcDefaultFramebufferWidth = fb->Width;
    glcDefaultFramebufferHeight = fb->Height;
    glBindFramebuffer(GL_FRAMEBUFFER, glcDefaultFramebuffer);
    glViewport(0, 0, fb->Width, fb->Height);
}

void VrEyeBegin(int eye)
{
    VrFrameBegin();
    sCurEye = eye & 1;
    TBXR_prepareEyeBuffer(sCurEye);
    setTarget(&gAppState.Renderer.FrameBuffer[sCurEye]);
}

void VrEyeEnd(int eye)
{
    TBXR_finishEyeBuffer(eye & 1);
    sEyeDrawn[eye & 1] = true;
    sCurEye = -1;
    glcDefaultFramebuffer = 0;
}

void VrUiBegin(void)
{
    VrFrameBegin();
    ovrFramebuffer* fb = &gAppState.Renderer.UiBuffer;
    ovrFramebuffer_Acquire(fb);
    ovrFramebuffer_SetCurrent(fb);
    setTarget(fb);
    /* Transparent where nothing is drawn: the layer is alpha-blended over the scene. */
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    sUiActive = true;
}

void VrUiEnd(void)
{
    if (!sUiActive) {
        return;
    }
    ovrFramebuffer* fb = &gAppState.Renderer.UiBuffer;
    const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);
    ovrFramebuffer_Release(fb);
    ovrFramebuffer_SetNone();
    sUiActive = false;
    sUiDrawn = true;
    glcDefaultFramebuffer = 0;
}

static void VrSubmitFrame(int uiMode)
{
    if (gAppState.SessionActive == GL_FALSE) {
        return;
    }

    gAppState.LayerCount = 0;
    memset(gAppState.Layers, 0, sizeof(xrCompositorLayer_Union) * ovrMaxLayerCount);

    static XrCompositionLayerProjectionView projection_layer_elements[2];
    if (sEyeDrawn[0] && sEyeDrawn[1]) {
        XrCompositionLayerProjection projection_layer = {};
        projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projection_layer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
        projection_layer.space = gAppState.CurrentSpace;
        projection_layer.viewCount = ovrMaxNumEyes;
        projection_layer.views = projection_layer_elements;

        for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
            ovrFramebuffer* frameBuffer = &gAppState.Renderer.FrameBuffer[eye];
            memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
            projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection_layer_elements[eye].pose = XrPosef_Multiply(gAppState.xfStageFromHead, gAppState.Projections[eye].pose);
            projection_layer_elements[eye].fov = gAppState.Projections[eye].fov;
            projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
            projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
            projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
            projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
            projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
            projection_layer_elements[eye].subImage.imageArrayIndex = 0;
        }
        gAppState.Layers[gAppState.LayerCount++].Projection = projection_layer;
    }

    if (sUiDrawn && uiMode != VR_UI_NONE) {
        ovrFramebuffer* fb = &gAppState.Renderer.UiBuffer;
        float sx, sy;
        uiSizeMetres(uiMode, &sx, &sy);

        XrCompositionLayerQuad quad = {};
        quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = fb->ColorSwapChain.Handle;
        quad.subImage.imageRect.offset.x = 0;
        quad.subImage.imageRect.offset.y = 0;
        quad.subImage.imageRect.extent.width = fb->ColorSwapChain.Width;
        quad.subImage.imageRect.extent.height = fb->ColorSwapChain.Height;
        quad.subImage.imageArrayIndex = 0;
        quad.size.width = sx;
        quad.size.height = sy;

        if (uiMode == VR_UI_HUD) {
            /* Head-locked: a pose in VIEW space is relative to the head. */
            quad.space = gAppState.HeadSpace;
            quad.pose.orientation.w = 1.0f;
            quad.pose.position.x = 0.0f;
            quad.pose.position.y = -sHudDrop;
            quad.pose.position.z = -sHudDistance;
            gAppState.Layers[gAppState.LayerCount++].Quad = quad;
        } else {
            quad.space = gAppState.CurrentSpace;
            menuQuadPose(&quad.pose);
            if (TBXR_HasCylinderLayer) {
                /* Same image on a cylinder around the viewer: every part of a wide
                 * menu is then the same distance away and square-on. */
                const float r = sMenuDistance;
                const float zAxis[3] = {0, 0, 1};
                XrVector3f back = rot3(quad.pose.orientation, zAxis);
                XrCompositionLayerCylinderKHR cyl = {};
                cyl.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
                cyl.layerFlags = quad.layerFlags;
                cyl.space = quad.space;
                cyl.eyeVisibility = quad.eyeVisibility;
                cyl.subImage = quad.subImage;
                cyl.pose = quad.pose;
                cyl.pose.position.x += back.x * r;
                cyl.pose.position.y += back.y * r;
                cyl.pose.position.z += back.z * r;
                cyl.radius = r;
                cyl.centralAngle = sx / r;
                cyl.aspectRatio = sx / sy;
                gAppState.Layers[gAppState.LayerCount++].Cylinder = cyl;
            } else {
                gAppState.Layers[gAppState.LayerCount++].Quad = quad;
            }
        }
    }

    const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {};
    for (int i = 0; i < gAppState.LayerCount; i++) {
        layers[i] = (const XrCompositionLayerBaseHeader*)&gAppState.Layers[i];
    }

    XrFrameEndInfo endFrameInfo = {};
    endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
    endFrameInfo.displayTime = gAppState.FrameState.predictedDisplayTime;
    endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endFrameInfo.layerCount = gAppState.LayerCount;
    endFrameInfo.layers = layers;
    OXR(xrEndFrame(gAppState.Session, &endFrameInfo));

    gAppState.FrameSetup = false;
}

void VrFrameEnd(int uiMode)
{
    if (sCurEye >= 0) {
        VrEyeEnd(sCurEye);
    }
    if (sUiActive) {
        VrUiEnd();
    }
    if (!gAppState.FrameSetup) {
        return;   /* nothing was begun */
    }
    if (!(sEyeDrawn[0] && sEyeDrawn[1])) {
        /* Keep the compositor fed: submit the eye buffers as they are rather
         * than an unrendered swapchain image. */
        for (int eye = 0; eye < 2; eye++) {
            if (!sEyeDrawn[eye]) {
                TBXR_prepareEyeBuffer(eye);
                TBXR_finishEyeBuffer(eye);
                sEyeDrawn[eye] = true;
            }
        }
    }
    VrSubmitFrame(uiMode);
    sEyeDrawn[0] = sEyeDrawn[1] = false;
    sUiDrawn = false;
}

double VrTimeSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

} /* extern "C" */
