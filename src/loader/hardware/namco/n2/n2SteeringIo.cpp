#include "n2SteeringIo.h"
#include "n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstring>
#include <algorithm>
#include <mutex>

#include "../../../config/config.h"
#include "../../ffb/sdlFfbBackend.h"
#include "../../../log/log.h"

namespace
{
using SetByte = void (*)(void *, uint8_t);
using SetWord = void (*)(void *, uint16_t);
using SetInt = void (*)(void *, int);
using SetReflect = void (*)(void *, int8_t);
using SetReflectEx = void (*)(void *, float, int);
using SetVibrate = void (*)(void *, float, int, int);
using SetTorque = void (*)(void *, bool);

SetByte originalSetSpring = nullptr;
SetWord originalSetCenter = nullptr;
SetByte originalSetViscosity = nullptr;
SetReflect originalSetReflect = nullptr;
SetReflectEx originalSetReflectEx = nullptr;
SetInt originalSetCenterOffset = nullptr;
SetVibrate originalSetVibrate = nullptr;
SetTorque originalOnTorque = nullptr;
SetTorque originalOffTorque = nullptr;
N2SteeringOutputState outputState = {};
N2SteeringOutputBackend outputBackend = {};
std::mutex outputMutex;
unsigned long diagnosticSequence = 0;

void applySdlSteering(const N2SteeringOutputState *state, void *)
{
    FfbSteeringState translated = {};
    translated.enabled = state->torqueEnabled;
    translated.center = std::clamp((int)state->center - 32768 + state->centerOffset,
                                   -32768, 32767);
    translated.springStrength = (float)state->spring / 255.0f;
    translated.damperStrength = (float)state->viscosity / 255.0f;
    translated.constantForce = state->reflection != 0
                                   ? (float)state->reflection / 127.0f
                                   : state->reflectionStrength;
    translated.vibrationStrength = state->vibrationStrength;
    translated.vibrationPeriodMs = state->vibrationPeriod;
    translated.vibrationDurationMs = state->vibrationDuration;
    sdlFfbApplySteering(&translated);
}

void shutdownSdlSteering(void *)
{
    sdlFfbStopSteering();
}

template <typename Update>
void updateOutput(Update update)
{
    N2SteeringOutputState state;
    N2SteeringOutputBackend backend;
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        update(outputState);
        state = outputState;
        backend = outputBackend;
    }
    // Never invoke a future device backend while holding the state lock.
    if (getConfig()->namcoN2.forceFeedbackEnabled && backend.apply)
        backend.apply(&state, backend.userData);
    if (getConfig()->namcoN2.forceFeedbackDiagnostics &&
        (++diagnosticSequence == 1 || diagnosticSequence % 120 == 0))
    {
        log_info("N2 FFB[%lu]: torque=%d center=%u offset=%d spring=%u damper=%u "
                 "reflection=%d/%.3f vibration=%.3f period=%d duration=%d",
                 diagnosticSequence, state.torqueEnabled, state.center,
                 state.centerOffset, state.spring, state.viscosity,
                 state.reflection, state.reflectionStrength,
                 state.vibrationStrength, state.vibrationPeriod,
                 state.vibrationDuration);
    }
}

void setSpring(void *object, uint8_t value)
{
    originalSetSpring(object, value);
    updateOutput([value](N2SteeringOutputState &state) { state.spring = value; });
}

void setCenter(void *object, uint16_t value)
{
    originalSetCenter(object, value);
    updateOutput([value](N2SteeringOutputState &state) { state.center = value; });
}

void setViscosity(void *object, uint8_t value)
{
    originalSetViscosity(object, value);
    updateOutput([value](N2SteeringOutputState &state) { state.viscosity = value; });
}

void setReflect(void *object, int8_t value)
{
    originalSetReflect(object, value);
    updateOutput([value](N2SteeringOutputState &state) { state.reflection = value; });
}

void setReflectEx(void *object, float strength, int duration)
{
    originalSetReflectEx(object, strength, duration);
    updateOutput([strength, duration](N2SteeringOutputState &state) {
        state.reflectionStrength = strength;
        state.vibrationDuration = duration;
    });
}

void setCenterOffset(void *object, int value)
{
    originalSetCenterOffset(object, value);
    updateOutput([value](N2SteeringOutputState &state) { state.centerOffset = value; });
}

void setVibrate(void *object, float strength, int period, int duration)
{
    originalSetVibrate(object, strength, period, duration);
    updateOutput([strength, period, duration](N2SteeringOutputState &state) {
        state.vibrationStrength = strength;
        state.vibrationPeriod = period;
        state.vibrationDuration = duration;
    });
}

void onTorque(void *object, bool immediate)
{
    originalOnTorque(object, immediate);
    updateOutput([](N2SteeringOutputState &state) { state.torqueEnabled = 1; });
}

void offTorque(void *object, bool immediate)
{
    originalOffTorque(object, immediate);
    updateOutput([](N2SteeringOutputState &state) { state.torqueEnabled = 0; });
}

}

extern "C" int n2SteeringIoInstallHooks(void)
{
    int installed = 0;
    installed += n2HookSymbolWithOriginal("_ZN10clKickback9setSpringEh",
                      reinterpret_cast<void *>(setSpring),
                      reinterpret_cast<void **>(&originalSetSpring));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback9setCenterEt",
                      reinterpret_cast<void *>(setCenter),
                      reinterpret_cast<void **>(&originalSetCenter));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback10setViosityEh",
                      reinterpret_cast<void *>(setViscosity),
                      reinterpret_cast<void **>(&originalSetViscosity));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback10setReflectEc",
                      reinterpret_cast<void *>(setReflect),
                      reinterpret_cast<void **>(&originalSetReflect));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback10setReflectEfi",
                      reinterpret_cast<void *>(setReflectEx),
                      reinterpret_cast<void **>(&originalSetReflectEx));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback15setCenterOffsetEi",
                      reinterpret_cast<void *>(setCenterOffset),
                      reinterpret_cast<void **>(&originalSetCenterOffset));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback10setVibrateEfii",
                      reinterpret_cast<void *>(setVibrate),
                      reinterpret_cast<void **>(&originalSetVibrate));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback5onTrqEb",
                      reinterpret_cast<void *>(onTorque),
                      reinterpret_cast<void **>(&originalOnTorque));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback6offTrqEb",
                      reinterpret_cast<void *>(offTorque),
                      reinterpret_cast<void **>(&originalOffTorque));
    log_info("Namco N2 steering: installed %d output hooks (FFB backend %s)",
             installed, getConfig()->namcoN2.forceFeedbackEnabled ? "enabled" : "disabled");

    const N2SteeringOutputBackend sdlBackend = {
        applySdlSteering,
        shutdownSdlSteering,
        nullptr
    };
    n2SteeringIoSetBackend(&sdlBackend);
    return installed > 0 ? 0 : 1;
}

extern "C" void n2SteeringIoSetBackend(const N2SteeringOutputBackend *backend)
{
    N2SteeringOutputBackend previous;
    N2SteeringOutputBackend current;
    N2SteeringOutputState state;
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        previous = outputBackend;
        outputBackend = backend ? *backend : N2SteeringOutputBackend{};
        current = outputBackend;
        state = outputState;
    }
    if (previous.shutdown)
        previous.shutdown(previous.userData);
    if (getConfig()->namcoN2.forceFeedbackEnabled && current.apply)
        current.apply(&state, current.userData);
}

extern "C" const N2SteeringOutputState *n2SteeringIoGetState(void)
{
    return &outputState;
}

#endif
