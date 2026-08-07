#include "es1Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "../../../platform/platformBackend.h"
#include "../../../log/log.h"

namespace
{
constexpr int Descriptor = 0x4e20;
/*
 * clKickback::init() in Maximum Heat 3D builds clSerialParam with device
 * index 1.  clSerialN2::open() indexes the game's table at
 * 0x85e1cd8, where index 1 is /dev/ttyS1.  This is the separate steering
 * PCB port; it must not be confused with the ES1 JVS serial ports.
 */
constexpr char DevicePath[] = "/dev/ttyS1";
constexpr size_t CommandLength = 10;
constexpr size_t MaximumQueuedBytes = 1024;
constexpr uint8_t FrameHeader = 0xff;
constexpr uint8_t HealthyResult[3] = {'E', '0', '0'};

std::mutex boardMutex;
std::deque<uint8_t> commandBytes;
std::deque<uint8_t> replyBytes;
bool opened = false;
unsigned long framesSeen = 0;

void describeFrame(const uint8_t *frame, char *text, size_t size)
{
    size_t written = 0;
    for (size_t i = 0; i < CommandLength && written + 4 < size; ++i)
    {
        const int amount = std::snprintf(text + written, size - written,
                                         "%02X%s", frame[i],
                                         i + 1 == CommandLength ? "" : " ");
        if (amount <= 0)
            break;
        written += static_cast<size_t>(amount);
    }
}

void consumeCommands(void)
{
    std::lock_guard<std::mutex> lock(boardMutex);
    for (;;)
    {
        while (!commandBytes.empty() && commandBytes.front() != FrameHeader)
            commandBytes.pop_front();

        if (commandBytes.size() < CommandLength)
            return;

        uint8_t frame[CommandLength];
        std::copy_n(commandBytes.begin(), CommandLength, frame);
        commandBytes.erase(commandBytes.begin(), commandBytes.begin() + CommandLength);

        /* clKickback::receive() reads exactly three bytes from the STR PCB. */
        replyBytes.assign(HealthyResult, HealthyResult + sizeof(HealthyResult));

        if (++framesSeen == 1)
        {
            char text[CommandLength * 3] = {};
            describeFrame(frame, text, sizeof(text));
            log_warn("System ES1 steering: first STR frame [%s], replied E00", text);
        }
    }
}
}

extern "C" bool es1KickbackClaimsPath(const char *path)
{
    return platformIsES1() && path && std::strcmp(path, DevicePath) == 0;
}

extern "C" int es1KickbackOpen(const char *path, int)
{
    if (!es1KickbackClaimsPath(path))
    {
        errno = ENOENT;
        return -1;
    }

    std::lock_guard<std::mutex> lock(boardMutex);
    commandBytes.clear();
    replyBytes.clear();
    framesSeen = 0;
    if (!opened)
    {
        opened = true;
        log_warn("System ES1 steering: %s answered by the virtual STR PCB", DevicePath);
    }
    return Descriptor;
}

extern "C" int es1KickbackOwnsDescriptor(int fd)
{
    return fd == Descriptor && platformIsES1();
}

extern "C" int es1KickbackBytesAvailable(int fd)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return 0;
    std::lock_guard<std::mutex> lock(boardMutex);
    return static_cast<int>(replyBytes.size());
}

extern "C" int es1KickbackRead(int fd, void *buffer, size_t count)
{
    if (!es1KickbackOwnsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(boardMutex);
    if (replyBytes.empty())
    {
        errno = EAGAIN;
        return -1;
    }

    const size_t amount = std::min(count, replyBytes.size());
    std::copy_n(replyBytes.begin(), amount, static_cast<uint8_t *>(buffer));
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + amount);
    return static_cast<int>(amount);
}

extern "C" int es1KickbackWrite(int fd, const void *buffer, size_t count)
{
    if (!es1KickbackOwnsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        std::lock_guard<std::mutex> lock(boardMutex);
        if (commandBytes.size() + count > MaximumQueuedBytes)
            commandBytes.clear();
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
        commandBytes.insert(commandBytes.end(), bytes, bytes + count);
    }
    consumeCommands();
    return static_cast<int>(count);
}

extern "C" int es1KickbackClose(int fd)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(boardMutex);
    commandBytes.clear();
    replyBytes.clear();
    return 0;
}

extern "C" int es1KickbackIoctl(int fd, unsigned long request, void *argument)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return -1;

    constexpr unsigned long LinuxFionread = 0x541B;
    if (request == LinuxFionread && argument)
    {
        *static_cast<int *>(argument) = es1KickbackBytesAvailable(fd);
        return 0;
    }
    return 0;
}

#endif
