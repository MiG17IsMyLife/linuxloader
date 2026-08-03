#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdlFfbBackend.h"
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

// The last state actually pushed to the device, so repeats can be dropped.
static FfbSteeringState lastSteering;
static int steeringApplied;

/*
 * Effect bookkeeping.  Creating and destroying an effect is far dearer than
 * updating one, so a state that keeps crossing a magnitude threshold can cost
 * more than a state that changes constantly.  Counting them is the only way to
 * tell those two apart from a frame rate alone.
 */
static unsigned long effectCreates;
static unsigned long effectDestroys;
static unsigned long effectUpdates;
static unsigned long steeringApplies;
static unsigned long steeringSkips;

// Vibration magnitude last pushed, to spot the rising edge of a fresh burst.
static float previousVibration;

// Defined with the worker below; started as soon as a device is chosen.
static void startSteeringWorker(void);

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
        effectDestroys++;
    }
    *effectId = -1;
}

static const char *effectName(Uint16 type)
{
    switch (type)
    {
        case SDL_HAPTIC_SPRING:    return "spring";
        case SDL_HAPTIC_DAMPER:    return "damper";
        case SDL_HAPTIC_CONSTANT:  return "constant";
        case SDL_HAPTIC_SINE:      return "sine";
        case SDL_HAPTIC_LEFTRIGHT: return "leftright";
        default:                   return "effect";
    }
}

static void applyEffect(int *effectId, SDL_HapticEffect *effect)
{
    if (!activeHaptic)
        return;

    if (*effectId >= 0)
    {
        /*
         * An update keeps a running effect running, so it must not be followed
         * by another run.  Re-running costs a second driver round trip and
         * restarts the effect from its beginning, which turns a steady spring
         * into one that is continually retriggered.
         */
        if (SDL_UpdateHapticEffect(activeHaptic, *effectId, effect))
        {
            effectUpdates++;
            return;
        }
        log_warn("FFB: %s update rejected: %s", effectName(effect->type),
                 SDL_GetError());
        destroyEffect(effectId);
    }

    *effectId = SDL_CreateHapticEffect(activeHaptic, effect);
    if (*effectId < 0)
    {
        log_warn("FFB: %s not created: %s", effectName(effect->type),
                 SDL_GetError());
        return;
    }

    effectCreates++;
    if (!SDL_RunHapticEffect(activeHaptic, *effectId, 1))
        log_warn("FFB: %s created but would not run: %s",
                 effectName(effect->type), SDL_GetError());
    else
        log_info("FFB: %s effect running", effectName(effect->type));
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

    // Report the raw mask either way: knowing which effects a wheel advertises
    // is the only way to tell a rejected device from an unsupported one.
    log_info("FFB: %s features=0x%08x [%s%s%s%s%s%s] axes=%d effects=%d",
             name ? name : "SDL haptic", features,
             (features & SDL_HAPTIC_CONSTANT) ? "constant " : "",
             (features & SDL_HAPTIC_SPRING) ? "spring " : "",
             (features & SDL_HAPTIC_DAMPER) ? "damper " : "",
             (features & SDL_HAPTIC_FRICTION) ? "friction " : "",
             (features & SDL_HAPTIC_SINE) ? "sine " : "",
             (features & SDL_HAPTIC_LEFTRIGHT) ? "leftright " : "",
             SDL_GetNumHapticAxes(haptic), SDL_GetMaxHapticEffects(haptic));

    if (!supportsSteering(features))
        return 0;

    activeHaptic = haptic;
    activeFeatures = features;
    log_info("FFB: using %s", name ? name : "SDL haptic");
    startSteeringWorker();
    return 1;
}

