#include "n2SteeringIo.h"
#include "n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstring>
#include <algorithm>
#include <mutex>

#include "../../../config/config.h"
#include "../../ffb/sdlFfbBackend.h"
#include "../../../log/log.h"
#include "n2Kickback.h"

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

// clKickback::sm_default_center, and what it takes to stretch a ten bit
// position across a signed sixteen bit axis: 512 either side of centre becomes
// 32768 either side of it.
constexpr int kickbackCentre = 511;
constexpr int kickbackCentreScale = 64;

// clKickback::sm_instance, resolved once the hooks go in.
void *const *kickbackInstance = nullptr;
// Mirrors what clKickback::init leaves behind: torque type 1 at half scale,
// and no ramp having muted anything yet.
N2SteeringOutputState outputState = {
    /*center*/ kickbackCentre, /*centerOffset*/ 0, /*spring*/ 0, /*viscosity*/ 0,
    /*reflection*/ 0, /*reflectionStrength*/ 0.0f, /*vibrationStrength*/ 0.0f,
    /*vibrationPeriod*/ 0, /*vibrationDuration*/ 0,
    /*torqueEnabled*/ 1, /*powerType*/ 1, /*powerScale*/ 0.5f,
    /*motorRunning*/ 0};
N2SteeringOutputBackend outputBackend = {};
std::mutex outputMutex;
unsigned long diagnosticSequence = 0;

void applySdlSteering(const N2SteeringOutputState *state, void *)
{
    const float scale = state->powerType != 0 ? state->powerScale : 0.0f;
    FfbSteeringState translated = {};

    /*
     * The board works in ten bits, not sixteen.
     *
     * clKickback::setCenter masks its argument with 0x3ff and
     * clKickback::sm_default_center is 0x1ff, so a wheel position runs 0..1023
     * about a centre of 511 - the same figure the ten byte torque field uses.
     * Treating it as an unsigned sixteen bit value about 32768 put the spring's
     * centre at the far left of the axis, where it can only pull.
     */
    const int centred = (int)state->center - kickbackCentre + state->centerOffset;
    translated.center = std::clamp(centred * kickbackCentreScale, -32768, 32767);

    /*
     * Coefficients run to 127, not 255. Halving every force was most of why the
     * wheel felt light.
     */
    translated.enabled = state->torqueEnabled && state->powerType != 0 &&
                         state->motorRunning;
    translated.springStrength = (float)state->spring / 127.0f * scale;
    translated.damperStrength = (float)state->viscosity / 127.0f * scale;
    translated.constantForce = (state->reflection != 0
                                   ? (float)state->reflection / 127.0f
                                   : state->reflectionStrength) * scale;
    translated.vibrationStrength = state->vibrationStrength * scale;
    translated.vibrationPeriodMs = state->vibrationPeriod;
    translated.vibrationDurationMs = state->vibrationDuration;

    /*
     * Say when output starts and stops. The game's own scene messages are in
     * the same log, so this is what tells us which screens are meant to have
     * force on them without anyone having to time it by hand.
     */
    static int wasEnabled = -1;
    if (translated.enabled != wasEnabled)
    {
        wasEnabled = translated.enabled;
        log_info("N2 FFB: output %s (torque=%d motor=%d type=%d spring=%u damper=%u)",
                 translated.enabled ? "ON" : "OFF", state->torqueEnabled,
                 state->motorRunning, state->powerType, state->spring,
                 state->viscosity);
    }

    sdlFfbApplySteering(&translated);
}

void shutdownSdlSteering(void *)
{
    sdlFfbStopSteering();
}

/*
 * Re-read the board while the game is quiet.
 *
 * Between races the game stops calling clKickback's setters entirely, so
 * nothing pushes an update - but offTrq has meanwhile zeroed the board's spring
 * and viscosity, and that release has to reach the wheel or it stays stiff all
 * the way through the attract screen. Runs on the force feedback worker, which
 * is off the game thread; it only reads the board's fields and takes the same
 * lock every other update does.
 */
void pollBoard(void);

