/*
 * VrCommon.h - minimal replacement for QuakeQuest's VrCommon.h.
 * Provides the few types/globals the Team Beef OpenXR framework (TBXR_Common.c,
 * OpenXrInput.c) expects, without any Quake engine dependencies.
 */
#if !defined(vrcommon_h)
#define vrcommon_h

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <android/log.h>

typedef float vec3_t[3];

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef DEG2RAD
#define DEG2RAD(a) ((a) * (float)(M_PI / 180.0))
#endif
#ifndef RAD2DEG
#define RAD2DEG(a) ((a) * (float)(180.0 / M_PI))
#endif
#ifndef EPSILON
#define EPSILON 0.001f
#endif

#include "TBXR_Common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ovrInputStateTrackedRemote leftTrackedRemoteState_old;
extern ovrInputStateTrackedRemote leftTrackedRemoteState_new;
extern ovrTrackedController leftRemoteTracking_new;
extern ovrInputStateTrackedRemote rightTrackedRemoteState_old;
extern ovrInputStateTrackedRemote rightTrackedRemoteState_new;
extern ovrTrackedController rightRemoteTracking_new;

extern float playerYaw;
extern vec3_t hmdorientation;

#ifdef __cplusplus
}
#endif

#endif /* vrcommon_h */
