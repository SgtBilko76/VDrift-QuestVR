#ifndef VR_LOG_H
#define VR_LOG_H

#include <android/log.h>

#define VR_LOG_TAG "VDriftVR"

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, VR_LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, VR_LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, VR_LOG_TAG, __VA_ARGS__)
#ifndef NDEBUG
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, VR_LOG_TAG, __VA_ARGS__)
#else
#define ALOGV(...)
#endif

#endif
