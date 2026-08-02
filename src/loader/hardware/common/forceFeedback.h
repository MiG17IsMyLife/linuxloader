#ifndef FFB_H
#define FFB_H

#include <stddef.h>

typedef struct
{
    int enabled;
    int center;
    float springStrength;
    float damperStrength;
    float constantForce;
    float vibrationStrength;
    int vibrationPeriodMs;
    int vibrationDurationMs;
} FfbSteeringState;

#ifdef __cplusplus
extern "C" {
#endif

void sdlFfbInit(void);
void sdlFfbShutdown(void);
void sdlFfbRumble(float left, float right, int duration_ms);
void sdlFfbApplySteering(const FfbSteeringState *state);
void sdlFfbStopSteering(void);
void sdlFfbDriveboard(const unsigned char *buffer, size_t count);
void sdlFfbOutput(const unsigned char *buffer, size_t count);

#ifdef __cplusplus
}
#endif

#endif
