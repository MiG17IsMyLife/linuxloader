#include "n2Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "n2.h"
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
 * form clKickback::getPCBError() checks byte for byte.
 *
 * Answering every frame the same way is only good enough because a cabinet with
 * force feedback switched off never transmits. Telling the request kinds apart
 * and replying "C0?" to acknowledgements is part of the work described in the
 * header, and this constant is the single place that changes when it is done.
 */
constexpr uint8_t healthyResult[3] = {'E', '0', '0'};

// A reply the game stops collecting must not grow without bound.
constexpr size_t maximumQueuedBytes = 1024;

std::mutex bufferMutex;
std::deque<uint8_t> commandBytes; // game -> board
std::deque<uint8_t> replyBytes;   // board -> game
bool opened = false;

/*
 * Working out the request layout is the next step for force feedback, and the
 * frames are the evidence it needs, so they stay visible at debug level: the
 * first one, then every thousandth, which is a line every twenty seconds or so
 * at the rate the board runs.
 */
unsigned long framesSeen = 0;
unsigned long repliesTaken = 0;

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