/*
 * Read the board's own fields rather than trusting what came through the
 * setters.
 *
 * clKickback::onTrq and offTrq write spring and viscosity straight into the
 * object - 0x3f when torque starts, zero when it stops - without going near
 * setSpring or setViosity. Watching only the setters meant the release never
 * arrived, so the last values from a race stayed applied and the wheel was
 * still stiff back on the attract screen. The offsets come from the
 * disassembly of those two and of clKickback::init.
 *
 * Caller holds outputMutex.
 */
void refreshFromBoard(N2SteeringOutputState &state)
{
    if (!kickbackInstance || !*kickbackInstance)
        return;

    const uint8_t *fields = static_cast<const uint8_t *>(*kickbackInstance);

    int center = 0;
    std::memcpy(&center, fields + 0x18, sizeof(center));
    std::memcpy(&state.centerOffset, fields + 0x20, sizeof(state.centerOffset));
    std::memcpy(&state.powerType, fields + 0x14, sizeof(state.powerType));
    std::memcpy(&state.powerScale, fields + 0x10, sizeof(state.powerScale));

    state.center = (uint16_t)(center & 0x3ff);
    state.spring = fields[0x3d];
    state.viscosity = fields[0x3e];
    state.reflection = (int8_t)fields[0x3f];
}

template <typename Update>
void updateOutput(Update update)
{
    N2SteeringOutputState state;
    N2SteeringOutputBackend backend;
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        update(outputState);
        refreshFromBoard(outputState);
        state = outputState;
        backend = outputBackend;
    }
    // Never invoke a future device backend while holding the state lock.
    if (getConfig()->namcoN2.forceFeedbackEnabled && backend.apply)
        backend.apply(&state, backend.userData);
    if (getConfig()->namcoN2.forceFeedbackDiagnostics &&
        (++diagnosticSequence == 1 || diagnosticSequence % 120 == 0))
    {
        log_info("N2 FFB[%lu]: torque=%d type=%d scale=%.2f center=%u offset=%d "
                 "spring=%u damper=%u reflection=%d/%.3f vibration=%.3f "
                 "period=%d duration=%d",
                 diagnosticSequence, state.torqueEnabled, state.powerType,
                 state.powerScale, state.center, state.centerOffset,
                 state.spring, state.viscosity, state.reflection,
                 state.reflectionStrength, state.vibrationStrength,
                 state.vibrationPeriod, state.vibrationDuration);
    }
}

/*
 * Lifecycle tracing.
 *
 * The nine setters above are the only things that stage a frame for
 * clKickback::send(), and a boot with force feedback switched on produced no
 * frames at all on /dev/ttyM1 even though clSerialN2::open() reported success.
 * That puts the stall somewhere in the board's own start-up sequence rather
 * than in the reply this loader gives, so trace that sequence before changing
 * any of it.  The offsets are read out of the disassembly of clKickback::init()
 * and clKickback::send(): 0x24 is set by every setter to mean "a frame is
 * staged", 0x33 means "not yet transmitted", and 0x6c..0x6e hold the three
 * character error code getPCBError() reports.
 */
using KickbackCall = int (*)(void *);
using KickbackInit = int (*)(void *, unsigned);

KickbackInit originalInit = nullptr;
KickbackCall originalCreate = nullptr;
KickbackCall originalOnPower = nullptr;
KickbackCall originalWaitOnPower = nullptr;
KickbackCall originalChkSelf = nullptr;
KickbackCall originalRequestSelfCheck = nullptr;
KickbackCall originalWaitSelfCheck = nullptr;
KickbackCall originalSend = nullptr;
KickbackCall originalReceive = nullptr;
KickbackCall originalOffPower = nullptr;
using KickbackSetInt = int (*)(void *, int);
KickbackSetInt originalSetTrqPowerType = nullptr;

void traceState(const char *what, const void *object, int result)
{
    if (!getConfig()->namcoN2.forceFeedbackDiagnostics || !object)
        return;

    const uint8_t *fields = static_cast<const uint8_t *>(object);
    char error[4] = {};
    for (int i = 0; i < 3; ++i)
    {
        const uint8_t byte = fields[0x6c + i];
        error[i] = (byte >= 0x20 && byte < 0x7f) ? (char)byte : '.';
    }

    int state = 0;
    std::memcpy(&state, fields + 0x2c, sizeof(state));
    log_info("N2 kickback: %s -> %d | staged=%u pending=%u state=%d "
             "flag30=%d flag34=%u ffb=%u err=\"%s\"",
             what, result, fields[0x24], fields[0x33], state,
             (int)(int8_t)fields[0x30], fields[0x34], fields[0x71], error);
}

