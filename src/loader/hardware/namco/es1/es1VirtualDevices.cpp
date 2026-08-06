#include "es1VirtualDevices.h"

#include "../../../platform/platformBackend.h"
#include "../../../elfLoader/virtualDeviceRegistry.hpp"
#include "../../../log/log.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>

namespace
{
enum class Kind
{
    Serial,
    Joystick,
    Camera
};

struct Slot
{
    int fd = -1;
    Kind kind = Kind::Serial;
    bool used = false;
};

std::array<Slot, 8> slots{};
std::mutex slotsMutex;
constexpr int FirstDescriptor = 0x4e10;

bool claims(const char *path)
{
    if (!platformIsES1() || !path)
        return false;
    return std::strncmp(path, "/dev/ttyS", 9) == 0 ||
           std::strcmp(path, "/dev/input/js0") == 0 ||
           std::strcmp(path, "/dev/video0") == 0;
}

Kind kindForPath(const char *path)
{
    if (std::strncmp(path, "/dev/ttyS", 9) == 0)
        return Kind::Serial;
    if (std::strcmp(path, "/dev/input/js0") == 0)
        return Kind::Joystick;
    return Kind::Camera;
}

int openDevice(const char *path, int flags)
{
    (void)flags;
    std::lock_guard<std::mutex> lock(slotsMutex);
    for (Slot &slot : slots)
    {
        if (!slot.used)
        {
            slot.used = true;
            slot.kind = kindForPath(path);
            slot.fd = FirstDescriptor + static_cast<int>(&slot - slots.data());
            log_info("System ES1: opened virtual device %s as fd %d", path, slot.fd);
            return slot.fd;
        }
    }
    errno = EMFILE;
    return -1;
}

Slot *findSlot(int fd)
{
    for (Slot &slot : slots)
    {
        if (slot.used && slot.fd == fd)
            return &slot;
    }
    return nullptr;
}

int owns(int fd)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    return findSlot(fd) ? 1 : 0;
}

int available(int fd)
{
    (void)fd;
    return 0;
}

int readDevice(int fd, void *buffer, size_t count)
{
    (void)fd;
    (void)buffer;
    (void)count;
    errno = EAGAIN;
    return -1;
}

int writeDevice(int fd, const void *buffer, size_t count)
{
    (void)fd;
    (void)buffer;
    return static_cast<int>(count);
}

int closeDevice(int fd)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot)
    {
        errno = EBADF;
        return -1;
    }
    slot->used = false;
    slot->fd = -1;
    return 0;
}

int ioctlDevice(int fd, unsigned long request, void *argument)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot)
    {
        errno = EBADF;
        return -1;
    }

    /* Linux joystick ABI queries used by the shared input probe. */
    if (slot->kind == Kind::Joystick && argument)
    {
        if (request == 0x80016a11) // JSIOCGAXES
            *static_cast<unsigned char *>(argument) = 4;
        else if (request == 0x80016a12) // JSIOCGBUTTONS
            *static_cast<unsigned char *>(argument) = 16;
        else if ((request & 0xffff) == 0x6a13) // JSIOCGNAME(len)
            std::strncpy(static_cast<char *>(argument), "Pacloader ES1 input", 64);
    }
    return 0;
}

const VirtualDeviceRegistry::Device serialDevice{
    "namco-es1-serial-input",
    claims,
    openDevice,
    owns,
    available,
    readDevice,
    writeDevice,
    closeDevice,
    ioctlDevice};
}

extern "C" void es1RegisterVirtualDevices(void)
{
    VirtualDeviceRegistry::registerDevice(serialDevice);
}
