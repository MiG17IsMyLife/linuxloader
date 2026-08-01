#include "n2Jvio.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "n2.h"
#include "../../../config/config.h"
#include "../../../log/log.h"
#include "../../lindbergh/jvs.h"

/*
 * The game writes a whole JVS request in one write() and then polls read()
 * until the reply turns up, so the slave runs inline on the writing thread and
 * the answer is already queued by the time the first read() happens.  No
 * background thread is involved and nothing here ever blocks.
 */

namespace
{
constexpr int jvioDescriptor = 0x7203;
constexpr char jvioDevicePath[] = "/dev/ttyM3";
// A reply the game never collects must not grow without bound.
constexpr size_t maximumQueuedBytes = 8 * 1024;

std::mutex bufferMutex;
std::deque<uint8_t> requestBytes; // game -> slave, still escaped
std::deque<uint8_t> replyBytes;   // slave -> game, already escaped
bool opened = false;

/*
 * The wheel and pedals are 16 bit and the game stores its calibration as raw
 * counts, so what goes on the wire is the count the cabinet's potentiometer
 * would report rather than a plain fraction of full scale.  n2AnalogueCount()
 * owns that conversion for both this path and the direct-write one.
 */
constexpr int analogueChannels[] = {ANALOGUE_1, ANALOGUE_2, ANALOGUE_3};

int calibratedCount(const JVSIO *io, size_t index)
{
    const int maximum = io->analogueMax > 0 ? io->analogueMax : 1;
    int raw = io->state.analogueChannel[analogueChannels[index]];
    raw = std::max(0, std::min(raw, maximum));

    const float normalised = static_cast<float>(raw) / static_cast<float>(maximum);
    return n2AnalogueCount(static_cast<int>(index), normalised);
}

/*
 * Length in raw, still escaped bytes to take off the head of the buffer, or 0
 * while more input is needed.  `complete` tells the two cases apart: it is set
 * only when the span really is a whole request, and left clear for line noise
 * ahead of a frame and for a truncated frame that a fresh SYNC has already
 * overtaken, both of which the caller drops rather than parses.
 */
size_t framedLength(const std::deque<uint8_t> &bytes, bool &complete)
{
    complete = false;
    if (bytes.empty())
        return 0;
    if (bytes.front() != SYNC)
        return 1;

    size_t raw = 1;      // the SYNC byte itself
    size_t decoded = 0;  // destination, length, then that many more bytes
    size_t needed = 2;   // revised as soon as the length byte is known

    while (decoded < needed)
    {
        if (raw >= bytes.size())
            return 0;

        uint8_t value = bytes[raw];
        if (value == SYNC)
            return raw; // a new frame began, so this one will never complete

        if (value == ESCAPE)
        {
            if (raw + 1 >= bytes.size())
                return 0;
            value = static_cast<uint8_t>(bytes[raw + 1] + 1);
            raw += 2;
        }
        else
        {
            raw += 1;
        }

        decoded += 1;
        if (decoded == 2)
            needed = 2 + static_cast<size_t>(value); // data bytes plus checksum
    }

    complete = true;
    return raw;
}

// Runs with bufferMutex released; processPacket() takes its own lock.
void answerRequest(const uint8_t *frame, size_t length)
{
    if (length > sizeof(inputBuffer))
    {
        log_warn("Namco N2 JVS: dropped a %zu byte request that cannot fit the packet buffer",
                 length);
        return;
    }

    /*
     * Kept at trace level for the manufacturer specific command, which is still
     * undecoded: the game sends 0x70 as "E0 01 07 70 18 50 4C <a> <b> <sum>",
     * built by n2JvioTxPl and reached from clKickback::sendJVS(). It is not the
     * steering board's own conversation - that runs over /dev/ttyM1, see
     * n2Kickback.h - so it is most likely the cabinet lamps, which clKickback
     * also owns through setLampView() and setLampIntrude().
     */
    char hex[3 * 48 + 4] = {};
    size_t written = 0;
    for (size_t i = 0; i < length && written + 4 < sizeof(hex); i++)
        written += static_cast<size_t>(std::snprintf(hex + written, sizeof(hex) - written,
                                                     "%02X ", frame[i]));
    log_trace("Namco N2 JVS: request %s", hex);

    JVSIO *io = getJVSIO();
    constexpr size_t calibrationCount =
        sizeof(analogueChannels) / sizeof(analogueChannels[0]);

    /*
     * Publish the cabinet's own reading of the panel for the duration of the
     * exchange only.  SDL keeps writing plain 0..analogueMax values and the
     * logical switch state into the same JVSIO, so leaving the translated
     * values behind would let a second request scale an already scaled reading
     * or latch a gear that has since been shifted out of.
     */
    int savedAnalogue[calibrationCount] = {};
    for (size_t i = 0; i < calibrationCount; i++)
    {
        savedAnalogue[i] = io->state.analogueChannel[analogueChannels[i]];
        io->state.analogueChannel[analogueChannels[i]] = calibratedCount(io, i);
    }

    /*
     * The sequential GearUp/GearDown bindings have no switches of their own on
     * the cabinet; they drive the same four shifter switches.  Only the bits
     * this overlay actually introduced are taken back out again: SDL reports
     * switches as edges rather than as a level every poll, so restoring the
     * whole word would drop any button pressed while the packet was answered.
     */
    const int gearOverlay =
        n2GearSwitchBits(n2UpdateShifter()) & ~io->state.inputSwitch[PLAYER_1];
    io->state.inputSwitch[PLAYER_1] |= gearOverlay;

    /*
     * readPacket() trusts the length byte it finds and has no end of buffer
     * check, so the bytes past this request are cleared rather than left as
     * whatever the previous one put there.
     */
    std::memcpy(inputBuffer, frame, length);
    std::memset(inputBuffer + length, 0, sizeof(inputBuffer) - length);
    int packetSize = 0;
    const JVSStatus status = processPacket(&packetSize);

    io->state.inputSwitch[PLAYER_1] &= ~gearOverlay;
    for (size_t i = 0; i < calibrationCount; i++)
        io->state.analogueChannel[analogueChannels[i]] = savedAnalogue[i];

    if (status != JVS_STATUS_SUCCESS || packetSize <= 0)
        return;

    /*
     * A reset is a broadcast that no board answers; replying to it would leave
     * a stray packet in front of the address assignment the master sends next.
     */
    if (inputPacket.data[0] == CMD_RESET)
        return;

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (replyBytes.size() + static_cast<size_t>(packetSize) > maximumQueuedBytes)
        replyBytes.clear();
    replyBytes.insert(replyBytes.end(), outputBuffer, outputBuffer + packetSize);
}

void consumeRequests()
{
    for (;;)
    {
        std::vector<uint8_t> frame;
        bool complete = false;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            const size_t length = framedLength(requestBytes, complete);
            if (length == 0)
                return;

            if (complete)
                frame.assign(requestBytes.begin(), requestBytes.begin() + length);
            requestBytes.erase(requestBytes.begin(), requestBytes.begin() + length);
        }

        if (complete && !frame.empty())
            answerRequest(frame.data(), frame.size());
    }
}
} // namespace

