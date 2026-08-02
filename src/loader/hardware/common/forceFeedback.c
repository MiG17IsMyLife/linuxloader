#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "forceFeedback.h"
#include "../../input/sdlInput.h"
#include "../../log/log.h"

extern SDLControllers sdlJoysticks;

static SDL_Haptic *activeHaptic;
static Uint32 activeFeatures;
static int leftRightEffect = -1;
static int springEffect = -1;
static int damperEffect = -1;
static int constantEffect = -1;
static int periodicEffect = -1;

static float clampUnit(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static float clampSigned(float value)
{
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static void destroyEffect(int *effectId)
{
    if (activeHaptic && *effectId >= 0)
    {
        SDL_StopHapticEffect(activeHaptic, *effectId);
        SDL_DestroyHapticEffect(activeHaptic, *effectId);
    }
    *effectId = -1;
}

static void applyEffect(int *effectId, SDL_HapticEffect *effect)
{
    if (!activeHaptic)
        return;

    if (*effectId >= 0 && !SDL_UpdateHapticEffect(activeHaptic, *effectId, effect))
        destroyEffect(effectId);

    if (*effectId < 0)
        *effectId = SDL_CreateHapticEffect(activeHaptic, effect);

    if (*effectId >= 0)
        SDL_RunHapticEffect(activeHaptic, *effectId, 1);
}

static int supportsSteering(Uint32 features)
{
    return (features & (SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SPRING |
                        SDL_HAPTIC_DAMPER | SDL_HAPTIC_FRICTION |
                        SDL_HAPTIC_SINE | SDL_HAPTIC_LEFTRIGHT)) != 0;
}

static int selectHaptic(SDL_Haptic *haptic, const char *name)
{
    if (!haptic)
        return 0;

    Uint32 features = SDL_GetHapticFeatures(haptic);
    if (!supportsSteering(features))
        return 0;

    activeHaptic = haptic;
    activeFeatures = features;
    log_info("FFB: using %s (features=0x%08x)", name ? name : "SDL haptic", features);
    return 1;
}

void sdlFfbInit(void)
{
    if (activeHaptic)
        return;

    for (int i = 0; i < sdlJoysticks.joysticksCount && i < MAX_JOYSTICKS; ++i)
    {
        SDL_Joystick *joystick = NULL;
        if (sdlJoysticks.controllers[i])
            joystick = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joystick = sdlJoysticks.joysticks[i];

        if (!joystick || !SDL_IsJoystickHaptic(joystick))
            continue;

        SDL_Haptic *haptic = SDL_OpenHapticFromJoystick(joystick);
        if (!haptic)
            continue;

        sdlJoysticks.haptics[i] = haptic;
        if (selectHaptic(haptic, SDL_GetJoystickName(joystick)))
            return;

        log_debug("FFB: joystick %d has no supported force effects", i);
        SDL_CloseHaptic(haptic);
        sdlJoysticks.haptics[i] = NULL;
    }

    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    if (ids)
    {
        for (int i = 0; i < count; ++i)
        {
            SDL_Haptic *haptic = SDL_OpenHaptic(ids[i]);
            if (!haptic)
                continue;

            int slot = -1;
            for (int j = 0; j < MAX_JOYSTICKS; ++j)
            {
                if (!sdlJoysticks.haptics[j])
                {
                    slot = j;
                    break;
                }
            }
            if (slot >= 0)
                sdlJoysticks.haptics[slot] = haptic;

            if (selectHaptic(haptic, SDL_GetHapticNameForID(ids[i])))
            {
                SDL_free(ids);
                return;
            }

            SDL_CloseHaptic(haptic);
            if (slot >= 0)
                sdlJoysticks.haptics[slot] = NULL;
        }
        SDL_free(ids);
    }

    log_warn("FFB: no compatible SDL haptic device found");
}

void sdlFfbRumble(float left, float right, int durationMs)
{
    if (!activeHaptic)
        return;

    left = clampUnit(left);
    right = clampUnit(right);
    if (durationMs <= 0)
        durationMs = 1;

    SDL_HapticEffect effect;
    memset(&effect, 0, sizeof(effect));
    if (activeFeatures & SDL_HAPTIC_LEFTRIGHT)
    {
        effect.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = (Uint32)durationMs;
        effect.leftright.large_magnitude = (Uint16)(left * 65535.0f);
        effect.leftright.small_magnitude = (Uint16)(right * 65535.0f);
        applyEffect(&leftRightEffect, &effect);
    }
    else if (activeFeatures & SDL_HAPTIC_SINE)
    {
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.periodic.length = (Uint32)durationMs;
        effect.periodic.period = 20;
        effect.periodic.magnitude = (Sint16)(fmaxf(left, right) * 32767.0f);
        applyEffect(&periodicEffect, &effect);
    }
}

void sdlFfbStopSteering(void)
{
    destroyEffect(&springEffect);
    destroyEffect(&damperEffect);
    destroyEffect(&constantEffect);
    destroyEffect(&periodicEffect);
}

void sdlFfbApplySteering(const FfbSteeringState *state)
{
    if (!state || !activeHaptic)
        return;
    if (!state->enabled)
    {
        sdlFfbStopSteering();
        return;
    }

    const float spring = clampUnit(state->springStrength);
    if ((activeFeatures & SDL_HAPTIC_SPRING) && spring > 0.001f)
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_SPRING;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.center[0] = (Sint16)state->center;
        effect.condition.right_sat[0] = effect.condition.left_sat[0] = 65535;
        effect.condition.right_coeff[0] = (Sint16)(-32767.0f * spring);
        effect.condition.left_coeff[0] = (Sint16)(32767.0f * spring);
        applyEffect(&springEffect, &effect);
    }
    else
        destroyEffect(&springEffect);

    const float damper = clampUnit(state->damperStrength);
    if ((activeFeatures & SDL_HAPTIC_DAMPER) && damper > 0.001f)
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_DAMPER;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_sat[0] = effect.condition.left_sat[0] = 65535;
        effect.condition.right_coeff[0] = (Sint16)(-32767.0f * damper);
        effect.condition.left_coeff[0] = (Sint16)(32767.0f * damper);
        applyEffect(&damperEffect, &effect);
    }
    else
        destroyEffect(&damperEffect);

    const float constant = clampSigned(state->constantForce);
    if ((activeFeatures & SDL_HAPTIC_CONSTANT) && fabsf(constant) > 0.001f)
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.constant.length = state->vibrationDurationMs > 0
                                     ? (Uint32)state->vibrationDurationMs
                                     : SDL_HAPTIC_INFINITY;
        effect.constant.level = (Sint16)(constant * 32767.0f);
        applyEffect(&constantEffect, &effect);
    }
    else
        destroyEffect(&constantEffect);

    const float vibration = clampUnit(fabsf(state->vibrationStrength));
    if ((activeFeatures & SDL_HAPTIC_SINE) && vibration > 0.001f)
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.periodic.length = state->vibrationDurationMs > 0
                                     ? (Uint32)state->vibrationDurationMs
                                     : 100;
        effect.periodic.period = state->vibrationPeriodMs > 0
                                     ? (Uint16)state->vibrationPeriodMs
                                     : 20;
        effect.periodic.magnitude = (Sint16)(vibration * 32767.0f);
        applyEffect(&periodicEffect, &effect);
    }
    else if ((activeFeatures & SDL_HAPTIC_LEFTRIGHT) && vibration > 0.001f)
        sdlFfbRumble(vibration, vibration, state->vibrationDurationMs);
    else
        destroyEffect(&periodicEffect);
}

