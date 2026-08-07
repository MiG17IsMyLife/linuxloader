#if defined(_WIN32) || defined(__MINGW32__)

#include "virtualDeviceRegistry.hpp"

#include "../log/log.h"

#include <array>
#include <cstring>

namespace VirtualDeviceRegistry
{
    namespace
    {
        constexpr size_t MaxDevices = 16;
        std::array<Device, MaxDevices> devices{};
        size_t deviceCount = 0;
    }

    bool registerDevice(const Device &device)
    {
        if (!device.name || !device.claimsPath || !device.open ||
            !device.ownsDescriptor || !device.read || !device.write ||
            !device.close || !device.ioctl)
        {
            log_error("Virtual device registration rejected: incomplete operations");
            return false;
        }

        for (size_t i = 0; i < deviceCount; ++i)
        {
            if (std::strcmp(devices[i].name, device.name) == 0)
                return true;
        }

        if (deviceCount == devices.size())
        {
            log_error("Virtual device registry is full; cannot register %s", device.name);
            return false;
        }

        devices[deviceCount++] = device;
        log_debug("Registered virtual device: %s", device.name);
        return true;
    }

    OpenResult open(const char *path, int flags)
    {
        if (!path)
            return {false, -1};

        for (size_t i = 0; i < deviceCount; ++i)
        {
            if (devices[i].claimsPath(path))
                return {true, devices[i].open(path, flags)};
        }
        return {false, -1};
    }

    const Device *find(int fd)
    {
        for (size_t i = 0; i < deviceCount; ++i)
        {
            if (devices[i].ownsDescriptor(fd))
                return &devices[i];
        }
        return nullptr;
    }

    void *map(int fd, void *address, size_t length, int protection,
              int flags, off_t offset)
    {
        for (size_t i = 0; i < deviceCount; ++i)
        {
            if (devices[i].ownsDescriptor(fd) && devices[i].map)
                return devices[i].map(fd, address, length, protection, flags, offset);
        }
        return nullptr;
    }

    void clear(void)
    {
        devices.fill({});
        deviceCount = 0;
    }
}

#endif
