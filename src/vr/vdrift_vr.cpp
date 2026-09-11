/*
 * vdrift_vr.cpp - Android/OpenXR entry point for VDrift VR.
 *
 * JNI glue (activity + surface lifecycle), the app thread that owns the OpenXR
 * session and the GL context, and the VR_* callbacks the Team Beef framework
 * (src/vr/tbxr) expects the game to provide.
 *
 * VDrift's own main() is compiled into this library as VdriftMain() and called
 * from the app thread once the OpenXR session is active. VDrift finds its data
 * and settings through the environment (VDRIFT_DATA_DIRECTORY, HOME).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

extern "C" {
#include "tbxr/VrCommon.h"
}
#undef ALOGV
#undef ALOGE
#include "vr_log.h"

#include "vr_paths.h"
#include "vr_config.h"
#include "vr_internal.h"
#include "vdrift_vr.h"

/* VDrift's main(), renamed at compile time (see CMakeLists.txt). */
extern int VdriftMain(int argc, char* argv[]);

/* ------------------------------------------------------------------------- */
/* Globals the TBXR framework expects the game to provide                    */
/* ------------------------------------------------------------------------- */
extern "C" {
ovrInputStateTrackedRemote leftTrackedRemoteState_old;
ovrInputStateTrackedRemote leftTrackedRemoteState_new;
ovrTrackedController leftRemoteTracking_new;
ovrInputStateTrackedRemote rightTrackedRemoteState_old;
ovrInputStateTrackedRemote rightTrackedRemoteState_new;
ovrTrackedController rightRemoteTracking_new;

float playerYaw = 0.0f;
vec3_t hmdorientation = {0, 0, 0};
}

static char gBaseDir[1024] = "/sdcard/VDriftVR";
static float gHmdPosition[3] = {0, 0, 0};
static bool gPlayerYawSet = false;
static int gUiWidth = 1600;
static int gUiHeight = 1200;

/* TBXR_Common.c tunables */
extern "C" float SS_MULTIPLIER;
extern "C" int REFRESH;
extern "C" int NUM_MULTI_SAMPLES;

/* ------------------------------------------------------------------------- */
/* VR_* callbacks (game side of the TBXR contract)                           */
/* ------------------------------------------------------------------------- */
extern "C" {

void VR_FrameSetup() {}

void VR_GetUiSize(int* w, int* h)
{
    *w = gUiWidth;
    *h = gUiHeight;
}

void VrGetUiSize(int* w, int* h)
{
    *w = gUiWidth;
    *h = gUiHeight;
}

int VrConfigGetIntC(const char* key, int def) { return VrConfigGetInt(key, def); }
float VrConfigGetFloatC(const char* key, float def) { return VrConfigGetFloat(key, def); }

bool VR_GetVRProjection(int eye, float zNear, float zFar, float* projection)
{
    XrMatrix4x4f_CreateProjectionFov(&gAppState.ProjectionMatrices[eye], GRAPHICS_OPENGL_ES,
                                     gAppState.Projections[eye].fov, zNear, zFar);
    memcpy(projection, gAppState.ProjectionMatrices[eye].m, 16 * sizeof(float));
    return true;
}

void VR_HandleControllerInput()
{
    TBXR_UpdateControllers();
    VrInputUpdate();
}

void VR_SetHMDOrientation(float pitch, float yaw, float roll)
{
    hmdorientation[0] = pitch;
    hmdorientation[1] = yaw;
    hmdorientation[2] = roll;
    if (!gPlayerYawSet) {
        playerYaw = yaw;
        gPlayerYawSet = true;
    }
}

void VR_SetHMDPosition(float x, float y, float z)
{
    gHmdPosition[0] = x;
    gHmdPosition[1] = y;
    gHmdPosition[2] = z;
}

void VR_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight) {}
void VR_HapticUpdateEvent(const char* event, int intensity, float angle) {}
void VR_HapticEndFrame() {}
void VR_HapticStopEvent(const char* event) {}
void VR_HapticEnable() {}
void VR_HapticDisable() {}

} /* extern "C" */

/* ------------------------------------------------------------------------- */
/* JNI callback into the activity                                            */
/* ------------------------------------------------------------------------- */
static JavaVM* jVM = NULL;
static jobject jniCallbackObj = 0;
static jmethodID android_shutdown = 0;

