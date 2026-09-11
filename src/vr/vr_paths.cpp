/*
 * vr_paths.cpp - data / settings directories for VDrift on Android.
 *
 * VDrift looks for its data in $VDRIFT_DATA_DIRECTORY and keeps user settings in
 * $HOME/.vdrift (SETTINGS_DIR). Both are pointed into the one writable place the
 * player can reach over adb:
 *
 *   /sdcard/VDriftVR/data/       the data tree (cars, tracks, settings, shaders, ...)
 *   /sdcard/VDriftVR/.vdrift/    VDrift.config, controls.config, replays, logs
 *   /sdcard/VDriftVR/templates/  VR defaults: seeded into .vdrift once, then left alone
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "vr_paths.h"
#include "vr_log.h"
#include "vdrift_vr.h"

static char gBaseDir[1024];
static char gDataDir[1024];
static char gHomeDir[1024];

/* ------------------------------------------------------------------ logcat */
static void* stdioPumpThread(void* arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[1024];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(fd, buf + used, sizeof(buf) - 1 - used);
        if (n <= 0) {
            if (used) {
                buf[used] = 0;
                __android_log_write(ANDROID_LOG_INFO, VR_LOG_TAG, buf);
            }
            break;
        }
        used += n;
        buf[used] = 0;
        char* start = buf;
        char* nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = 0;
            if (nl > start) {
                __android_log_write(ANDROID_LOG_INFO, VR_LOG_TAG, start);
            }
            start = nl + 1;
        }
        used = strlen(start);
        memmove(buf, start, used + 1);
        if (used == sizeof(buf) - 1) {
            __android_log_write(ANDROID_LOG_INFO, VR_LOG_TAG, buf);
            used = 0;
        }
    }
    close(fd);
    return NULL;
}

extern "C" void VrLogRedirectStdio(void)
{
    int fds[2];
    if (pipe(fds) != 0) {
        return;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, stdioPumpThread, (void*)(intptr_t)fds[0]);
    pthread_attr_destroy(&attr);
}

/* ------------------------------------------------------------------ files */
static bool isDir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool isFile(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int mkdirP(const char* path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static bool copyFile(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return true;
}

/* Copy templates/<sub>/* into <dst>/ for every file that is not there yet. */
static void seedDir(const char* srcDir, const char* dstDir)
{
    DIR* dp = opendir(srcDir);
    if (!dp) {
        return;
    }
    mkdirP(dstDir);
    struct dirent* ep;
    while ((ep = readdir(dp)) != NULL) {
        if (ep->d_name[0] == '.') {
            continue;
        }
        char src[1024], dst[1024];
        snprintf(src, sizeof(src), "%s/%s", srcDir, ep->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", dstDir, ep->d_name);
        if (isDir(src)) {
            seedDir(src, dst);
        } else if (!isFile(dst)) {
            if (copyFile(src, dst)) {
                ALOGI("VrPathsInit: seeded %s", dst);
            } else {
                ALOGE("VrPathsInit: could not seed %s", dst);
            }
        }
    }
    closedir(dp);
}

/* ------------------------------------------------------------------ init */
extern "C" int VrPathsInit(const char* base)
{
    char tmp[1024];
    snprintf(gBaseDir, sizeof(gBaseDir), "%s", base);
    snprintf(gDataDir, sizeof(gDataDir), "%s/data", base);
    snprintf(gHomeDir, sizeof(gHomeDir), "%s", base);

    /* VDrift's PathManager: data from the env var, settings under $HOME/.vdrift */
    setenv("VDRIFT_DATA_DIRECTORY", gDataDir, 1);
    setenv("HOME", gHomeDir, 1);

    if (!isDir(base)) {
        ALOGE("VrPathsInit: %s does not exist (push the game data first)", base);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s/settings/options.config", gDataDir);
    if (!isFile(tmp)) {
        ALOGE("VrPathsInit: %s missing: game data not pushed?", tmp);
        return -1;
    }

    /* The settings tree, seeded from the VR templates on first run. VDrift creates
     * the rest (replays, screenshots, logs) itself. */
    snprintf(tmp, sizeof(tmp), "%s/.vdrift", base);
    mkdirP(tmp);
    char templates[1024];
    snprintf(templates, sizeof(templates), "%s/templates/.vdrift", base);
    seedDir(templates, tmp);

    /* VDrift opens a few things relative to the working directory. */
    if (chdir(gDataDir) != 0) {
        ALOGE("VrPathsInit: chdir(%s) failed: %s", gDataDir, strerror(errno));
    }

    ALOGI("VrPathsInit: data=%s home=%s", gDataDir, gHomeDir);
    return 0;
}

extern "C" const char* VrBaseDir(void) { return gBaseDir; }
extern "C" const char* VrDataDir(void) { return gDataDir; }
extern "C" const char* VrHomeDir(void) { return gHomeDir; }
