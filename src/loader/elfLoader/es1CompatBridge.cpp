#if defined(_WIN32) || defined(__MINGW32__)

#include "filesystemBridge.hpp"
#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "virtualDeviceRegistry.hpp"
#include "../log/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include <thread>
#include <unordered_map>
#include <windows.h>

namespace
{
class HostMutex
{
public:
    HostMutex() { InitializeCriticalSection(&criticalSection); }
    ~HostMutex() { DeleteCriticalSection(&criticalSection); }
    HostMutex(const HostMutex &) = delete;
    HostMutex &operator=(const HostMutex &) = delete;

    void lock() { EnterCriticalSection(&criticalSection); }
    void unlock() { LeaveCriticalSection(&criticalSection); }

private:
    CRITICAL_SECTION criticalSection{};
};

float bridgeStrtofInternal(const char *value, char **end, int group)
{
    (void)group;
    return std::strtof(value, end);
}

long double bridgeStrtoldInternal(const char *value, char **end, int group)
{
    (void)group;
    return std::strtold(value, end);
}

int bridgeAbs(int value)
{
    return std::abs(value);
}

#pragma pack(push, 1)
struct LinuxEpollEvent
{
    uint32_t events;
    uint8_t data[8];
};
#pragma pack(pop)
static_assert(sizeof(LinuxEpollEvent) == 12, "i386 epoll_event layout mismatch");

struct EpollEntry
{
    int fd;
    uint32_t events;
    uint8_t data[8];
};

struct EpollInstance
{
    int fd = -1;
    bool used = false;
    std::vector<EpollEntry> entries;
};

struct EventfdInstance
{
    int fd = -1;
    bool used = false;
    bool semaphore = false;
    uint64_t counter = 0;
};

std::array<EpollInstance, 8> g_epollInstances{};
std::array<EventfdInstance, 8> g_eventfdInstances{};
HostMutex g_epollMutex;
constexpr int FirstEpollDescriptor = 0x4f00;
constexpr int FirstEventfdDescriptor = 0x4e00;
std::atomic<int> g_epollTraceCount{0};

uint32_t epollEventsFromWinsock(short events)
{
    uint32_t translated = 0;
    if (events & (POLLRDNORM | POLLIN)) translated |= 0x001; // EPOLLIN
    if (events & (POLLPRI | POLLRDBAND)) translated |= 0x002; // EPOLLPRI
    if (events & (POLLWRNORM | POLLOUT)) translated |= 0x004; // EPOLLOUT
    if (events & POLLERR) translated |= 0x008; // EPOLLERR
    if (events & POLLHUP) translated |= 0x010; // EPOLLHUP
    if (events & POLLNVAL) translated |= 0x020; // EPOLLNVAL
    return translated;
}

short winsockEventsFromEpoll(uint32_t events)
{
    short translated = 0;
    if (events & 0x001) translated |= POLLRDNORM; // EPOLLIN
    if (events & 0x002) translated |= POLLRDBAND; // EPOLLPRI
    if (events & 0x004) translated |= POLLWRNORM; // EPOLLOUT
    return translated;
}

EpollInstance *findEpoll(int fd)
{
    for (EpollInstance &instance : g_epollInstances)
    {
        if (instance.used && instance.fd == fd)
            return &instance;
    }
    return nullptr;
}

EventfdInstance *findEventfd(int fd)
{
    for (EventfdInstance &instance : g_eventfdInstances)
    {
        if (instance.used && instance.fd == fd)
            return &instance;
    }
    return nullptr;
}

int bridgeEpollCreate(int size)
{
    (void)size;
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EpollInstance &instance : g_epollInstances)
    {
        if (!instance.used)
        {
            instance.used = true;
            instance.fd = FirstEpollDescriptor + static_cast<int>(&instance - g_epollInstances.data());
            instance.entries.clear();
            if (g_epollTraceCount.fetch_add(1) < 16)
                log_debug("ES1 epoll_create -> %d", instance.fd);
            return instance.fd;
        }
    }
    errno = EMFILE;
    return -1;
}

