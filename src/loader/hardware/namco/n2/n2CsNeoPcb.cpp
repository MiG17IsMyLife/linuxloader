#include "n2CsNeoPcb.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include "n2.h"
#include "../../../log/log.h"

namespace
{
constexpr int pcbDescriptor = 0x7200;
constexpr char pcbDevicePath[] = "/dev/ttyM0";
constexpr size_t pcbPacketSize = 8;

std::atomic_bool opened{false};
std::atomic_bool packetReady{false};

void makeInputPacket(uint8_t packet[pcbPacketSize])
{
    std::memset(packet, 0, pcbPacketSize);

    // Byte zero contains cabinet fault lines and stays clear. PC reset/test
    // and service are active-low cabinet inputs, so both must idle high. If
    // bit 2 is left low CS Neo remains in TEST MODE; changing it back to low
    // after startup also raises PC RESET ERROR.
    //
    // Player keyboard and mouse input is read through SDL. The virtual PCB
    // therefore only reports a neutral cabinet state.
    packet[1] = (1u << 2) | (1u << 3);

    unsigned int checksum = 0;
    for (size_t i = 0; i + 1 < pcbPacketSize; ++i)
        checksum += packet[i];
    packet[pcbPacketSize - 1] = static_cast<uint8_t>(checksum & 0x7f);
} // namespace
}

extern "C" int n2CsNeoPcbEnabled(void)
{
    return n2GetGame() == N2_GAME_CSNEO;
}

extern "C" int n2CsNeoPcbOpen(const char *path, int)
{
    if (!n2CsNeoPcbEnabled() || !path || std::strcmp(path, pcbDevicePath) != 0)
    {
        errno = ENOENT;
        return -1;
    }

    opened.store(true, std::memory_order_release);
    packetReady.store(false, std::memory_order_release);
    log_info("Namco N2 CS Neo: virtual cabinet PCB opened on /dev/ttyM0");
    return pcbDescriptor;
}

extern "C" int n2CsNeoPcbIsDescriptor(int fd)
{
    return fd == pcbDescriptor && opened.load(std::memory_order_acquire);
}

extern "C" int n2CsNeoPcbBytesAvailable(int fd)
{
    return n2CsNeoPcbIsDescriptor(fd) && packetReady.load(std::memory_order_acquire)
               ? static_cast<int>(pcbPacketSize)
               : 0;
}

extern "C" int n2CsNeoPcbRead(int fd, void *buffer, size_t count)
{
    if (!n2CsNeoPcbIsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }
    if (count < pcbPacketSize)
    {
        errno = EINVAL;
        return -1;
    }

    bool expected = true;
    if (!packetReady.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
        return 0; // idle line; a negative read makes the game drop PCB link

    uint8_t packet[pcbPacketSize];
    makeInputPacket(packet);
    std::memcpy(buffer, packet, pcbPacketSize);
    return static_cast<int>(pcbPacketSize);
}

extern "C" int n2CsNeoPcbWrite(int fd, const void *, size_t count)
{
    if (!n2CsNeoPcbIsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }
    packetReady.store(true, std::memory_order_release);
    return static_cast<int>(count);
}

extern "C" int n2CsNeoPcbClose(int fd)
{
    if (!n2CsNeoPcbIsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }
    packetReady.store(false, std::memory_order_release);
    opened.store(false, std::memory_order_release);
    return 0;
}

extern "C" int n2CsNeoPcbIoctl(int fd, unsigned long request, void *argument)
{
    if (!n2CsNeoPcbIsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }

    constexpr unsigned long linuxFionread = 0x541B;
    if (request == linuxFionread && argument)
        *static_cast<int *>(argument) = n2CsNeoPcbBytesAvailable(fd);
    return 0;
}

#endif
