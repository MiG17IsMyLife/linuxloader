#include <stdint.h>

#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>
#else
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "../config/config.h"
#include "fpsLimiter.h"

double lastTime = 0.0;
int frameCount = 0;
double fps = 0.0;

static int g_fpsLimiterEnabled = 0;
static double g_targetFps = 60.0;

#if defined(_WIN32) || defined(__MINGW32__)
static LARGE_INTEGER g_qpcFrequency;
static LONGLONG g_qpcStart = 0;
static uint64_t g_frameIndex = 0;

static LONGLONG qpcNow(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static LONGLONG qpcDeadline(uint64_t frameIndex)
{
    /* Keep the fractional part of non-integer frame periods instead of
     * truncating the target to whole microseconds. */
    const long double ticks =
        (long double)g_qpcStart +
        ((long double)frameIndex * (long double)g_qpcFrequency.QuadPart) / g_targetFps;
    return (LONGLONG)ticks;
}

static void waitUntilQpc(LONGLONG deadline)
{
    const LONGLONG oneMillisecond = g_qpcFrequency.QuadPart / 1000;
    const LONGLONG twoMilliseconds = oneMillisecond * 2;

    for (;;)
    {
        const LONGLONG remaining = deadline - qpcNow();
        if (remaining <= 0)
            return;

        /* Sleep while there is enough time left, yield near the deadline,
         * then use a short pause for the final sub-millisecond window.  This
         * retains QPC precision without burning a CPU for the whole frame. */
        if (remaining > twoMilliseconds)
            Sleep(1);
        else if (remaining > oneMillisecond / 10)
            Sleep(0);
        else
            _mm_pause();
    }
}
#endif

void initFpsLimiter(void)
{
    g_fpsLimiterEnabled = getConfig()->fpsLimiter == 1;
    g_targetFps = getConfig()->fpsTarget > 0.0f ? getConfig()->fpsTarget : 60.0;
    lastTime = 0.0;
    frameCount = 0;
    fps = 0.0;

    if (!g_fpsLimiterEnabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    QueryPerformanceFrequency(&g_qpcFrequency);
    g_qpcStart = qpcNow();
    g_frameIndex = 0;

    /* Sleep(1) is used as the coarse part of the hybrid wait above. */
    timeBeginPeriod(1);
#endif
}

double getTimeInMilliseconds(void)
{
    return (double)clockNow() / 1000.0;
}

double getTimeInSeconds(void)
{
    return (double)clockNow() / 1000000.0;
}

double calculateFps(void)
{
    const double currentTime = getTimeInSeconds();
    const double deltaTime = currentTime - lastTime;
    frameCount++;
    if (deltaTime >= 1.0)
    {
        fps = frameCount / deltaTime;
        frameCount = 0;
        lastTime = currentTime;
    }
    return fps;
}

int64_t clockNow(void)
{
#if defined(_WIN32) || defined(__MINGW32__)
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    LARGE_INTEGER now;

    if (!initialized)
    {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    QueryPerformanceCounter(&now);
    return (int64_t)(((long double)now.QuadPart * 1000000.0L) /
                     (long double)frequency.QuadPart);
#else
    struct timeval timeNow;
    gettimeofday(&timeNow, NULL);
    return (int64_t)timeNow.tv_sec * 1000000 + timeNow.tv_usec;
#endif
}

void frameTiming(void)
{
    if (!g_fpsLimiterEnabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    const LONGLONG deadline = qpcDeadline(g_frameIndex);
    waitUntilQpc(deadline);

    const LONGLONG now = qpcNow();
    const LONGLONG period =
        (LONGLONG)((long double)g_qpcFrequency.QuadPart / g_targetFps);

    /* If rendering missed a full frame, skip stale deadlines instead of
     * issuing a burst of frames while trying to catch up. */
    if (now > deadline + period)
    {
        const long double elapsed = (long double)(now - g_qpcStart);
        g_frameIndex = (uint64_t)(elapsed * g_targetFps /
                                   (long double)g_qpcFrequency.QuadPart);
    }
    g_frameIndex++;
#else
    const int64_t targetMicroseconds = (int64_t)(1000000.0 / g_targetFps);
    static int64_t nextDeadline = 0;
    const int64_t now = clockNow();

    if (nextDeadline == 0)
        nextDeadline = now;
    nextDeadline += targetMicroseconds;

    while ((nextDeadline - clockNow()) > 0)
    {
        const int64_t remaining = nextDeadline - clockNow();
        struct timespec request;
        request.tv_sec = (time_t)(remaining / 1000000);
        request.tv_nsec = (long)(remaining % 1000000) * 1000L;
        nanosleep(&request, NULL);
    }

    if (clockNow() > nextDeadline + targetMicroseconds)
        nextDeadline = clockNow();
#endif
}

/* Kept as a compatibility entry point for existing callers. The new limiter
 * is deadline-based, so per-frame state is maintained internally. */
void fpsLimiter(FpsLimit *stats)
{
    (void)stats;
    frameTiming();
}