void sdlFfbShutdown(void)
{
    sdlFfbStopSteering();
    destroyEffect(&leftRightEffect);
    for (int i = 0; i < MAX_JOYSTICKS; ++i)
    {
        if (sdlJoysticks.haptics[i])
        {
            SDL_CloseHaptic(sdlJoysticks.haptics[i]);
            sdlJoysticks.haptics[i] = NULL;
        }
    }
    activeHaptic = NULL;
    activeFeatures = 0;
}

void sdlFfbDriveboard(const unsigned char *buffer, size_t count)
{
    if (!buffer || count < 3)
        return;

    switch (buffer[0])
    {
        case 0x85:
        {
            float force = (float)buffer[2] / 63.0f;
            sdlFfbRumble(force, force, 100);
            break;
        }
        case 0x84:
        {
            float left = 0.0f;
            float right = 0.0f;
            if (buffer[1] == 0x00)
                right = (127.0f - (float)buffer[2]) / 127.0f;
            else if (buffer[1] == 0x01)
                left = (float)buffer[2] / 127.0f;
            sdlFfbRumble(left, right, 100);
            break;
        }
        case 0xFB:
            if (buffer[2] == 0x02)
                sdlFfbRumble(1.0f, 1.0f, 120);
            else if (buffer[2] == 0x10 || buffer[2] == 0x0B || buffer[2] == 0x04)
                sdlFfbRumble(0.0f, 1.0f, 120);
            else if (buffer[2] == 0x00 || buffer[2] == 0x1B || buffer[2] == 0x14)
                sdlFfbRumble(1.0f, 0.0f, 120);
            break;
        default:
            break;
    }
}

void sdlFfbOutput(const unsigned char *buffer, size_t count)
{
    (void)buffer;
    (void)count;
}