// exec() and manage() run every frame, so they are summarised rather than
// logged one by one; the first call is what shows the sequence got that far.
void traceRepeating(const char *what, const void *object, int result,
                    unsigned long &counter)
{
    if (++counter == 1 || counter % 600 == 0)
        traceState(what, object, result);
}

unsigned long sendCalls = 0;
unsigned long receiveCalls = 0;
unsigned long manageCalls = 0;
unsigned long execCalls = 0;

/*
 * clKickback::manage() and exec() are the two entry points that were never
 * hooked, and between them they hold six of the eight places that write the
 * "E20" the cabinet reports. Both run every frame, so they are summarised - but
 * the moment either latches the error, say so immediately: that is the exact
 * frame the cause has to be read from, and everything else here has only ever
 * shown the state before or after it.
 */
KickbackCall originalManage = nullptr;
KickbackCall originalExec = nullptr;
char lastReportedError[4] = {};

void watchForError(const char *what, void *object)
{
    if (!object)
        return;

    const uint8_t *fields = static_cast<const uint8_t *>(object);
    char error[4] = {};
    for (int i = 0; i < 3; ++i)
    {
        const uint8_t byte = fields[0x6c + i];
        error[i] = (byte >= 0x20 && byte < 0x7f) ? (char)byte : '.';
    }

    if (std::strcmp(error, lastReportedError) == 0)
        return;
    std::memcpy(lastReportedError, error, sizeof(error));

    if (error[0] == 'E' && !(error[1] == '0' && error[2] == '0'))
        log_warn("Namco N2 steering: %s latched error \"%s\"", what, error);
    traceState(what, object, 0);
}

int traceManage(void *object)
{
    const int result = originalManage(object);
    watchForError("manage", object);
    traceRepeating("manage", object, result, manageCalls);
    return result;
}

int traceExec(void *object)
{
    const int result = originalExec(object);
    watchForError("exec", object);
    traceRepeating("exec", object, result, execCalls);
    return result;
}

int traceInit(void *object, unsigned argument)
{
    const int result = originalInit(object, argument);
    traceState("init", object, result);
    return result;
}

int traceCreate(void *object)
{
    const int result = originalCreate(object);
    traceState("create", object, result);
    return result;
}

/*
 * Functional, not trace hooks. The board reports the motor running or stopped,
 * and the game blocks in waitOnPower/waitOffPower until it hears the matching
 * one, so each power transition has to put its report on the wire.
 */
int traceOnPower(void *object)
{
    const int result = originalOnPower(object);
    traceState("onPower", object, result);
    n2KickbackReportMotorPower(1);
    updateOutput([](N2SteeringOutputState &s) { s.motorRunning = 1; });
    return result;
}

int offPowerHook(void *object)
{
    const int result = originalOffPower(object);
    traceState("offPower", object, result);
    n2KickbackReportMotorPower(0);
    updateOutput([](N2SteeringOutputState &s) { s.motorRunning = 0; });
    return result;
}

/*
 * Both of these spin inside the frame until the board has reported the motor in
 * the state they are waiting for, so the report has to be up before the loop
 * starts rather than in reaction to anything it does. waitOffPower was never
 * hooked at all, and it is the one that runs after a race: without an answer it
 * never returns, which is why the game kept rendering at sixty while its own
 * scene progression stopped at the game over screen.
 */
int traceWaitOnPower(void *object)
{
    n2KickbackReportMotorPower(1);
    const int result = originalWaitOnPower(object);
    traceState("waitOnPower", object, result);
    updateOutput([](N2SteeringOutputState &s) { s.motorRunning = 1; });
    return result;
}

KickbackCall originalWaitOffPower = nullptr;

