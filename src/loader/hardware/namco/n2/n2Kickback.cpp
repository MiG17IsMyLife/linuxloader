#include "n2Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
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

// The three replies this board gives. "E00" and "C01" put this->0x2c at 0,
// "C06" at 1. The self check answer is "E00" then "C06".
constexpr uint8_t healthyResult[3] = {'E', '0', '0'};
constexpr uint8_t selfCheckComplete[3] = {'C', '0', '6'};
constexpr uint8_t motorRunning[3] = {'C', '0', '1'};
constexpr uint8_t motorStopped[3] = {'C', '0', '6'};

// A reply the game stops collecting must not grow without bound.
constexpr size_t maximumQueuedBytes = 1024;

std::mutex bufferMutex;
std::deque<uint8_t> commandBytes; // game -> board
std::deque<uint8_t> replyBytes;   // board -> game
bool opened = false;

// clKickback::sm_instance, so this->0x2c can be read whatever the game is doing.
void *const *kickbackInstance = nullptr;

// The motor is not turning until the game powers it, so the first onPower is a
// real transition.
bool motorPowered = false;

/*
 * Replies volunteered rather than owed - a power report, or the self check
 * answer. They wait for this->0x2c, so they cannot share a queue with the
 * acknowledgements, which must keep flowing meanwhile.
 */
std::deque<uint8_t> volunteeredReplies;

// The request layout past the 0xFF 0xFF header is still unknown, so the frames
// stay visible at debug level: the first, then every thousandth.
unsigned long framesSeen = 0;
unsigned long repliesTaken = 0;
unsigned long selfCheckRefills = 0;

// How many more reads to write out byte for byte. A window rather than a
// permanent trace, because the game reads sixty times a second. Opened where
// something interesting starts; caller holds bufferMutex.
unsigned traceWindow = 0;

const uint8_t *boardFields()
{
    return kickbackInstance ? static_cast<const uint8_t *>(*kickbackInstance) : nullptr;
}

int boardState()
{
    const uint8_t *fields = boardFields();
    if (!fields)
        return -1;

    int state = 0;
    std::memcpy(&state, fields + 0x2c, sizeof(state));
    return state;
}

bool boardTracing()
{
    return getConfig()->namcoN2.forceFeedbackDiagnostics;
}

/*
 * decordResultCode's own test, at 0x85aacc7 in the 3DX+ JP binary: a "C06"
 * arriving while this->0x2c is 0 and the self check bit is up is not ignored,
 * it sets this->0x2c to 4 and writes "E20". Nothing else is ever refused, and
 * only 3DX+ JP guards the branch at all - v337 and 3DX EN take "C06"
 * unconditionally, so there this only delays a reply they would have accepted.
 */
bool volunteeredReplyWelcome()
{
    // The self check's second reply is the same three bytes as the motor stop.
    if (volunteeredReplies.size() < sizeof(motorStopped) ||
        !std::equal(motorStopped, motorStopped + sizeof(motorStopped),
                    volunteeredReplies.begin()))
        return true;

    const uint8_t *fields = boardFields();
    if (!fields)
        return true;

    int state = 0;
    std::memcpy(&state, fields + 0x2c, sizeof(state));
    return !(state == 0 && (fields[0x30] & 0x80) != 0);
}

/*
 * The wheel position, for the test menu's I/F INITIALIZE screen only. It sets
 * this->0x71, clears this->0x34 and waits about sixty polls for a report to set
 * it again, twelve times over, drawing PCB ERROR if none arrives. Only an 'H'
 * sets that flag, and only with force feedback on does the screen take the
 * path at all.
 *
 * Both conditions gate it. Outside the window an 'H' answers a torque frame
 * without touching this->0x2c, so the request stays unanswered and send()
 * counts to E20; with this->0x34 already set, nothing is waiting for one and
 * more would only starve the acknowledgements.
 *
 * Caller holds bufferMutex.
 */
bool calibrationWindowOpen = false;

void stagePositionReport()
{
    const uint8_t *fields = boardFields();

    const bool open = fields && fields[0x71] != 0;
    if (open != calibrationWindowOpen)
    {
        calibrationWindowOpen = open;
        traceWindow = 60;
        log_info("Namco N2 steering: I/F INITIALIZE calibration window %s",
                 open ? "opened" : "closed");
    }

    if (!open || fields[0x34] != 0 || !volunteeredReplies.empty())
        return;

    const JVSIO *io = getJVSIO();
    const int maximum = io->analogueMax > 0 ? io->analogueMax : 1;
    int raw = io->state.analogueChannel[ANALOGUE_1];
    raw = raw < 0 ? 0 : (raw > maximum ? maximum : raw);

    // decordResultCode reads the two bytes as one big endian word, so the wheel
    // goes out at the width the JVS channel carries it.
    const unsigned position =
        (unsigned)((long long)raw * 0xffff / maximum) & 0xffff;
    const uint8_t report[3] = {'H', (uint8_t)(position >> 8), (uint8_t)position};
    volunteeredReplies.assign(report, report + sizeof(report));
}