void sdlFfbInit(void)
{
    if (activeHaptic)
        return;

    // Slots are player-indexed, not packed, so scan all of them rather than
    // stopping at joysticksCount.
    for (int i = 0; i < MAX_JOYSTICKS; ++i)
    {
        SDL_Joystick *joystick = NULL;
        if (sdlJoysticks.controllers[i])
            joystick = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joystick = sdlJoysticks.joysticks[i];

        if (!joystick)
            continue;

        if (!SDL_IsJoystickHaptic(joystick))
        {
            log_info("FFB: joystick %d (%s) reports no haptic capability", i,
                     SDL_GetJoystickName(joystick));
            continue;
        }

        SDL_Haptic *haptic = SDL_OpenHapticFromJoystick(joystick);
        if (!haptic)
        {
            log_warn("FFB: joystick %d (%s) failed to open haptic: %s", i,
                     SDL_GetJoystickName(joystick), SDL_GetError());
            continue;
        }

        sdlJoysticks.haptics[i] = haptic;
        if (selectHaptic(haptic, SDL_GetJoystickName(joystick)))
            return;

        log_warn("FFB: joystick %d has no supported force effects", i);
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
    steeringApplied = 0;
    previousVibration = 0.0f;
}

static void applySteeringNow(const FfbSteeringState *original)
{
    /*
     * A disabled state is driven to zero rather than torn down. The game
     * toggles torque off and back on within a frame or two at a time - offTrq
     * then onTrq, measured sixty odd milliseconds apart, over and over - and
     * destroying every effect on each of those only to rebuild them is the same
     * churn that cost frame time before, now reached through the enable flag
     * instead of a magnitude.
     */
    FfbSteeringState zeroed;
    const FfbSteeringState *state = original;

    if (!original->enabled)
    {
        zeroed = *original;
        zeroed.springStrength = 0.0f;
        zeroed.damperStrength = 0.0f;
        zeroed.constantForce = 0.0f;
        zeroed.vibrationStrength = 0.0f;
        state = &zeroed;
    }

    if (++steeringApplies % 120 == 0)
        log_info("FFB: applied=%lu skipped=%lu | creates=%lu destroys=%lu updates=%lu",
                 steeringApplies, steeringSkips, effectCreates, effectDestroys,
                 effectUpdates);

    /*
     * Effects are left allocated once they exist and driven to zero instead of
     * being torn down, because the game's magnitudes cross zero constantly -
     * reflection alone was measured toggling 0/34/0 while driving - and a
     * create/destroy pair per crossing costs far more than an update carrying a
     * zero.  They are only really destroyed when steering stops.
     */
    const float spring = clampUnit(state->springStrength);
    if ((activeFeatures & SDL_HAPTIC_SPRING) && (spring > 0.001f || springEffect >= 0))
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_SPRING;
        /*
         * The one effect that reaches the wheel is the constant force, and the
         * only thing it does differently is name its axis. A condition effect
         * left with a zeroed direction defaults to polar zero, which is not the
         * steering axis on a two axis device.
         */
        effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.center[0] = 0;
        effect.condition.right_sat[0] = effect.condition.left_sat[0] = 65535;
        effect.condition.deadband[0] =
            (Uint16)(clampUnit(state->springDeadband) * 65535.0f);
        /*
         * Both coefficients are positive, which is what makes a spring pull
         * back towards its centre. A negative one on the right pushed further
         * right instead, so the two halves fought each other and what reached
         * the wheel was a fraction of the centring force the game asked for.
         */
        effect.condition.right_coeff[0] = (Sint16)(32767.0f * spring);
        effect.condition.left_coeff[0] = (Sint16)(32767.0f * spring);
        applyEffect(&springEffect, &effect);
    }

    // Optional floor, off by default. The cabinet is direct drive too, so the
    // game's own viscosity is already the damping it means - this is taste.
    float damper = clampUnit(state->damperStrength);
    const float damperFloor = clampUnit(state->damperFloor);
    if (damper < damperFloor)
        damper = damperFloor;

    if ((activeFeatures & SDL_HAPTIC_DAMPER) && (damper > 0.001f || damperEffect >= 0))
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_DAMPER;
        effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_sat[0] = effect.condition.left_sat[0] = 65535;
        // Same again: a damper resists movement in both directions.
        effect.condition.right_coeff[0] = (Sint16)(32767.0f * damper);
        effect.condition.left_coeff[0] = (Sint16)(32767.0f * damper);
        applyEffect(&damperEffect, &effect);
    }

    float constant = clampSigned(state->constantForce);
    if (!(activeFeatures & SDL_HAPTIC_SPRING) && spring > 0.001f)
    {
        float deflection = clampSigned(state->wheelPosition);
        const float slack = clampUnit(state->springDeadband);

        if (fabsf(deflection) <= slack)
            deflection = 0.0f;
        else if (slack < 1.0f)
            deflection = (deflection - (deflection > 0.0f ? slack : -slack)) /
                         (1.0f - slack);

        constant = clampSigned(constant - deflection * spring);
    }

    if ((activeFeatures & SDL_HAPTIC_CONSTANT) &&
        (fabsf(constant) > 0.001f || constantEffect >= 0))
    {
        SDL_HapticEffect effect;
        memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        // Held indefinitely: a length taken from the vibration fields would end
        // the effect behind the game's back and need it recreated.
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = (Sint16)(constant * 32767.0f);
        applyEffect(&constantEffect, &effect);
    }

    const float vibration = clampUnit(fabsf(state->vibrationStrength));
    if (activeFeatures & SDL_HAPTIC_SINE)
    {
        if (vibration > 0.001f || periodicEffect >= 0)
        {
            SDL_HapticEffect effect;
            memset(&effect, 0, sizeof(effect));
            effect.type = SDL_HAPTIC_SINE;
            effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
            // The game passes a duration, so let the burst end on its own
            // rather than deciding here how long a shake should last.
            effect.periodic.length = state->vibrationDurationMs > 0
                                         ? (Uint32)state->vibrationDurationMs
                                         : SDL_HAPTIC_INFINITY;
            effect.periodic.period = state->vibrationPeriodMs > 0
                                         ? (Uint16)state->vibrationPeriodMs
                                         : 20;
            effect.periodic.magnitude = (Sint16)(vibration * 32767.0f);
            applyEffect(&periodicEffect, &effect);

            // A burst that already ran to its length is stopped, so starting
            // the next one takes a run and not just new parameters.
            if (periodicEffect >= 0 && vibration > 0.001f &&
                previousVibration <= 0.001f)
                SDL_RunHapticEffect(activeHaptic, periodicEffect, 1);
        }
        previousVibration = vibration;
    }
    else if ((activeFeatures & SDL_HAPTIC_LEFTRIGHT) && vibration > 0.001f)
        sdlFfbRumble(vibration, vibration, state->vibrationDurationMs);
}