int bridgeEpollControl(int epfd, int operation, int fd, void *event)
{
    (void)epfd;
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EpollInstance *instance = findEpoll(epfd);
    if (!instance || (operation != 2 && !event))
    {
        errno = EBADF;
        return -1;
    }

    const auto *guestEvent = static_cast<const LinuxEpollEvent *>(event);
    if (g_epollTraceCount.fetch_add(1) < 32)
        log_debug("ES1 epoll_ctl epfd=%d op=%d fd=%d events=0x%08x", epfd, operation, fd,
                 guestEvent ? guestEvent->events : 0);
    auto entry = std::find_if(instance->entries.begin(), instance->entries.end(),
                              [fd](const EpollEntry &candidate) { return candidate.fd == fd; });
    if (operation == 1) // EPOLL_CTL_ADD
    {
        if (entry != instance->entries.end())
        {
            errno = EEXIST;
            return -1;
        }
        EpollEntry newEntry = {};
        newEntry.fd = fd;
        newEntry.events = guestEvent->events;
        std::memcpy(newEntry.data, guestEvent->data, sizeof(newEntry.data));
        instance->entries.push_back(newEntry);
        return 0;
    }
    if (operation == 2) // EPOLL_CTL_DEL
    {
        if (entry == instance->entries.end())
        {
            errno = ENOENT;
            return -1;
        }
        instance->entries.erase(entry);
        return 0;
    }
    if (operation == 3) // EPOLL_CTL_MOD
    {
        if (entry == instance->entries.end())
        {
            errno = ENOENT;
            return -1;
        }
        entry->events = guestEvent->events;
        std::memcpy(entry->data, guestEvent->data, sizeof(entry->data));
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int bridgeEpollWait(int epfd, void *events, int maxevents, int timeout)
{
    if (!events || maxevents <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    std::vector<EpollEntry> entries;
    {
        std::lock_guard<HostMutex> lock(g_epollMutex);
        EpollInstance *instance = findEpoll(epfd);
        if (!instance)
        {
            errno = EBADF;
            return -1;
        }
        entries = instance->entries;
    }

    auto *guestEvents = static_cast<LinuxEpollEvent *>(events);
    std::vector<WSAPOLLFD> hostFds;
    std::vector<size_t> hostIndexes;
    int ready = 0;
    if (g_epollTraceCount.fetch_add(1) < 48)
        log_debug("ES1 epoll_wait epfd=%d max=%d timeout=%d entries=%u", epfd, maxevents, timeout,
                 static_cast<unsigned>(entries.size()));

    for (size_t index = 0; index < entries.size() && ready < maxevents; ++index)
    {
        const EpollEntry &entry = entries[index];
        EventfdInstance *eventfd = nullptr;
        {
            std::lock_guard<HostMutex> lock(g_epollMutex);
            eventfd = findEventfd(entry.fd);
        }
        if (eventfd)
        {
            uint32_t eventMask = 0;
            {
                std::lock_guard<HostMutex> lock(g_epollMutex);
                eventfd = findEventfd(entry.fd);
                if (eventfd && (entry.events & 0x001) && eventfd->counter != 0)
                    eventMask |= 0x001; // EPOLLIN
            }
            if (eventMask)
            {
                guestEvents[ready].events = eventMask;
                std::memcpy(guestEvents[ready].data, entry.data, sizeof(entry.data));
                ++ready;
            }
            continue;
        }
        const auto *device = VirtualDeviceRegistry::find(entry.fd);
        if (device)
        {
            uint32_t eventMask = 0;
            if ((entry.events & 0x001) && device->bytesAvailable(entry.fd) > 0)
                eventMask |= 0x001; // EPOLLIN
            if (entry.events & 0x004)
                eventMask |= 0x004; // EPOLLOUT
            if (eventMask)
            {
                guestEvents[ready].events = eventMask;
                std::memcpy(guestEvents[ready].data, entry.data, sizeof(entry.data));
                ++ready;
            }
            continue;
        }

        if (NetworkBridge::isSocketDescriptor(entry.fd))
        {
            WSAPOLLFD host = {};
            host.fd = NetworkBridge::hostSocket(entry.fd);
            host.events = winsockEventsFromEpoll(entry.events);
            hostFds.push_back(host);
            hostIndexes.push_back(index);
        }
    }

    const int hostTimeout = ready != 0 ? 0 : timeout;
    if (!hostFds.empty())
    {
        const int polled = WSAPoll(hostFds.data(), static_cast<ULONG>(hostFds.size()), hostTimeout);
        if (polled == SOCKET_ERROR)
        {
            errno = WSAGetLastError() == WSAEINTR ? EINTR : EIO;
            return -1;
        }
        for (size_t index = 0; index < hostFds.size() && ready < maxevents; ++index)
        {
            const uint32_t eventMask = epollEventsFromWinsock(hostFds[index].revents);
            if (!eventMask)
                continue;
            guestEvents[ready].events = eventMask;
            std::memcpy(guestEvents[ready].data, entries[hostIndexes[index]].data,
                        sizeof(entries[hostIndexes[index]].data));
            ++ready;
        }
    }
    else if (ready == 0 && timeout != 0)
    {
        const int waitMilliseconds = timeout < 0 ? 10 : std::min(timeout, 10);
        Sleep(static_cast<DWORD>(waitMilliseconds));
    }

    return ready;
}

int bridgeEventfd(unsigned int initial, int flags)
{
    // Boost.Asio has a Linux pipe fallback for platforms without eventfd.  The
    // game was built against that path and expects the returned descriptor to
    // behave like a normal pipe endpoint (including fcntl/epoll ownership).
    // Let it take the same fallback used by the reference ES1 loader instead
    // of returning a private descriptor type that libc code cannot recognize.
    (void)initial;
    (void)flags;
    errno = ENOMEM;
    return -1;

#if 0
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EventfdInstance &instance : g_eventfdInstances)
    {
        if (!instance.used)
        {
            instance.used = true;
            instance.fd = FirstEventfdDescriptor + static_cast<int>(&instance - g_eventfdInstances.data());
            instance.semaphore = (flags & 1) != 0; // EFD_SEMAPHORE
            instance.counter = initial;
            if (g_epollTraceCount.fetch_add(1) < 16)
                log_debug("ES1 eventfd -> %d", instance.fd);
            return instance.fd;
        }
    }
    errno = EMFILE;
    return -1;
#endif
}

bool isEventfdInternal(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    return findEventfd(fd) != nullptr;
}

int readEventfdInternal(int fd, void *buffer, size_t length)
{
    if (!buffer || length < sizeof(uint64_t))
    {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
    {
        errno = EBADF;
        return -1;
    }
    if (instance->counter == 0)
    {
        errno = EAGAIN;
        return -1;
    }
    const uint64_t value = instance->semaphore ? 1 : instance->counter;
    std::memcpy(buffer, &value, sizeof(value));
    if (instance->semaphore)
        --instance->counter;
    else
        instance->counter = 0;
    return static_cast<int>(sizeof(value));
}

int writeEventfdInternal(int fd, const void *buffer, size_t length)
{
    if (!buffer || length < sizeof(uint64_t))
    {
        errno = EINVAL;
        return -1;
    }
    uint64_t value = 0;
    std::memcpy(&value, buffer, sizeof(value));
    if (value == UINT64_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
    {
        errno = EBADF;
        return -1;
    }
    if (UINT64_MAX - instance->counter <= value)
    {
        errno = EAGAIN;
        return -1;
    }
    instance->counter += value;
    return static_cast<int>(sizeof(value));
}

int closeEventfdInternal(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
        return 0;
    instance->used = false;
    instance->fd = -1;
    instance->counter = 0;
    return 0;
}

int bridgeGetNprocs()
{
    const unsigned count = std::thread::hardware_concurrency();
    return count ? static_cast<int>(count) : 1;
}

int bridgeLink(const char *oldPath, const char *newPath)
{
    (void)oldPath;
    (void)newPath;
    return 0;
}

int bridgeMlockall(unsigned long flags)
{
    (void)flags;
    return 0;
}

int bridgeMunlockall()
{
    return 0;
}

long bridgePathconf(const char *path, int name)
{
    (void)path;
    (void)name;
    return 4096;
}

int bridgePthreadKill(void *thread, int signal)
{
    (void)thread;
    (void)signal;
    return 0;
}

struct LinuxDirent64
{
    uint64_t inode;
    int64_t offset;
    uint16_t recordLength;
    uint8_t type;
    char name[260];
};

HostMutex direntMutex;
std::unordered_map<void *, LinuxDirent64> dirent64Records;

LinuxDirent64 *nextDirent64(void *directory)
{
    linux_dirent *source = bridgeReaddir(directory);
    if (!source)
    {
        std::lock_guard<HostMutex> lock(direntMutex);
        dirent64Records.erase(directory);
        return nullptr;
    }

    LinuxDirent64 &record = dirent64Records[directory];
    std::memset(&record, 0, sizeof(record));
    record.inode = static_cast<uint32_t>(source->d_ino);
    record.offset = source->d_off;
    record.type = source->d_type;
    const size_t nameLength = std::min(std::strlen(source->d_name), sizeof(record.name) - 1);
    std::memcpy(record.name, source->d_name, nameLength);
    record.name[nameLength] = '\0';
    record.recordLength = static_cast<uint16_t>(offsetof(LinuxDirent64, name) +
                                                std::strlen(record.name) + 1);
    return &record;
}

void *bridgeReaddir64(void *directory)
{
    std::lock_guard<HostMutex> lock(direntMutex);
    return nextDirent64(directory);
}

int bridgeReaddir64R(void *directory, void *entry, void **result)
{
    std::lock_guard<HostMutex> lock(direntMutex);
    LinuxDirent64 *source = nextDirent64(directory);
    if (!source)
    {
        if (result) *result = nullptr;
        return 0;
    }
    if (entry) std::memcpy(entry, source, sizeof(LinuxDirent64));
    if (result) *result = entry;
    return 0;
}

int bridgeStatvfs64(const char *path, void *result)
{
    (void)path;
    if (result) std::memset(result, 0, 256);
    return 0;
}

int bridgeSymlink(const char *target, const char *linkPath)
{
    (void)target;
    (void)linkPath;
    return 0;
}

long bridgeSyscall(long number, ...)
{
    (void)number;
    errno = ENOSYS;
    return -1;
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace Es1CompatBridge
{
bool isEventfd(int fd)
{
    return isEventfdInternal(fd);
}

int readEventfd(int fd, void *buffer, size_t length)
{
    return readEventfdInternal(fd, buffer, length);
}

int writeEventfd(int fd, const void *buffer, size_t length)
{
    return writeEventfdInternal(fd, buffer, length);
}

int closeEventfd(int fd)
{
    return closeEventfdInternal(fd);
}

void initBridges()
{
    map("__strtof_internal", bridgeStrtofInternal);
    map("__strtold_internal", bridgeStrtoldInternal);
    map("abs", bridgeAbs);
    map("epoll_create", bridgeEpollCreate);
    map("epoll_create1", bridgeEpollCreate);
    map("epoll_ctl", bridgeEpollControl);
    map("epoll_wait", bridgeEpollWait);
    map("eventfd", bridgeEventfd);
    map("get_nprocs", bridgeGetNprocs);
    map("link", bridgeLink);
    map("mlockall", bridgeMlockall);
    map("munlockall", bridgeMunlockall);
    map("pathconf", bridgePathconf);
    map("pthread_kill", bridgePthreadKill);
    map("readdir64", bridgeReaddir64);
    map("readdir64_r", bridgeReaddir64R);
    map("statvfs64", bridgeStatvfs64);
    map("symlink", bridgeSymlink);
    map("syscall", bridgeSyscall);
}
}

#endif