// Replies are three printable characters, so show them as such rather than hex.
void describeReply(const uint8_t *bytes, size_t count, char *out, size_t size)
{
    size_t written = 0;
    for (size_t i = 0; i < count && written + 1 < size; i++)
    {
        const uint8_t byte = bytes[i];
        out[written++] = (byte >= 0x20 && byte < 0x7f) ? static_cast<char>(byte) : '.';
    }
    out[written] = '\0';
}

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

        // Acknowledge, always and only once: withholding this in any game state
        // silences the board into E20, and letting acknowledgements accumulate
        // leaves the game reading answers to frames it sent minutes ago. "E00"
        // also puts this->0x2c at 0, which is what waitOnPower wants.
        replyBytes.assign(healthyResult, healthyResult + sizeof(healthyResult));
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

extern "C" void n2KickbackReportSelfCheck(void)
{
    if (!n2KickbackSerialEnabled())
        return;

    std::lock_guard<std::mutex> lock(bufferMutex);

    // Healthy, then finished - this->0x2c of 0 and then 1, waitSelfCheck's two
    // phases in order. They go out one per opening, not back to back: "E00"
    // leaves the field at 0, where the "C06" would be refused.
    volunteeredReplies.clear();
    volunteeredReplies.insert(volunteeredReplies.end(), healthyResult,
                              healthyResult + sizeof(healthyResult));
    volunteeredReplies.insert(volunteeredReplies.end(), selfCheckComplete,
                              selfCheckComplete + sizeof(selfCheckComplete));

    traceWindow = 60;
    log_info("Namco N2 steering: self check %lu requested, answering \"E00\" "
             "then \"C06\" | state=%d",
             ++selfCheckRefills, boardState());
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

        // Every call, not just on a change: waitOffPower blocks until it reads
        // the "C06", and it will not go looking twice. Replaces rather than
        // queues, because the motor is only ever in one state.
        const uint8_t *report = motorPowered ? motorRunning : motorStopped;
        volunteeredReplies.assign(report, report + 3);
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
        volunteeredReplies.clear();
        traceWindow = 60;
    }

    // Logged on every open, not just the first: entering the test menu closes
    // this port and leaving it opens it again.
    if (!opened)
    {
        opened = true;
        log_info("Namco N2 steering: %s answered by the loader's kickback board",
                 kickbackDevicePath);
    }
    else
        log_info("Namco N2 steering: %s reopened | state=%d", kickbackDevicePath,
                 boardState());
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

    stagePositionReport();

    // Whichever the read would hand over. The acknowledgement is always on
    // offer; a volunteered reply only once this->0x2c will accept it.
    if (!volunteeredReplies.empty() && volunteeredReplyWelcome())
        return 3;
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

    stagePositionReport();

    // A volunteered reply goes first when this->0x2c will take it, otherwise
    // the acknowledgement does and it waits for the next read. Never both: the
    // game reads three bytes and decodes exactly one.
    const bool holding = !volunteeredReplies.empty() && !volunteeredReplyWelcome();
    std::deque<uint8_t> &source =
        (!volunteeredReplies.empty() && !holding) ? volunteeredReplies : replyBytes;

    if (source.empty())
    {
        // The port is non-blocking, so a board with nothing to say is EAGAIN
        // rather than a short read it would be blamed for.
        if (traceWindow && boardTracing())
        {
            traceWindow--;
            log_info("Namco N2 steering: %s | state=%d held=%zu",
                     holding ? "holding a report until this->0x2c will take it"
                             : "read with nothing to say",
                     boardState(), volunteeredReplies.size());
        }
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = count < source.size() ? count : source.size();
    uint8_t *out = static_cast<uint8_t *>(buffer);
    for (size_t i = 0; i < taken; i++)
        out[i] = source[i];
    source.erase(source.begin(), source.begin() + taken);

    // "state before" is this->0x2c as the previous reply left it, so reading
    // two of these lines in sequence shows whether decordResultCode accepted
    // the first: a "C06" followed by a state that is still not 1 was refused.
    if (traceWindow && boardTracing())
    {
        traceWindow--;
        char shown[16];
        describeReply(out, taken, shown, sizeof(shown));
        log_info("Namco N2 steering: gave the game \"%s\" (%s) | state before=%d "
                 "held=%zu",
                 shown, &source == &volunteeredReplies ? "report" : "ack",
                 boardState(), volunteeredReplies.size());
    }

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
    log_info("Namco N2 steering: %s closed | state=%d queued=%zu held=%zu",
             kickbackDevicePath, boardState(), replyBytes.size(),
             volunteeredReplies.size());
    commandBytes.clear();
    replyBytes.clear();
    volunteeredReplies.clear();
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
