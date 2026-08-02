#pragma once

#include <cstddef>

namespace VirtualDeviceRegistry
{
    struct Device
    {
        const char *name;
        bool (*claimsPath)(const char *path);
        int (*open)(const char *path, int flags);
        int (*ownsDescriptor)(int fd);
        int (*bytesAvailable)(int fd);
        int (*read)(int fd, void *buffer, size_t count);
        int (*write)(int fd, const void *buffer, size_t count);
        int (*close)(int fd);
        int (*ioctl)(int fd, unsigned long request, void *argument);
    };

    struct OpenResult
    {
        bool claimed;
        int descriptor;
    };

    bool registerDevice(const Device &device);
    OpenResult open(const char *path, int flags);
    const Device *find(int fd);
    void clear(void);
}
