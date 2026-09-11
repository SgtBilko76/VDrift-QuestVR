#ifndef VR_PATHS_H
#define VR_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Redirect stdout/stderr into logcat (tag "VDriftVR"). VDrift logs to a file
 * and to cout/cerr; this makes the latter visible over adb. */
void VrLogRedirectStdio(void);

/* Prepare the directory layout under <base> (/sdcard/VDriftVR):
 *   <base>/data/       the game data (adb push, or unpacked from the APK)
 *   <base>/.vdrift/    user settings; VDrift's PathManager finds it as $HOME/.vdrift
 *   <base>/templates/  VR defaults, copied into .vdrift on first run (never overwritten)
 * Returns 0 when <base>/data looks like the VDrift data tree. */
int VrPathsInit(const char* base);

#ifdef __cplusplus
}
#endif

#endif
