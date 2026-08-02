#pragma once

#include <stdint.h>

typedef struct
{
    uint16_t center;
    int centerOffset;
    uint8_t spring;
    uint8_t viscosity;
    int8_t reflection;
    float reflectionStrength;
    float vibrationStrength;
    int vibrationPeriod;
    int vibrationDuration;

    /*
     * clKickback::onTrq/offTrq start and stop a torque ramp. Only some
     * revisions use them - WMMT3 drives spring, damper and reflection without
     * ever calling either - so this mutes output rather than enabling it, and
     * starts out unmuted.
     */
    int torqueEnabled;

    /*
     * clKickback::setTRQPowerType is the real master switch, and the value the
     * game's own decordResultCode() and waitSelfCheck() test for "force
     * feedback fitted". 0 is off, 1 is half strength, 2 is full; the scale is
     * the game's own table for each, and clKickback::init defaults to type 1.
     * The setters carry unscaled magnitudes, so the scale applies here.
     */
    int powerType;
    float powerScale;

    /*
     * Whether the motor is turning, from clKickback::onPower and offPower. A
     * stopped motor makes no force whatever the coefficients say, and the game
     * leaves spring and viscosity at their last race values when it drops back
     * to attract - so without this the wheel stayed stiff on the attract
     * screen.
     */
    int motorRunning;
} N2SteeringOutputState;

typedef struct
{
    void (*apply)(const N2SteeringOutputState *state, void *userData);
    void (*shutdown)(void *userData);
    void *userData;
} N2SteeringOutputBackend;

#ifdef __cplusplus
extern "C" {
#endif

int n2SteeringIoInstallHooks(void);
void n2SteeringIoSetBackend(const N2SteeringOutputBackend *backend);
const N2SteeringOutputState *n2SteeringIoGetState(void);

#ifdef __cplusplus
}
#endif
