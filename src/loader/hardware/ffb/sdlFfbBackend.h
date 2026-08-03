#ifndef PACLOADER_SDL_FFB_BACKEND_H
#define PACLOADER_SDL_FFB_BACKEND_H

#include <stddef.h>

typedef struct
{
    int enabled;
    int center;
    float springStrength;
    float damperStrength;

    /*
     * Optional shaping, both 0..1 and both off by default. The cabinet is
     * direct drive as well, so neither corrects for it - a floor under the
     * damper only stops the wheel going fully slack, and a deadband only stops
     * a strong spring hunting at dead centre.
     */
    float damperFloor;
    float springDeadband;

    /*
     * Where the wheel is sitting, -1 to 1. Only used when the device has no
     * spring of its own: the centring force is worked out from it and folded
     * into the constant force, so a wheel that supports nothing but constant
     * force still self centres.
     */
    float wheelPosition;
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
