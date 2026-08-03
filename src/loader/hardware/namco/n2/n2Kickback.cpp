#include "n2Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "n2.h"
#include "../../common/jvs.h"
#include "../../../config/config.h"
#include "../../../log/log.h"

namespace
{
constexpr int kickbackDescriptor = 0x7201;
constexpr char kickbackDevicePath[] = "/dev/ttyM1";

// clKickback::send() hands the port exactly ten bytes at a time and then reads
// three back, so a frame is never split by the game itself; the resynchronise
// path only exists in case the port is opened mid-conversation.
constexpr size_t commandLength = 10;
constexpr uint8_t frameHeader = 0xFF;

/*
 * The one reply this bridge gives. "E00" is the no-error status report, the
 * form clKickback::getPCBError() checks byte for byte, and one of the two codes
 * that clear clKickback's this->0x2c - the field waitSelfCheck() blocks on.
 *
 * Answering every frame the same way is only good enough while the game has
 * barely started transmitting. Telling the request kinds apart and replying
 * "C0?" to acknowledgements is part of the work described in the header, and
 * this constant is the single place that changes when it is done.
 */
constexpr uint8_t healthyResult[3] = {'E', '0', '0'};

/*
 * The self check is answered in two steps, which is what clKickback::
 * waitSelfCheck() reads as "running" and then "finished".
 *
 * Its first phase waits for decordResultCode() to clear this->0x2c, which the
 * status report above does. Only then does it start its second countdown, and
 * that one ends when this->0x2c is exactly 1 - the value "C06" sets, and the
 * single code that makes waitSelfCheck() return success. Anything else leaves
 * it spinning until the countdown expires and the cabinet reports E20.
 */
constexpr uint8_t selfCheckComplete[3] = {'C', '0', '6'};

/*
 * The two power reports. "C01" is the motor running and "C06" is the motor
 * stopped, which is why decordResultCode() clears this->0x2c for the first and
 * sets it to one for the second, and why clKickback::send() only puts a torque
 * frame on the wire while it is zero: a stopped motor has nothing to drive.
 *
 * clKickback::waitOnPower() waits for the running report and waitOffPower() for
 * the stopped one, so answering only ever with "C06" leaves the game waiting
 * the moment it powers the motor back up - which is what stalled it after a
 * race.
 */
constexpr uint8_t motorRunning[3] = {'C', '0', '1'};
constexpr uint8_t motorStopped[3] = {'C', '0', '6'};

// A reply the game stops collecting must not grow without bound.
constexpr size_t maximumQueuedBytes = 1024;

std::mutex bufferMutex;
std::deque<uint8_t> commandBytes; // game -> board
std::deque<uint8_t> replyBytes;   // board -> game
bool opened = false;

/*
 * The board answers from the read path rather than from anything hooked on the
 * game's side, because the game stops calling clKickback's setters entirely
 * once a race is over while its own manage() keeps counting unanswered
 * requests - six hundred frames of them is E20. Reading the request line
 * straight out of the singleton keeps the board alive no matter which of the
 * game's own entry points happen to be running.
 */
void *const *kickbackInstance = nullptr;

// The motor is not turning until the game powers it, so the first onPower is a
// real transition and gets reported. Starting this the other way round made it
// a no-op, and the game waited for a report the board never sent.
bool motorPowered = false;

/*
 * Working out the request layout is the next step for force feedback, and the
 * frames are the evidence it needs, so they stay visible at debug level: the
 * first one, then every thousandth, which is a line every twenty seconds or so
 * at the rate the board runs.
 */
unsigned long framesSeen = 0;
unsigned long repliesTaken = 0;
unsigned long selfCheckRefills = 0;
unsigned long positionsQueued = 0;

void reportFrame(const uint8_t *frame)
{
    framesSeen++;
    if (framesSeen != 1 && framesSeen % 1000 != 0)
        return;

    char hex[3 * commandLength + 1] = {};
    size_t written = 0;
    for (size_t i = 0; i < commandLength && written + 4 < sizeof(hex); i++)
        written += static_cast<size_t>(
            std::snprintf(hex + written, sizeof(hex) - written, "%02X ", frame[i]));
    log_debug("Namco N2 steering: frame %lu from the game: %s", framesSeen, hex);
}

/*
 * The wheel position report.
 *
 * decordResultCode turns 'H' followed by a big endian ten bit value into
 * this->0x54, and sets this->0x34 to say a position has arrived. The test
 * menu's I/F INITIALIZE screen polls that flag: without it the screen shows
 * STEERING 000 and eventually gives up with PCB ERROR, which is exactly what it
 * was doing. Unlike the power reports this touches neither this->0x2c nor
 * anything waitSelfCheck reads, so it is safe to send whenever the board is
 * asked for something.
 *
 * The count comes from the same JVSIO the JVS path publishes, so the board and
 * the I/O board cannot disagree about where the wheel is.
 */
void queuePosition()
{
    const JVSIO *io = getJVSIO();
    const int maximum = io->analogueMax > 0 ? io->analogueMax : 1;
    int raw = io->state.analogueChannel[ANALOGUE_1];
    raw = raw < 0 ? 0 : (raw > maximum ? maximum : raw);

    // 0..1023 about a centre of 511, the same ten bits the torque field uses.
    int position = static_cast<int>(static_cast<long long>(raw) * 1023 / maximum);
    position = position < 0 ? 0 : (position > 1023 ? 1023 : position);

    const uint8_t frame[3] = {'H', static_cast<uint8_t>((position >> 8) & 0xff),
                              static_cast<uint8_t>(position & 0xff)};
    replyBytes.insert(replyBytes.end(), frame, frame + sizeof(frame));

    /*
     * The test menu's I/F INITIALIZE screen clears clKickback's this->0x34 and
     * then counts down waiting for a position report to set it again, so
     * whether these are being queued at all - and whether the game is reading
     * them - is the difference between that screen working and giving up with
     * PCB ERROR.
     */
    {
        const uint8_t *live =
            kickbackInstance ? static_cast<const uint8_t *>(*kickbackInstance) : nullptr;
        if (live && live[0x71] != 0)
        {
            int held = 0;
            std::memcpy(&held, live + 0x54, sizeof(held));
            log_info("Namco N2 steering: calibration window open - queued count %d, "
                     "flag34=%u held=%d",
                     position, live[0x34], held);
        }
    }

    if (++positionsQueued == 1 || positionsQueued % 600 == 0)
    {
        /*
         * this->0x34 is the flag the screen waits on and this->0x54 the value
         * it wants, both set by decordResultCode when it accepts an 'H'. Report
         * them next to what was queued: if the game is taking these replies and
         * the flag is still clear, they are not being parsed as position
         * reports, which is a different problem from not sending them.
         */
        const uint8_t *object =
            kickbackInstance ? static_cast<const uint8_t *>(*kickbackInstance) : nullptr;
        int accepted = -1;
        if (object)
            std::memcpy(&accepted, object + 0x54, sizeof(accepted));

        log_info("Namco N2 steering: queued %lu positions (count %d), game took "
                 "%lu replies, flag34=%d accepted=%d",
                 positionsQueued, position, repliesTaken,
                 object ? object[0x34] : -1, accepted);
    }
}

// Caller holds bufferMutex.
void refillReply()
{
    if (!replyBytes.empty())
        return;

    const uint8_t *object =
        kickbackInstance ? static_cast<const uint8_t *>(*kickbackInstance) : nullptr;

    /*
     * Bit 7 of clKickback's general purpose output byte is the self check
     * request, held rather than pulsed, so this is a level and not an edge.
     *
     * Nothing is said when it is low. A board that also reported its power
     * state continuously looked more faithful but broke the check outright:
     * "C01" clears this->0x2c, waitSelfCheck's second phase only finishes while
     * that field is 1, and a steady stream of them pinned it at 0 for three and
     * a half thousand polls. Power reports are transitions, and they are queued
     * by n2KickbackReportMotorPower where the transitions happen.
     */
    if (!object || (object[0x30] & 0x80) == 0)
    {
        // Nothing to acknowledge, so just say where the wheel is.
        queuePosition();
        return;
    }

    // The check wants the motor reported running and then stopped, the pair
    // decordResultCode turns into this->0x2c of 0 and then 1 - what
    // waitSelfCheck's two phases wait for, in that order.
    replyBytes.insert(replyBytes.end(), healthyResult,
                      healthyResult + sizeof(healthyResult));
    replyBytes.insert(replyBytes.end(), selfCheckComplete,
                      selfCheckComplete + sizeof(selfCheckComplete));
    queuePosition();

    if (++selfCheckRefills == 1 || selfCheckRefills % 300 == 0)
        log_info("Namco N2 steering: answered %lu self check polls (motor %s)",
                 selfCheckRefills, motorPowered ? "running" : "stopped");
}

void consumeCommands()
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    for (;;)
    {
        // Drop anything ahead of a frame header so a late open cannot leave the
        // stream permanently out of step.
        while (!commandBytes.empty() && commandBytes.front() != frameHeader)
            commandBytes.pop_front();

        if (commandBytes.size() < commandLength)
            return;

        uint8_t frame[commandLength];
        for (size_t i = 0; i < commandLength; i++)
            frame[i] = commandBytes[i];
        commandBytes.erase(commandBytes.begin(), commandBytes.begin() + commandLength);
        reportFrame(frame);

        if (replyBytes.size() + sizeof(healthyResult) > maximumQueuedBytes)
            replyBytes.clear();
        replyBytes.insert(replyBytes.end(), healthyResult,
                          healthyResult + sizeof(healthyResult));
    }
}
} // namespace

