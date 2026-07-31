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
    int torqueEnabled;
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
