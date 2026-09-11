/*
 * vr_perf.cpp - one frame-rate line to logcat every few seconds.
 *
 * The in-game FPS display is drawn into an eye buffer we cannot see over adb,
 * so the game reports each rendered frame here (VrPerfFrame) and this prints
 * the average, the 1% low and the draw calls per frame.
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "vr_log.h"
#include "vdrift_vr.h"
#include "vr_internal.h"

#define REPORT_PERIOD_NS 5000000000LL   /* 5 s */

static int64_t sReportStart;
static int64_t sLastFrame;
static int64_t sWorst;
static int     sFrames;
static long    sDrawCalls;

static int64_t nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

extern "C" void VrPerfInit(void)
{
    sReportStart = 0;
    sLastFrame = 0;
    sWorst = 0;
    sFrames = 0;
    sDrawCalls = 0;
}

extern "C" void VrPerfFrame(int drawCalls)
{
    int64_t now = nowNs();
    if (sReportStart == 0) {
        sReportStart = now;
        sLastFrame = now;
        return;
    }
    int64_t dt = now - sLastFrame;
    sLastFrame = now;
    if (dt > sWorst) sWorst = dt;
    sFrames++;
    sDrawCalls += drawCalls;

    int64_t elapsed = now - sReportStart;
    if (elapsed < REPORT_PERIOD_NS) {
        return;
    }
    double secs = (double)elapsed / 1e9;
    ALOGI("perf: %.1f fps (%d frames, worst %.1f ms) | %.0f draws/frame",
          (double)sFrames / secs, sFrames, (double)sWorst / 1e6,
          sFrames ? (double)sDrawCalls / sFrames : 0.0);
    sReportStart = now;
    sWorst = 0;
    sFrames = 0;
    sDrawCalls = 0;
}
