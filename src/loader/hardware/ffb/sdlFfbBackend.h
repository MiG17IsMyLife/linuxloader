#ifndef PACLOADER_SDL_FFB_BACKEND_H
#define PACLOADER_SDL_FFB_BACKEND_H

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

/*
 * Called from the force feedback worker whenever it has been idle, so a source
 * whose state changes without anything pushing it can be re-read. Runs off the
 * game thread, so whatever it calls has to be safe there.
 */
void sdlFfbSetSteeringPoll(void (*poll)(void));
void sdlFfbStopSteering(void);
void sdlFfbDriveboard(const unsigned char *buffer, size_t count);
void sdlFfbOutput(const unsigned char *buffer, size_t count);

#ifdef __cplusplus
}
#endif

#endif
