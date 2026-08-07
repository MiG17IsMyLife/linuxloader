#include "es1Jvs.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "../../../config/config.h"
#include "../../common/jvs.h"
#include "../../../log/log.h"

namespace
{
constexpr int Es1JvsDescriptor = 0x4e12;
constexpr char Es1JvsDevicePath[] = "/dev/ttyS2";
constexpr size_t MaximumQueuedBytes = 8 * 1024;

std::mutex bufferMutex;
std::deque<uint8_t> requestBytes;
std::deque<uint8_t> replyBytes;

/*
 * Return the number of escaped bytes occupied by the first complete frame.
 * A zero return means that more bytes are needed.  A stray byte or an
 * incomplete frame overtaken by a new SYNC returns its byte count so the
 * caller can discard it without feeding malformed data to readPacket().
 */
size_t framedLength(const std::deque<uint8_t> &bytes, bool &complete)
{
    complete = false;
    if (bytes.empty())
        return 0;
    if (bytes.front() != SYNC)
        return 1;

    size_t raw = 1;
    size_t decoded = 0;
    size_t needed = 2; // destination and length

    while (decoded < needed)
    {
        if (raw >= bytes.size())
            return 0;

        uint8_t value = bytes[raw];
        if (value == SYNC)
            return raw;

        if (value == ESCAPE)
        {
            if (raw + 1 >= bytes.size())
                return 0;
            value = static_cast<uint8_t>(bytes[raw + 1] + 1);
            raw += 2;
        }
        else
        {
            ++raw;
        }

        ++decoded;
        if (decoded == 2)
        {
            needed = 2 + static_cast<size_t>(value);
            if (needed > JVS_MAX_PACKET_SIZE + 2)
                return 1;
        }
    }

    complete = true;
    return raw;
}

void answerRequest(const uint8_t *frame, size_t length)
{
    if (length == 0 || length > sizeof(inputBuffer))
        return;

    /* readPacket() expects the complete escaped frame in inputBuffer. */
    std::memcpy(inputBuffer, frame, length);
    std::memset(inputBuffer + length, 0, sizeof(inputBuffer) - length);

    int packetSize = 0;
    if (processPacket(&packetSize) != JVS_STATUS_SUCCESS || packetSize <= 0)
        return;

    /* A JVS reset changes the sense line; it is not answered by the slave. */
    if (inputPacket.length > 0 && inputPacket.data[0] == CMD_RESET)
        return;

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (replyBytes.size() + static_cast<size_t>(packetSize) > MaximumQueuedBytes)
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

extern "C" int es1JvsSerialEnabled(void)
{
    return getConfig()->platform == ARCADE_PLATFORM_NAMCO_ES1;
}

extern "C" int es1JvsSerialOpen(const char *path, int)
{
    if (!es1JvsSerialEnabled() || !path || std::strcmp(path, Es1JvsDevicePath) != 0)
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    requestBytes.clear();
    replyBytes.clear();
    log_info("System ES1 JVS: %s answered by the ES1-JAMMA virtual board (%s)",
             Es1JvsDevicePath, getJVSIO()->capabilities.name);
    return Es1JvsDescriptor;
}

extern "C" int es1JvsSerialIsDescriptor(int fd)
{
    return fd == Es1JvsDescriptor && es1JvsSerialEnabled();
}

extern "C" int es1JvsSerialBytesAvailable(int fd)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(replyBytes.size());
}

extern "C" int es1JvsSerialRead(int fd, void *buffer, size_t count)
{
    if (!es1JvsSerialIsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (replyBytes.empty())
    {
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = std::min(count, replyBytes.size());
    auto *out = static_cast<uint8_t *>(buffer);
    for (size_t i = 0; i < taken; ++i)
        out[i] = replyBytes[i];
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + taken);
    return static_cast<int>(taken);
}

extern "C" int es1JvsSerialWrite(int fd, const void *buffer, size_t count)
{
    if (!es1JvsSerialIsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        const auto *in = static_cast<const uint8_t *>(buffer);
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (requestBytes.size() + count > MaximumQueuedBytes)
                requestBytes.clear();
            requestBytes.insert(requestBytes.end(), in, in + count);
        }
        consumeRequests();
    }
    return static_cast<int>(count);
}

extern "C" int es1JvsSerialClose(int fd)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    requestBytes.clear();
    replyBytes.clear();
    return 0;
}

extern "C" int es1JvsSerialIoctl(int fd, unsigned long request, void *argument)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return -1;

    constexpr unsigned long LinuxFionread = 0x541B;
    if (request == LinuxFionread && argument)
    {
        *static_cast<int *>(argument) = es1JvsSerialBytesAvailable(fd);
        return 0;
    }

    /* The ES1 game only uses termios/RS485 setup here; a queue needs neither. */
    return 0;
}

#endif
