#pragma once

#include <stdint.h>

typedef enum
{
    RUNTIME_PROFILE_BORDER,
    RUNTIME_PROFILE_CROSSHAIR,
    RUNTIME_PROFILE_BLIT,
    RUNTIME_PROFILE_INPUT,
    RUNTIME_PROFILE_SWAP,
    RUNTIME_PROFILE_LIMITER,
    RUNTIME_PROFILE_TITLE,
    RUNTIME_PROFILE_PHASE_COUNT
} RuntimeProfilePhase;

#ifdef __cplusplus
extern "C" {
#endif

void runtimeProfilerFrameBoundary(void);
uint64_t runtimeProfilerPhaseBegin(void);
void runtimeProfilerPhaseEnd(RuntimeProfilePhase phase, uint64_t started);
void runtimeProfilerShutdown(void);

#ifdef __cplusplus
}
#endif