extern "C" int n2JvioSerialEnabled(void)
{
    return getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 && n2IsWanganTitle();
}

extern "C" int n2JvioSerialOpen(const char *path, int)
{
    if (!n2JvioSerialEnabled() || !path || std::strcmp(path, jvioDevicePath) != 0)
        return -1;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        requestBytes.clear();
        replyBytes.clear();
    }

    if (!opened)
    {
        opened = true;
        log_info("Namco N2 JVS: %s answered by the loader's JVS slave (%s)",
                 jvioDevicePath, getJVSIO()->capabilities.name);
    }
    return jvioDescriptor;
}

extern "C" int n2JvioSerialIsDescriptor(int fd)
{
    // Every read, write and close in the game passes through here, so compare
    // the descriptor before asking the configuration anything.
    return fd == jvioDescriptor && n2JvioSerialEnabled();
}

extern "C" int n2JvioSerialBytesAvailable(int fd)
{
    if (!n2JvioSerialIsDescriptor(fd))
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(replyBytes.size());
}

extern "C" int n2JvioSerialRead(int fd, void *buffer, size_t count)
{
    if (!n2JvioSerialIsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (replyBytes.empty())
    {
        // The port is opened O_NONBLOCK, so an idle line is EAGAIN rather than
        // a short read the master would treat as a dead board.
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = count < replyBytes.size() ? count : replyBytes.size();
    uint8_t *out = static_cast<uint8_t *>(buffer);
    for (size_t i = 0; i < taken; i++)
        out[i] = replyBytes[i];
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + taken);
    return static_cast<int>(taken);
}

extern "C" int n2JvioSerialWrite(int fd, const void *buffer, size_t count)
{
    if (!n2JvioSerialIsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        const uint8_t *in = static_cast<const uint8_t *>(buffer);
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (requestBytes.size() + count > maximumQueuedBytes)
                requestBytes.clear();
            requestBytes.insert(requestBytes.end(), in, in + count);
        }
        consumeRequests();
    }
    return static_cast<int>(count);
}

extern "C" int n2JvioSerialClose(int fd)
{
    if (!n2JvioSerialIsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    requestBytes.clear();
    replyBytes.clear();
    return 0;
}

extern "C" int n2JvioSerialIoctl(int fd, unsigned long request, void *argument)
{
    if (!n2JvioSerialIsDescriptor(fd))
        return -1;

    constexpr unsigned long linuxFionread = 0x541B;
    if (request == linuxFionread && argument)
    {
        *static_cast<int *>(argument) = n2JvioSerialBytesAvailable(fd);
        return 0;
    }

    // The rest are termios and RS485 line settings that a queue does not need.
    return 0;
}

#endif