int waitOffPowerHook(void *object)
{
    n2KickbackReportMotorPower(0);
    const int result = originalWaitOffPower(object);
    traceState("waitOffPower", object, result);

    /*
     * The wait is what the game actually calls - offPower itself was never seen
     * - so the output gate has to move here too. Reporting the stop only to the
     * board left this loader still applying the last race's spring, and the
     * wheel went on centring itself all through the attract screen.
     */
    updateOutput([](N2SteeringOutputState &s) { s.motorRunning = 0; });
    return result;
}

int traceChkSelf(void *object)
{
    const int result = originalChkSelf(object);
    traceState("chkSelf", object, result);
    return result;
}

/*
 * Ground truth for the whole exchange: which three bytes the game actually
 * collected, whether it accepted them, and what they did to the state field
 * every wait in clKickback keys off. Everything else here is inference from the
 * disassembly; this is the game telling us directly.
 */
using DecodeReply = int (*)(void *, const unsigned char *);
DecodeReply originalDecodeReply = nullptr;

int traceDecodeReply(void *object, const unsigned char *reply)
{
    int before = -1;
    if (object)
        std::memcpy(&before, static_cast<const uint8_t *>(object) + 0x2c, sizeof(before));

    const int result = originalDecodeReply(object, reply);

    if (getConfig()->namcoN2.forceFeedbackDiagnostics && object && reply)
    {
        int after = 0;
        std::memcpy(&after, static_cast<const uint8_t *>(object) + 0x2c, sizeof(after));
        if (after != before)
            log_info("N2 kickback: reply \"%c%c%c\" accepted=%d state %d -> %d",
                     reply[0], reply[1], reply[2], result, before, after);
    }

    return result;
}

/*
 * The self check request is bit 7 of clKickback's this->0x30, the byte
 * clKickback::sendJVS() publishes to the JVS general purpose output. Answering
 * it is n2Kickback's job now, from the read path, because none of the hooks
 * here keep running long enough to be relied on: the game stops calling its own
 * setters once a race is over, while clKickback::manage() carries on counting
 * unanswered requests until it reports E20.
 */
int answerSelfCheck(void *object)
{
    const int result = originalRequestSelfCheck(object);
    traceState("requestSelfCheck", object, result);
    return result;
}

/*
 * Functional, not a trace hook: waitSelfCheck() re-raises the request bit
 * itself every sixty frames while it waits, so this is where a retry is seen.
 */
int traceWaitSelfCheck(void *object)
{
    const int result = originalWaitSelfCheck(object);
    traceState("waitSelfCheck", object, result);
    return result;
}

int traceSend(void *object)
{
    const int result = originalSend(object);
    traceRepeating("send", object, result, sendCalls);
    return result;
}

int traceReceive(void *object)
{
    const int result = originalReceive(object);
    traceRepeating("receive", object, result, receiveCalls);
    return result;
}

/*
 * The master switch, not a trace hook. clKickback clamps the type to 0..2 and
 * looks the matching scale up in its own table - 0.0, 0.5, 1.0 - so read both
 * back out of the object rather than repeating the table here.
 */
int setTrqPowerType(void *object, int type)
{
    const int result = originalSetTrqPowerType(object, type);

    int applied = type;
    float scale = 0.0f;
    if (object)
    {
        const uint8_t *fields = static_cast<const uint8_t *>(object);
        std::memcpy(&applied, fields + 0x14, sizeof(applied));
        std::memcpy(&scale, fields + 0x10, sizeof(scale));
    }

    log_info("Namco N2 steering: torque power type %d (scale %.2f)", applied, scale);
    updateOutput([applied, scale](N2SteeringOutputState &state) {
        state.powerType = applied;
        state.powerScale = scale;
    });
    return result;
}

