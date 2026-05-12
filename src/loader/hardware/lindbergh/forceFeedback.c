#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <stdio.h>
#include <string.h>

#include "forceFeedback.h"
#include "../../input/sdlInput.h"
#include "../../log/log.h"

static int effect_id = -1;
extern SDLControllers sdlJoysticks;

void sdlFfbInit(void)
{
    // SDL input mode: open haptic from already-opened joysticks
    for (int i = 0; i < sdlJoysticks.joysticksCount && i < MAX_JOYSTICKS; ++i)
    {
        SDL_Joystick *joy = NULL;

        if (sdlJoysticks.controllers[i])
            joy = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joy = sdlJoysticks.joysticks[i];

        if (!joy)
            continue;

        if (!SDL_IsJoystickHaptic(joy))
            continue;

        sdlJoysticks.haptics[i] = SDL_OpenHapticFromJoystick(joy);
        if (sdlJoysticks.haptics[i])
        {
            if (SDL_GetHapticFeatures(sdlJoysticks.haptics[i]) & SDL_HAPTIC_LEFTRIGHT)
            {
                log_warn("FFB: Haptic opened from joystick %d (%s)", i, SDL_GetJoystickName(joy));
                sdlFfbRumble(1.0, 1.0, 200);
            }
            else
            {
                log_warn("FFB: Joystick %d has no LEFTRIGHT support, skipping", i);
                SDL_CloseHaptic(sdlJoysticks.haptics[i]);
                sdlJoysticks.haptics[i] = NULL;
            }
        }
    }

    // EVDEV input mode: fall back to standalone haptic devices
    if (!sdlJoysticks.haptics[0])
    {
        int num_haptics;
        SDL_HapticID *haptics = SDL_GetHaptics(&num_haptics);
        if (haptics)
        {
            for (int i = 0; i < num_haptics && i < MAX_JOYSTICKS; ++i)
            {
                if (sdlJoysticks.haptics[i])
                    continue;

                sdlJoysticks.haptics[i] = SDL_OpenHaptic(haptics[i]);
                if (sdlJoysticks.haptics[i])
                {
                    if (SDL_GetHapticFeatures(sdlJoysticks.haptics[i]) & SDL_HAPTIC_LEFTRIGHT)
                    {
                        log_warn("FFB: Standalone haptic %d: %s", i, SDL_GetHapticNameForID(haptics[i]));
                        sdlFfbRumble(1.0, 1.0, 200);
                    }
                    else
                    {
                        log_warn("FFB: Standalone haptic %d has no LEFTRIGHT support, skipping", i);
                        SDL_CloseHaptic(sdlJoysticks.haptics[i]);
                        sdlJoysticks.haptics[i] = NULL;
                    }
                }
            }
            SDL_free(haptics);
        }
    }

    if (!sdlJoysticks.haptics[0])
        log_warn("FFB: No haptic device found");
}

void sdlFfbRumble(float left, float right, int duration_ms)
{
    if (!sdlJoysticks.haptics[0])
        return;
    SDL_HapticEffect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_LEFTRIGHT;
    effect.leftright.length = duration_ms;
    effect.leftright.large_magnitude = (Uint16)(left * 0xFFFF);
    effect.leftright.small_magnitude = (Uint16)(right * 0xFFFF);
    if (effect_id >= 0)
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], effect_id);
    effect_id = SDL_CreateHapticEffect(sdlJoysticks.haptics[0], &effect);
    SDL_RunHapticEffect(sdlJoysticks.haptics[0], effect_id, 1);
}

void sdlFfbShutdown(void)
{
    if (sdlJoysticks.haptics[0])
    {
        SDL_CloseHaptic(sdlJoysticks.haptics[0]);
        sdlJoysticks.haptics[0] = NULL;
    }
}

void sdlFfbDriveboard(const unsigned char *buffer, size_t count)
{

    // printf("FFB driveboard: count=%zu, data:", count);
    // for (size_t i = 0; i < count; ++i) {
    //     printf(" 0x%02x", buffer[i]);
    // }
    // printf("\n");

    switch (buffer[0])
    {
        case 0x80:
        { // power
            if (buffer[2] == 0x01)
            {
                log_info("0x80 command: Triggering full-power rumble\n");
                // ffb_rumble(1.0f, 1.0f, 500);
            }
            break;
        }
        case 0x85:
        { // Rumble/vibrate
            // uint8_t speed = buffer[1];
            uint8_t power = buffer[2];

            float force = (float)power / 63.0f; // Map power 1-63 to 0.0-1.0

            int duration = 100;
            log_info("Triggering rumble: force=%.2f, duration=%dms\n", force, duration);
            sdlFfbRumble(force, force, duration);
            break;
        }
        case 0x84:
        { // Movement command
            uint8_t direction = buffer[1];
            uint8_t value = buffer[2];

            float left = 0.0f, right = 0.0f;
            int duration = 100; // ms

            if (direction == 0x00)
            { // right - value from 0x7F (min) to 0x01 (max)

                right = (127.0f - (float)value) / 127.0f;
            }
            else if (direction == 0x01)
            { // left  - value goes from 0x00 (min) to 0x7F (max)

                left = (float)value / 127.0f;
            }

            log_info("Triggering movement rumble: left=%.2f, right=%.2f, duration=%dms\n", left, right, duration);
            sdlFfbRumble(left, right, duration);
            break;
        }
        case 0x86: // Friction
            log_info("Friction:");
            printf(" Power: %d", buffer[1]);
            printf(" / Percentage: %d\n", buffer[2]);
            break;
        case 0x8:
            break;
        case 0x87: // move and set target point
            printf("Move and set target point\n");
            break;
        case 0x88: // move to current target point
            printf("Move to current target point\n");
            break;
        case 0x8B: // centering strength
            printf("Centering strength:");
            printf(" param1: %d", buffer[1]);
            printf(" / param2: %d\n", buffer[2]);
            break;
        case 0xFB:
        { // Playback sud package

            uint8_t effect = buffer[2];
            printf("Outrun FFB 0xFB effect: 0x%02x\n", effect);

            if (effect == 0x02)
            {
                sdlFfbRumble(1.0f, 1.0f, 120);
            }
            else if (effect == 0x10 || effect == 0x0B || effect == 0x04)
            {
                sdlFfbRumble(0.0f, 1.0f, 120);
            }
            else if (effect == 0x00 || effect == 0x1B || effect == 0x14)
            {
                sdlFfbRumble(1.0f, 0.0f, 120);
            }
            break;
        }
        default:
            break;
    }
}

void ffb_output(const unsigned char *buffer, size_t count)
{
    /*     printf("FFB output: count=%zu, data:", count);
        for (size_t i = 0; i < count; ++i)
            printf(" 0x%02x", buffer[i]);
        printf("\n"); */

    if (count > 0)
    {
        if (buffer[0] & 0x40)
        {
            // printf("GPO: ABC Vibration triggered (bit 6 set)\n");
            // sdlFfbRumble(1.0f, 1.0f, 120);
        }
    }
}