/*
 * Every SDL haptic call runs here rather than on the thread that owns the
 * game's steering setters.
 *
 * A DirectInput parameter change is a driver round trip, and the game restates
 * its steering often enough that the three condition effects were costing
 * around eighty six updates a second on its own thread - enough frame time to
 * be visible while force feedback was active. The worker only ever applies the
 * newest state, so a burst of changes collapses into one update instead of
 * queueing up behind the driver.
 */
static SDL_Thread *steeringWorker;
static SDL_Mutex *steeringLock;
static SDL_Condition *steeringSignal;
static FfbSteeringState pendingSteering;
static int pendingValid;
static int workerRunning;
static void (*steeringPoll)(void);

void sdlFfbSetSteeringPoll(void (*poll)(void))
{
    steeringPoll = poll;
}

static int SDLCALL steeringWorkerMain(void *unused)
{
    (void)unused;

    for (;;)
    {
        FfbSteeringState state;

        int havePending;

        SDL_LockMutex(steeringLock);
        while (workerRunning && !pendingValid)
        {
            /*
             * Time out rather than sleeping indefinitely so the poller below
             * still runs when the game has gone quiet. It stops calling
             * clKickback's setters altogether between races, and the board's
             * own fields keep changing while it does - that is where the
             * release of the wheel comes from.
             */
            if (!SDL_WaitConditionTimeout(steeringSignal, steeringLock, 50))
                break;
        }

        havePending = pendingValid;
        if (!workerRunning)
        {
            SDL_UnlockMutex(steeringLock);
            break;
        }

        state = pendingSteering;
        pendingValid = 0;
        SDL_UnlockMutex(steeringLock);

        if (havePending)
            applySteeringNow(&state);
        else if (steeringPoll)
            steeringPoll();
    }

    return 0;
}

static void startSteeringWorker(void)
{
    if (steeringWorker)
        return;

    steeringLock = SDL_CreateMutex();
    steeringSignal = SDL_CreateCondition();
    if (!steeringLock || !steeringSignal)
    {
        log_warn("FFB: no worker thread, steering will run on the game thread");
        return;
    }

    workerRunning = 1;
    steeringWorker = SDL_CreateThread(steeringWorkerMain, "pacloader-ffb", NULL);
    if (!steeringWorker)
    {
        workerRunning = 0;
        log_warn("FFB: worker thread failed to start: %s", SDL_GetError());
    }
}

static void stopSteeringWorker(void)
{
    if (!steeringWorker)
        return;

    SDL_LockMutex(steeringLock);
    workerRunning = 0;
    SDL_SignalCondition(steeringSignal);
    SDL_UnlockMutex(steeringLock);

    SDL_WaitThread(steeringWorker, NULL);
    steeringWorker = NULL;

    SDL_DestroyCondition(steeringSignal);
    SDL_DestroyMutex(steeringLock);
    steeringSignal = NULL;
    steeringLock = NULL;
}

void sdlFfbApplySteering(const FfbSteeringState *state)
{
    if (!state || !activeHaptic)
        return;

    /*
     * The game restates its whole steering state from every one of clKickback's
     * setters, so most calls carry nothing new - measured at better than nine
     * in ten. Dropping those here keeps them off the worker entirely.
     */
    if (steeringApplied && memcmp(&lastSteering, state, sizeof(*state)) == 0)
    {
        steeringSkips++;
        return;
    }
    lastSteering = *state;
    steeringApplied = 1;

    if (!steeringWorker)
    {
        applySteeringNow(state);
        return;
    }

    SDL_LockMutex(steeringLock);
    pendingSteering = *state;
    pendingValid = 1;
    SDL_SignalCondition(steeringSignal);
    SDL_UnlockMutex(steeringLock);
}

void sdlFfbShutdown(void)
{
    stopSteeringWorker();

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