static void jni_shutdown()
{
    ALOGV("Calling: jni_shutdown");
    if (!jVM || !jniCallbackObj || !android_shutdown) {
        return;
    }
    JNIEnv* env = NULL;
    if (jVM->GetEnv((void**)&env, JNI_VERSION_1_4) < 0) {
        jVM->AttachCurrentThread(&env, NULL);
    }
    env->CallVoidMethod(jniCallbackObj, android_shutdown);
}

extern "C" void VR_Shutdown()
{
    jni_shutdown();
}

/* ------------------------------------------------------------------------- */
/* App thread                                                                */
/* ------------------------------------------------------------------------- */
extern "C" void* AppThreadFunction(void* parm)
{
    gAppThread = (ovrAppThread*)parm;

    java.Vm = gAppThread->JavaVm;
    java.Vm->AttachCurrentThread(&java.Env, NULL);
    java.ActivityObject = gAppThread->ActivityObject;

    // AttachCurrentThread resets the thread name.
    prctl(PR_SET_NAME, (long)"VDriftVR_App", 0, 0, 0);

    gAppState.MainThreadTid = gettid();

    VrLogRedirectStdio();

    // User tunables (refresh rate, eye buffer scale, screen sizes) from <base>/vr.cfg
    {
        char cfg[1024];
        snprintf(cfg, sizeof(cfg), "%s/vr.cfg", gBaseDir);
        VrConfigLoad(cfg);
        REFRESH = VrConfigGetInt("refresh", 72);
        SS_MULTIPLIER = VrConfigGetFloat("supersampling", 1.2f);
        if (SS_MULTIPLIER < 0.3f) SS_MULTIPLIER = 0.3f;
        if (SS_MULTIPLIER > 2.0f) SS_MULTIPLIER = 2.0f;

        /* Multisampling for the eye buffers, resolved in tile memory
         * (GL_EXT_multisampled_render_to_texture). 1 disables it. */
        NUM_MULTI_SAMPLES = VrConfigGetInt("msaa", 4);
        if (NUM_MULTI_SAMPLES < 1) NUM_MULTI_SAMPLES = 1;
        if (NUM_MULTI_SAMPLES > 8) NUM_MULTI_SAMPLES = 8;

        /* The 2D layer (menus, HUD), 4:3 like VDrift's GUI layouts assume. */
        gUiHeight = VrConfigGetInt("ui_height", 1200);
        if (gUiHeight < 480) gUiHeight = 480;
        if (gUiHeight > 2048) gUiHeight = 2048;
        gUiWidth = gUiHeight * 4 / 3;

        VrStereoLoadConfig();
        VrInputInit();
        VrPerfInit();

        ALOGI("VR settings: refresh=%d Hz supersampling=%.2f msaa=%dx ui=%dx%d",
              REFRESH, SS_MULTIPLIER, NUM_MULTI_SAMPLES, gUiWidth, gUiHeight);
    }

    TBXR_InitialiseOpenXR();
    TBXR_EnterVR();          // EGL context is current from here on
    TBXR_InitRenderer();
    TBXR_InitActions();

    ALOGI("OpenXR initialised; eye buffer %d x %d", (int)gAppState.Width, (int)gAppState.Height);

    if (VrPathsInit(gBaseDir) != 0) {
        ALOGE("Data directory not usable; push the game data to %s/data", gBaseDir);
    }

    TBXR_WaitForSessionActive();
    ALOGI("OpenXR session active");

    // VDrift's main().
    {
        static char arg0[] = "vdrift";
        char* argv[8] = {arg0, NULL};
        int argc = 1;
        static char optBench[] = "-benchmark";
        if (VrConfigGetInt("benchmark", 0)) {
            argv[argc++] = optBench;
            ALOGI("Benchmark mode: starting a race directly");
        }
        ALOGI("Starting VDrift: data=%s home=%s", VrDataDir(), VrHomeDir());
        VdriftMain(argc, argv);
    }
    ALOGI("Main loop finished, shutting down");

    TBXR_LeaveVR();
    VR_Shutdown();
    exit(0);
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* JNI entry points                                                          */
/* ------------------------------------------------------------------------- */
extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env;
    jVM = vm;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_4) != JNI_OK) {
        ALOGE("Failed JNI_OnLoad");
        return -1;
    }
    return JNI_VERSION_1_4;
}