extern "C" int n2KickbackSerialEnabled(void)
{
    return getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 && n2IsWanganTitle();
}

extern "C" void n2KickbackSetInstance(void *const *instance)
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    kickbackInstance = instance;
}

extern "C" void n2KickbackReportMotorPower(int running)
{
    if (!n2KickbackSerialEnabled())
        return;

    bool changed;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        changed = motorPowered != (running != 0);
        motorPowered = running != 0;

        /*
         * Always put the report up, not just on a change.
         *
         * clKickback::waitOffPower spins inside the frame - pumping
         * clSystemN2::exec and going round again - until decordResultCode has
         * set this->0x2c to 1, which only "C06" does. waitOnPower is the mirror
         * of it. Neither loop ends unless the report is waiting when it starts,
         * and a board that only spoke on transitions left the game turning over
         * at sixty frames a second with its own scene progression stopped dead.
         */
        const uint8_t *report = motorPowered ? motorRunning : motorStopped;
        replyBytes.clear();
        replyBytes.insert(replyBytes.end(), report, report + 3);
    }

    if (changed)
        log_info("Namco N2 steering: motor now %s (\"%s\")",
                 running ? "running" : "stopped", running ? "C01" : "C06");
}

extern "C" int n2KickbackSerialOpen(const char *path, int)
{
    if (!n2KickbackSerialEnabled() || !path || std::strcmp(path, kickbackDevicePath) != 0)
        return -1;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        commandBytes.clear();
        replyBytes.clear();
    }

    if (!opened)
    {
        opened = true;
        log_info("Namco N2 steering: %s answered by the loader's kickback board",
                 kickbackDevicePath);
    }
    return kickbackDescriptor;
}

