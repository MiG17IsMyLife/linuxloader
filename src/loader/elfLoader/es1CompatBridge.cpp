#if defined(_WIN32) || defined(__MINGW32__)

#include "filesystemBridge.hpp"
#include "symbolResolver.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{
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

int bridgeEpollCreate(int size)
{
    (void)size;
    errno = ENOSYS;
    return -1;
}

int bridgeEpollControl(int epfd, int operation, int fd, void *event)
{
    (void)epfd;
    (void)operation;
    (void)fd;
    (void)event;
    errno = ENOSYS;
    return -1;
}

int bridgeEpollWait(int epfd, void *events, int maxevents, int timeout)
{
    (void)epfd;
    (void)events;
    (void)maxevents;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int bridgeEventfd(unsigned int initial, int flags)
{
    (void)initial;
    (void)flags;
    errno = ENOSYS;
    return -1;
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

void *bridgeReaddir64(void *directory)
{
    /* The common filesystem bridge already translates Win32 enumeration
     * records into the 32-bit Linux dirent layout used by this ELF. */
    return bridgeReaddir(directory);
}

int bridgeReaddir64R(void *directory, void *entry, void **result)
{
    linux_dirent *source = bridgeReaddir(directory);
    if (!source)
    {
        if (result) *result = nullptr;
        return 0;
    }
    if (entry) std::memcpy(entry, source, sizeof(*source));
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
void initBridges()
{
    map("__strtof_internal", bridgeStrtofInternal);
    map("__strtold_internal", bridgeStrtoldInternal);
    map("abs", bridgeAbs);
    map("epoll_create", bridgeEpollCreate);
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