JNIEXPORT jlong JNICALL Java_com_vdriftvr_VDriftVRLib_onCreate(JNIEnv* env, jclass activityClass,
                                                              jobject activity, jstring dataDir)
{
    ALOGV("    VDriftVRLib::onCreate()");

    const char* dir = env->GetStringUTFChars(dataDir, NULL);
    if (dir && dir[0]) {
        snprintf(gBaseDir, sizeof(gBaseDir), "%s", dir);
    }
    env->ReleaseStringUTFChars(dataDir, dir);
    ALOGI("Base dir: %s", gBaseDir);
    setenv("VDVR_BASE_DIR", gBaseDir, 1);

    ovrAppThread* appThread = (ovrAppThread*)malloc(sizeof(ovrAppThread));
    ovrAppThread_Create(appThread, env, activity, activityClass);

    surfaceMessageQueue_Enable(&appThread->MessageQueue, true);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_CREATE, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);

    return (jlong)((size_t)appThread);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onStart(JNIEnv* env, jobject obj, jlong handle, jobject obj1)
{
    ALOGV("    VDriftVRLib::onStart()");

    jniCallbackObj = (jobject)env->NewGlobalRef(obj1);
    jclass callbackClass = env->GetObjectClass(jniCallbackObj);
    android_shutdown = env->GetMethodID(callbackClass, "shutdown", "()V");

    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_START, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onResume(JNIEnv* env, jobject obj, jlong handle)
{
    ALOGV("    VDriftVRLib::onResume()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_RESUME, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onPause(JNIEnv* env, jobject obj, jlong handle)
{
    ALOGV("    VDriftVRLib::onPause()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_PAUSE, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onStop(JNIEnv* env, jobject obj, jlong handle)
{
    ALOGV("    VDriftVRLib::onStop()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_STOP, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onDestroy(JNIEnv* env, jobject obj, jlong handle)
{
    ALOGV("    VDriftVRLib::onDestroy()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_DESTROY, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
    surfaceMessageQueue_Enable(&appThread->MessageQueue, false);

    ovrAppThread_Destroy(appThread, env);
    free(appThread);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onSurfaceCreated(JNIEnv* env, jobject obj, jlong handle, jobject surface)
{
    ALOGV("    VDriftVRLib::onSurfaceCreated()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);

    ANativeWindow* newNativeWindow = ANativeWindow_fromSurface(env, surface);
    if (ANativeWindow_getWidth(newNativeWindow) < ANativeWindow_getHeight(newNativeWindow)) {
        ALOGE("        Surface not in landscape mode!");
    }

    appThread->NativeWindow = newNativeWindow;
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED);
    surfaceMessage_SetPointerParm(&message, 0, appThread->NativeWindow);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onSurfaceChanged(JNIEnv* env, jobject obj, jlong handle, jobject surface)
{
    ALOGV("    VDriftVRLib::onSurfaceChanged()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);

    ANativeWindow* newNativeWindow = ANativeWindow_fromSurface(env, surface);
    if (ANativeWindow_getWidth(newNativeWindow) < ANativeWindow_getHeight(newNativeWindow)) {
        ALOGE("        Surface not in landscape mode!");
    }

    if (newNativeWindow != appThread->NativeWindow) {
        if (appThread->NativeWindow != NULL) {
            srufaceMessage message;
            surfaceMessage_Init(&message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED);
            surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
            ANativeWindow_release(appThread->NativeWindow);
            appThread->NativeWindow = NULL;
        }
        if (newNativeWindow != NULL) {
            appThread->NativeWindow = newNativeWindow;
            srufaceMessage message;
            surfaceMessage_Init(&message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED);
            surfaceMessage_SetPointerParm(&message, 0, appThread->NativeWindow);
            surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
        }
    } else if (newNativeWindow != NULL) {
        ANativeWindow_release(newNativeWindow);
    }
}

JNIEXPORT void JNICALL Java_com_vdriftvr_VDriftVRLib_onSurfaceDestroyed(JNIEnv* env, jobject obj, jlong handle)
{
    ALOGV("    VDriftVRLib::onSurfaceDestroyed()");
    ovrAppThread* appThread = (ovrAppThread*)((size_t)handle);
    srufaceMessage message;
    surfaceMessage_Init(&message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED);
    surfaceMessageQueue_PostMessage(&appThread->MessageQueue, &message);
    if (appThread->NativeWindow) {
        ANativeWindow_release(appThread->NativeWindow);
    }
    appThread->NativeWindow = NULL;
}

} /* extern "C" */
