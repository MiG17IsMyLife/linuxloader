#pragma once

#include <stdint.h>

/* Retained for source compatibility with older callers. Frame pacing state
 * is now kept internally by fpsLimiter.c and scheduled against QPC deadlines. */
typedef struct
{
    long targetFrameTime;
    long frameStart;
    long frameEnd;
    long sleepTime;
    long frameOverhead;
} FpsLimit;

#ifdef __cplusplus
extern "C" {
#endif

void initFpsLimiter(void);
int64_t clockNow(void);
void fpsLimiter(FpsLimit *stats);
double calculateFps(void);
void frameTiming(void);

#ifdef __cplusplus
}
#endif