void installTraceHooks(void)
{
    n2HookSymbolWithOriginal("_ZN10clKickback4initEj",
                             reinterpret_cast<void *>(traceInit),
                             reinterpret_cast<void **>(&originalInit));
    n2HookSymbolWithOriginal("_ZN10clKickback6createEv",
                             reinterpret_cast<void *>(traceCreate),
                             reinterpret_cast<void **>(&originalCreate));
    n2HookSymbolWithOriginal("_ZN10clKickback7chkSelfEv",
                             reinterpret_cast<void *>(traceChkSelf),
                             reinterpret_cast<void **>(&originalChkSelf));
    n2HookSymbolWithOriginal("_ZN10clKickback4sendEv",
                             reinterpret_cast<void *>(traceSend),
                             reinterpret_cast<void **>(&originalSend));
    n2HookSymbolWithOriginal("_ZN10clKickback7receiveEv",
                             reinterpret_cast<void *>(traceReceive),
                             reinterpret_cast<void **>(&originalReceive));
    n2HookSymbolWithOriginal("_ZN10clKickback16decordResultCodeEPKh",
                             reinterpret_cast<void *>(traceDecodeReply),
                             reinterpret_cast<void **>(&originalDecodeReply));
    n2HookSymbolWithOriginal("_ZN10clKickback6manageEv",
                             reinterpret_cast<void *>(traceManage),
                             reinterpret_cast<void **>(&originalManage));
    n2HookSymbolWithOriginal("_ZN10clKickback4execEv",
                             reinterpret_cast<void *>(traceExec),
                             reinterpret_cast<void **>(&originalExec));
}

void pollBoard(void)
{
    updateOutput([](N2SteeringOutputState &) {});
}

void setSpring(void *object, uint8_t value)
{
    originalSetSpring(object, value);
    // One setter runs often enough to keep the board answering while the game
    // is otherwise idle; the rest do not need to repeat the check.
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
    traceState("onTrq", object, immediate);
    updateOutput([](N2SteeringOutputState &state) { state.torqueEnabled = 1; });
}

void offTorque(void *object, bool immediate)
{
    originalOffTorque(object, immediate);
    traceState("offTrq", object, immediate);
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
    installed += n2HookSymbolWithOriginal("_ZN10clKickback16requestSelfCheckEv",
                      reinterpret_cast<void *>(answerSelfCheck),
                      reinterpret_cast<void **>(&originalRequestSelfCheck));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback15setTRQPowerTypeEi",
                      reinterpret_cast<void *>(setTrqPowerType),
                      reinterpret_cast<void **>(&originalSetTrqPowerType));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback13waitSelfCheckEv",
                      reinterpret_cast<void *>(traceWaitSelfCheck),
                      reinterpret_cast<void **>(&originalWaitSelfCheck));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback7onPowerEv",
                      reinterpret_cast<void *>(traceOnPower),
                      reinterpret_cast<void **>(&originalOnPower));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback8offPowerEv",
                      reinterpret_cast<void *>(offPowerHook),
                      reinterpret_cast<void **>(&originalOffPower));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback11waitOnPowerEv",
                      reinterpret_cast<void *>(traceWaitOnPower),
                      reinterpret_cast<void **>(&originalWaitOnPower));
    installed += n2HookSymbolWithOriginal("_ZN10clKickback12waitOffPowerEv",
                      reinterpret_cast<void *>(waitOffPowerHook),
                      reinterpret_cast<void **>(&originalWaitOffPower));
    /*
     * Hand the board clKickback's singleton so it can read the self check
     * request line itself. Without it the board can only answer while one of
     * the hooks above happens to be running, and the game stops calling all of
     * them between races.
     */
    if (void *instance = n2ResolveSymbol("_ZN10clKickback11sm_instanceE"))
    {
        kickbackInstance = static_cast<void *const *>(instance);
        n2KickbackSetInstance(kickbackInstance);
    }
    else
        log_warn("Namco N2 steering: clKickback::sm_instance not found, the "
                 "board cannot see self check requests");

    log_info("Namco N2 steering: installed %d output hooks (FFB backend %s)",
             installed, getConfig()->namcoN2.forceFeedbackEnabled ? "enabled" : "disabled");

    if (getConfig()->namcoN2.forceFeedbackDiagnostics)
        installTraceHooks();

    const N2SteeringOutputBackend sdlBackend = {
        applySdlSteering,
        shutdownSdlSteering,
        nullptr
    };
    n2SteeringIoSetBackend(&sdlBackend);
    sdlFfbSetSteeringPoll(pollBoard);
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