extern "C" int n2KickbackSerialIsDescriptor(int fd)
{
    // On the hot path for every read and write in the game, so the cheap test
    // comes first.
    return fd == kickbackDescriptor && n2KickbackSerialEnabled();
}

extern "C" int n2KickbackSerialBytesAvailable(int fd)
{
    if (!n2KickbackSerialIsDescriptor(fd))
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(replyBytes.size());
}

extern "C" int n2KickbackSerialRead(int fd, void *buffer, size_t count)
{
    if (!n2KickbackSerialIsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);

    // A real board is always saying something, so top the queue up here rather
    // than reporting an idle line: this is the one path the game keeps using
    // after it has stopped touching clKickback's setters.
    refillReply();

    if (replyBytes.empty())
    {
        // The port is non-blocking, so an idle line is EAGAIN rather than a
        // short read the board would be blamed for.
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = count < replyBytes.size() ? count : replyBytes.size();
    uint8_t *out = static_cast<uint8_t *>(buffer);
    for (size_t i = 0; i < taken; i++)
        out[i] = replyBytes[i];
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + taken);

    if (++repliesTaken == 1)
        log_debug("Namco N2 steering: the game collected its first %zu byte reply", taken);
    return static_cast<int>(taken);
}

extern "C" int n2KickbackSerialWrite(int fd, const void *buffer, size_t count)
{
    if (!n2KickbackSerialIsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        const uint8_t *in = static_cast<const uint8_t *>(buffer);
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (commandBytes.size() + count > maximumQueuedBytes)
                commandBytes.clear();
            commandBytes.insert(commandBytes.end(), in, in + count);
        }
        consumeCommands();
    }
    return static_cast<int>(count);
}

extern "C" int n2KickbackSerialClose(int fd)
{
    if (!n2KickbackSerialIsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    commandBytes.clear();
    replyBytes.clear();
    return 0;
}

extern "C" int n2KickbackSerialIoctl(int fd, unsigned long request, void *argument)
{
    if (!n2KickbackSerialIsDescriptor(fd))
        return -1;

    constexpr unsigned long linuxFionread = 0x541B;
    if (request == linuxFionread && argument)
    {
        *static_cast<int *>(argument) = n2KickbackSerialBytesAvailable(fd);
        return 0;
    }

    // The rest are termios and line settings a queue has no use for.
    return 0;
}

#endif
