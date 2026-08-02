#include "runtimeProfiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#else
#include <time.h>
#endif

#define PROFILE_WINDOW_FRAMES 600

static int initialized;
static int enabled;
static uint64_t frequency;
static uint64_t lastFrame;
static uint64_t frameSamples[PROFILE_WINDOW_FRAMES];
static uint64_t phaseTicks[RUNTIME_PROFILE_PHASE_COUNT];
static unsigned int sampleCount;
static unsigned long long totalFrames;
static FILE *output;

static uint64_t profilerNow(void)
{
#if defined(_WIN32) || defined(__MINGW32__)
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return (uint64_t)value.QuadPart;
#else
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#endif
}

static void initializeProfiler(void)
{
    if (initialized)
        return;
    initialized = 1;

    const char *requested = getenv("LL_PROFILE");
    enabled = requested && requested[0] != '\0' && strcmp(requested, "0") != 0;
    if (!enabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    frequency = (uint64_t)value.QuadPart;
#else
    frequency = 1000000000ULL;
#endif

    output = fopen("linuxloader-frame-profile.csv", "w");
    if (!output)
    {
        enabled = 0;
        return;
    }

    fputs("total_frames,sample_frames,avg_ms,p50_ms,p95_ms,p99_ms,max_ms,"
          "border_avg_ms,crosshair_avg_ms,blit_avg_ms,input_avg_ms,swap_avg_ms,"
          "limiter_avg_ms,title_avg_ms\n", output);
    fflush(output);
}

static int compareTicks(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b;
}

static double milliseconds(uint64_t ticks)
{
    return frequency ? (double)ticks * 1000.0 / (double)frequency : 0.0;
}

static void writeWindow(void)
{
    uint64_t sorted[PROFILE_WINDOW_FRAMES];
    memcpy(sorted, frameSamples, sampleCount * sizeof(sorted[0]));
    qsort(sorted, sampleCount, sizeof(sorted[0]), compareTicks);

    uint64_t total = 0;
    for (unsigned int i = 0; i < sampleCount; ++i)
        total += frameSamples[i];

    const unsigned int p50 = (sampleCount - 1) * 50 / 100;
    const unsigned int p95 = (sampleCount - 1) * 95 / 100;
    const unsigned int p99 = (sampleCount - 1) * 99 / 100;
    fprintf(output, "%llu,%u,%.4f,%.4f,%.4f,%.4f,%.4f",
            totalFrames, sampleCount, milliseconds(total) / sampleCount,
            milliseconds(sorted[p50]), milliseconds(sorted[p95]),
            milliseconds(sorted[p99]), milliseconds(sorted[sampleCount - 1]));
    for (int phase = 0; phase < RUNTIME_PROFILE_PHASE_COUNT; ++phase)
        fprintf(output, ",%.4f", milliseconds(phaseTicks[phase]) / sampleCount);
    fputc('\n', output);
    fflush(output);

    sampleCount = 0;
    memset(phaseTicks, 0, sizeof(phaseTicks));
}

void runtimeProfilerFrameBoundary(void)
{
    initializeProfiler();
    if (!enabled)
        return;

    const uint64_t now = profilerNow();
    if (lastFrame)
    {
        frameSamples[sampleCount++] = now - lastFrame;
        ++totalFrames;
        if (sampleCount == PROFILE_WINDOW_FRAMES)
            writeWindow();
    }
    lastFrame = now;
}

uint64_t runtimeProfilerPhaseBegin(void)
{
    initializeProfiler();
    return enabled ? profilerNow() : 0;
}

void runtimeProfilerPhaseEnd(RuntimeProfilePhase phase, uint64_t started)
{
    if (enabled && started && phase >= 0 && phase < RUNTIME_PROFILE_PHASE_COUNT)
        phaseTicks[phase] += profilerNow() - started;
}

void runtimeProfilerShutdown(void)
{
    if (output)
    {
        if (sampleCount)
            writeWindow();
        fclose(output);
        output = NULL;
    }
    enabled = 0;
}
