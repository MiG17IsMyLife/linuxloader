#if defined(_WIN32) || defined(__MINGW32__)

#define FD_SETSIZE 1024

#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "../log/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <climits>
#include <mutex>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>

static std::mutex g_net_mutex;
static std::mutex g_socket_mutex;

static constexpr int g_firstSocketDescriptor = 512;
static constexpr int g_lastSocketDescriptor = 1023;
static constexpr int g_socketSlotCount = g_lastSocketDescriptor - g_firstSocketDescriptor + 1;

struct SocketTable
{
    std::atomic<SOCKET> slots[g_socketSlotCount];
    SocketTable()
    {
        for (auto &slot : slots)
            slot.store(INVALID_SOCKET, std::memory_order_relaxed);
    }
};
static SocketTable g_socketTable;

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

// Helper to map WSA errors to POSIX errno

// Some POSIX error codes might not be defined in MinGW's <cerrno>
#ifndef ENOTSOCK
#define ENOTSOCK EINVAL
#endif
#ifndef EDESTADDRREQ
#define EDESTADDRREQ EINVAL
#endif
#ifndef EMSGSIZE
#define EMSGSIZE EINVAL
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE EINVAL
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT EINVAL
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT EINVAL
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT EINVAL
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP EINVAL
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT EINVAL
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT EINVAL
#endif
#ifndef EADDRINUSE
#define EADDRINUSE EINVAL
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL EINVAL
#endif
#ifndef ENETDOWN
#define ENETDOWN EINVAL
#endif
#ifndef ENETUNREACH
#define ENETUNREACH EINVAL
#endif
#ifndef ENETRESET
#define ENETRESET EINVAL
#endif
#ifndef ECONNABORTED
#define ECONNABORTED EINVAL
#endif
#ifndef ECONNRESET
#define ECONNRESET EINVAL
#endif
#ifndef ENOBUFS
#define ENOBUFS EINVAL
#endif
#ifndef EISCONN
#define EISCONN EINVAL
#endif
#ifndef ENOTCONN
#define ENOTCONN EINVAL
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN EINVAL
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS EINVAL
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT EINVAL
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED EINVAL
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN EINVAL
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH EINVAL
#endif
#ifndef EPROCLIM
#define EPROCLIM EINVAL
#endif
#ifndef EUSERS
#define EUSERS EINVAL
#endif
#ifndef EDQUOT
#define EDQUOT EINVAL
#endif
#ifndef ESTALE
#define ESTALE EINVAL
#endif
#ifndef EREMOTE
#define EREMOTE EINVAL
#endif

static int mapWSAErrorToErrno(int wsaError)
{
    switch (wsaError)
    {
        case WSAEINTR:
            return EINTR;
        case WSAEBADF:
            return EBADF;
        case WSAEACCES:
            return EACCES;
        case WSAEFAULT:
            return EFAULT;
        case WSAEINVAL:
            return EINVAL;
        case WSAEMFILE:
            return EMFILE;
        case WSAEWOULDBLOCK:
            return EAGAIN;
        case WSAEINPROGRESS:
            return EINPROGRESS;
        case WSAEALREADY:
            return EALREADY;
        case WSAENOTSOCK:
            return ENOTSOCK;
        case WSAEDESTADDRREQ:
            return EDESTADDRREQ;
        case WSAEMSGSIZE:
            return EMSGSIZE;
        case WSAEPROTOTYPE:
            return EPROTOTYPE;
        case WSAENOPROTOOPT:
            return ENOPROTOOPT;
        case WSAEPROTONOSUPPORT:
            return EPROTONOSUPPORT;
        case WSAESOCKTNOSUPPORT:
            return ESOCKTNOSUPPORT;
        case WSAEOPNOTSUPP:
            return EOPNOTSUPP;
        case WSAEPFNOSUPPORT:
            return EPFNOSUPPORT;
        case WSAEAFNOSUPPORT:
            return EAFNOSUPPORT;
        case WSAEADDRINUSE:
            return EADDRINUSE;
        case WSAEADDRNOTAVAIL:
            return EADDRNOTAVAIL;
        case WSAENETDOWN:
            return ENETDOWN;
        case WSAENETUNREACH:
            return ENETUNREACH;
        case WSAENETRESET:
            return ENETRESET;
        case WSAECONNABORTED:
            return ECONNABORTED;
        case WSAECONNRESET:
            return ECONNRESET;
        case WSAENOBUFS:
            return ENOBUFS;
        case WSAEISCONN:
            return EISCONN;
        case WSAENOTCONN:
            return ENOTCONN;
        case WSAESHUTDOWN:
            return ESHUTDOWN;
        case WSAETOOMANYREFS:
            return ETOOMANYREFS;
        case WSAETIMEDOUT:
            return ETIMEDOUT;
        case WSAECONNREFUSED:
            return ECONNREFUSED;
        case WSAELOOP:
            return ELOOP;
        case WSAENAMETOOLONG:
            return ENAMETOOLONG;
        case WSAEHOSTDOWN:
            return EHOSTDOWN;
        case WSAEHOSTUNREACH:
            return EHOSTUNREACH;
        case WSAENOTEMPTY:
            return ENOTEMPTY;
        case WSAEPROCLIM:
            return EPROCLIM;
        case WSAEUSERS:
            return EUSERS;
        case WSAEDQUOT:
            return EDQUOT;
        case WSAESTALE:
            return ESTALE;
        case WSAEREMOTE:
            return EREMOTE;
        default:
            return EINVAL;
    }
}

namespace NetworkBridge
{
    static int registerSocket(SOCKET socket)
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);

        int freeSlot = -1;
        for (int slot = 0; slot < g_socketSlotCount; slot++)
        {
            const SOCKET held = g_socketTable.slots[slot].load(std::memory_order_relaxed);
            if (held == socket)
                return g_firstSocketDescriptor + slot;
            if (held == INVALID_SOCKET && freeSlot < 0)
                freeSlot = slot;
        }

        if (freeSlot < 0)
        {
            log_error("Network bridge: no descriptor left for a new socket (%d in use)", g_socketSlotCount);
            return -1;
        }

        g_socketTable.slots[freeSlot].store(socket, std::memory_order_release);
        return g_firstSocketDescriptor + freeSlot;
    }

    SOCKET hostSocket(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return static_cast<SOCKET>(descriptor);
        const SOCKET held = g_socketTable.slots[descriptor - g_firstSocketDescriptor].load(std::memory_order_acquire);
        return held == INVALID_SOCKET ? static_cast<SOCKET>(descriptor) : held;
    }

    // The reverse of hostSocket(), for handing select() results back.
    int guestDescriptor(SOCKET socket)
    {
        for (int slot = 0; slot < g_socketSlotCount; slot++)
        {
            if (g_socketTable.slots[slot].load(std::memory_order_relaxed) == socket)
                return g_firstSocketDescriptor + slot;
        }
        return static_cast<int>(socket);
    }

    void forgetSocket(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return;
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        g_socketTable.slots[descriptor - g_firstSocketDescriptor].store(INVALID_SOCKET, std::memory_order_release);
    }

    void initBridges()
    {
        log_info("Initializing Network Bridges...");

        MAP("socket", bridgeSocket);
        MAP("connect", bridgeConnect);
        MAP("bind", bridgeBind);
        MAP("listen", bridgeListen);
        MAP("accept", bridgeAccept);
        MAP("send", bridgeSend);
        MAP("recv", bridgeRecv);
        MAP("sendto", bridgeSendto);
        MAP("recvfrom", bridgeRecvfrom);
        MAP("sendmsg", bridgeSendmsg);
        MAP("recvmsg", bridgeRecvmsg);
        MAP("getpeername", bridgeGetpeername);
        MAP("getsockname", bridgeGetsockname);
        MAP("shutdown", bridgeShutdown);
        MAP("setsockopt", bridgeSetsockopt);
        MAP("getsockopt", bridgeGetsockopt);
        MAP("inet_pton", bridgeInet_pton);
        MAP("inet_ntop", bridgeInet_ntop);
        MAP("inet_aton", bridgeInet_aton);
        MAP("inet_addr", bridgeInet_addr);
        MAP("inet_ntoa", bridgeInet_ntoa);
        MAP("ntohs", bridgeNtohs);
        MAP("htons", bridgeHtons);
        MAP("htonl", bridgeHtonl);
        MAP("ntohl", bridgeNtohl);
        MAP("gethostbyname_r", bridgeGethostbyname_r);
        MAP("gethostbyaddr_r", bridgeGethostbyaddr_r);
        MAP("gethostbyaddr", bridgeGethostbyaddr_r);
        MAP("gethostbyname", bridgeGethostbyname_r);
        MAP("gethostname", bridgeGethostname);
        MAP("getservbyname", bridgeGethostbyname_r);
    }

    unsigned long bridgeInet_addr(const char *cp)
    {
        // cp = "127.0.0.1";
        return inet_addr(cp);
    }

    int bridgeInet_aton(const char *cp, struct in_addr *inp)
    {
        unsigned long addr = bridgeInet_addr(cp);
        if (addr == INADDR_NONE && strcmp(cp, "255.255.255.255") != 0)
            return 0;
        if (inp)
            inp->s_addr = addr;
        return 1;
    }

    int bridgeInet_pton(int af, const char *src, void *dst)
    {
        return InetPtonA(af, src, dst);
    }

    const char *bridgeInet_ntop(int af, const void *src, char *dst, size_t size)
    {
        if (!src || !dst || size == 0)
        {
            errno = EINVAL;
            return nullptr;
        }

        const char *result = InetNtopA(af, const_cast<void *>(src), dst, size);
        if (!result)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return result;
    }

    char *bridgeInet_ntoa(struct in_addr in)
    {
        return inet_ntoa(in);
    }
    uint16_t bridgeNtohs(uint16_t netshort)
    {
        return ntohs(netshort);
    }
    uint16_t bridgeHtons(uint16_t hostshort)
    {
        return htons(hostshort);
    }
    uint32_t bridgeNtohl(uint32_t netlong)
    {
        return ntohl(netlong);
    }
    uint32_t bridgeHtonl(uint32_t hostlong)
    {
        return htonl(hostlong);
    }

    bool isSocketDescriptor(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return false;
        return g_socketTable.slots[descriptor - g_firstSocketDescriptor].load(std::memory_order_acquire) != INVALID_SOCKET;
    }

    int bridgeSocketRead(int descriptor, void *buffer, size_t length)
    {
        return bridgeRecv(static_cast<SOCKET>(descriptor), static_cast<char *>(buffer),
                          static_cast<int>(std::min(length, static_cast<size_t>(INT_MAX))), 0);
    }

    int bridgeSocketWrite(int descriptor, const void *buffer, size_t length)
    {
        return bridgeSend(static_cast<SOCKET>(descriptor), static_cast<const char *>(buffer),
                          static_cast<int>(std::min(length, static_cast<size_t>(INT_MAX))), 0);
    }

    int bridgeSocketClose(int descriptor)
    {
        const SOCKET socket = NetworkBridge::hostSocket(descriptor);
        NetworkBridge::forgetSocket(descriptor);
        const int result = closesocket(socket);
        if (result == SOCKET_ERROR)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return result;
    }
}; // namespace NetworkBridge

extern "C" SOCKET bridgeSocket(int af, int type, int protocol)
{
    log_trace(">>> socket called: af=%d, type=%d, protocol=%d", af, type, protocol);
    SOCKET s = socket(af, type, protocol);
    if (s == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }

    const int descriptor = NetworkBridge::registerSocket(s);
    if (descriptor < 0)
    {
        closesocket(s);
        errno = EMFILE;
        return (SOCKET)-1;
    }
    log_trace(">>> socket EXIT: handle %lld as descriptor %d", (long long)s, descriptor);
    return static_cast<SOCKET>(descriptor);
}

extern "C" int bridgeConnect(SOCKET s, const struct sockaddr *name, int namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> connect called: socket=%lld", (long long)s);
    int ret = connect(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> connect EXIT: returning %d (WSAError=%d)", ret, ret < 0 ? WSAGetLastError() : 0);
    return ret;
}

extern "C" int bridgeBind(SOCKET s, const struct sockaddr *name, int namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> bind called: socket=%lld", (long long)s);
    int ret = bind(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> bind EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeListen(SOCKET s, int backlog)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> listen called: socket=%lld, backlog=%d", (long long)s, backlog);
    int ret = listen(s, backlog);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> listen EXIT: returning %d", ret);
    return ret;
}

extern "C" SOCKET bridgeAccept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> accept ENTRY: socket=%lld (THIS MAY BLOCK!)", (long long)s);
    SOCKET ret = accept(s, addr, addrlen);
    if (ret == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }
    const int descriptor = NetworkBridge::registerSocket(ret);
    if (descriptor < 0)
    {
        closesocket(ret);
        errno = EMFILE;
        return (SOCKET)-1;
    }
    log_trace(">>> accept EXIT: handle %lld as descriptor %d", (long long)ret, descriptor);
    return static_cast<SOCKET>(descriptor);
}

extern "C" int bridgeShutdown(SOCKET s, int how)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    const int ret = shutdown(s, how);
    if (ret == SOCKET_ERROR)
        errno = mapWSAErrorToErrno(WSAGetLastError());
    return ret;
}

extern "C" int bridgeRecv(SOCKET s, char *buf, int len, int flags)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> recv ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);

    if (flags & 0x40)
    {
        flags &= ~0x40;
    }

    int ret = recv(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> recv EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSend(SOCKET s, const char *buf, int len, int flags)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> send called: socket=%lld, len=%d", (long long)s, len);

    flags &= ~(0x4000 | 0x40);

    int ret = send(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> send EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeRecvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> recvfrom ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);
    if (flags & 0x40)
        flags &= ~0x40;
    int ret = recvfrom(s, buf, len, flags, from, fromlen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> recvfrom EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> sendto called: socket=%lld, len=%d", (long long)s, len);
    flags &= ~(0x4000 | 0x40);
    int ret = sendto(s, buf, len, flags, to, tolen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_trace(">>> sendto EXIT: returning %d", ret);
    return ret;
}

static constexpr int linuxMsgDontWait = 0x40;
static constexpr int linuxMsgNoSignal = 0x4000;
static constexpr int linuxMsgWaitAll = 0x100;

static int translateMessageFlags(int flags)
{
    int translated = flags & ~(linuxMsgDontWait | linuxMsgNoSignal | linuxMsgWaitAll);
    if (flags & linuxMsgWaitAll)
        translated |= MSG_WAITALL;
    return translated;
}

// Winsock stores the length before the pointer, so the vectors must be rebuilt.
static bool buildWsaBuffers(const LinuxMsghdr *message, std::vector<WSABUF> &buffers)
{
    if (!message)
    {
        errno = EFAULT;
        return false;
    }

    if (message->iovCount && !message->iov)
    {
        errno = EFAULT;
        return false;
    }

    buffers.resize(message->iovCount);
    for (uint32_t i = 0; i < message->iovCount; ++i)
    {
        buffers[i].len = static_cast<ULONG>(message->iov[i].length);
        buffers[i].buf = static_cast<CHAR *>(message->iov[i].base);
    }
    return true;
}

extern "C" int bridgeSendmsg(SOCKET s, const LinuxMsghdr *message, int flags)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    std::vector<WSABUF> buffers;
    if (!buildWsaBuffers(message, buffers))
        return -1;

    if (message->controlLength)
    {
        // Only used for SCM_RIGHTS style ancillary data, which the LAN code
        // never sends; dropping it is safer than failing the whole transfer.
        log_warn(">>> sendmsg: dropping %u bytes of ancillary data",
                 static_cast<unsigned>(message->controlLength));
    }

    DWORD sent = 0;
    const DWORD wsaFlags = static_cast<DWORD>(translateMessageFlags(flags));
    int ret;
    if (message->name && message->nameLength)
        ret = WSASendTo(s, buffers.data(), static_cast<DWORD>(buffers.size()), &sent, wsaFlags,
                        static_cast<const struct sockaddr *>(message->name),
                        static_cast<int>(message->nameLength), nullptr, nullptr);
    else
        ret = WSASend(s, buffers.data(), static_cast<DWORD>(buffers.size()), &sent, wsaFlags,
                      nullptr, nullptr);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> sendmsg EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
        return -1;
    }

    log_trace(">>> sendmsg EXIT: socket=%lld sent %lu bytes in %u buffers",
             (long long)s, (unsigned long)sent, static_cast<unsigned>(buffers.size()));
    return static_cast<int>(sent);
}

extern "C" int bridgeRecvmsg(SOCKET s, LinuxMsghdr *message, int flags)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    std::vector<WSABUF> buffers;
    if (!buildWsaBuffers(message, buffers))
        return -1;

    DWORD received = 0;
    DWORD wsaFlags = static_cast<DWORD>(translateMessageFlags(flags));
    int ret;
    if (message->name && message->nameLength)
    {
        int nameLength = static_cast<int>(message->nameLength);
        ret = WSARecvFrom(s, buffers.data(), static_cast<DWORD>(buffers.size()), &received, &wsaFlags,
                          static_cast<struct sockaddr *>(message->name), &nameLength, nullptr, nullptr);
        if (ret != SOCKET_ERROR)
            message->nameLength = static_cast<uint32_t>(nameLength);
    }
    else
    {
        ret = WSARecv(s, buffers.data(), static_cast<DWORD>(buffers.size()), &received, &wsaFlags,
                      nullptr, nullptr);
    }

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> recvmsg EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
        return -1;
    }

    // Windows never reports ancillary data, so the caller must see none.
    message->controlLength = 0;
    message->flags = static_cast<int32_t>(wsaFlags);

    log_trace(">>> recvmsg EXIT: socket=%lld received %lu bytes in %u buffers",
             (long long)s, (unsigned long)received, static_cast<unsigned>(buffers.size()));
    return static_cast<int>(received);
}

extern "C" int bridgeGetpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    int ret = getpeername(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> getpeername EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
    }
    return ret;
}

extern "C" int bridgeGetsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    int ret = getsockname(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> getsockname EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
    }
    return ret;
}

namespace
{
constexpr int linuxSolSocket = 1;
constexpr int linuxIpprotoIp = 0;
constexpr int linuxIpprotoTcp = 6;

constexpr int linuxSoLinger = 13;
constexpr int linuxSoRcvtimeo = 20;
constexpr int linuxSoSndtimeo = 21;

struct OptionMapping
{
    int guest;
    int host;
};

const OptionMapping socketLevelOptions[] = {
    {1, SO_DEBUG},      {2, SO_REUSEADDR}, {3, SO_TYPE},       {4, SO_ERROR},
    {5, SO_DONTROUTE},  {6, SO_BROADCAST}, {7, SO_SNDBUF},     {8, SO_RCVBUF},
    {9, SO_KEEPALIVE},  {10, SO_OOBINLINE}, {13, SO_LINGER},   {18, SO_RCVLOWAT},
    {19, SO_SNDLOWAT},  {20, SO_RCVTIMEO}, {21, SO_SNDTIMEO},  {30, SO_ACCEPTCONN},
};

const OptionMapping ipLevelOptions[] = {
    {1, IP_TOS},           {2, IP_TTL},
    {32, IP_MULTICAST_IF}, {33, IP_MULTICAST_TTL},
    {34, IP_MULTICAST_LOOP}, {35, IP_ADD_MEMBERSHIP},
    {36, IP_DROP_MEMBERSHIP},
};

bool translateOption(int &level, int &optname)
{
    if (level == linuxSolSocket)
    {
        for (const OptionMapping &mapping : socketLevelOptions)
        {
            if (mapping.guest != optname)
                continue;
            level = SOL_SOCKET;
            optname = mapping.host;
            return true;
        }
        log_warn("Network bridge: no Winsock equivalent for SOL_SOCKET option %d", optname);
        return false;
    }

    if (level == linuxIpprotoIp)
    {
        for (const OptionMapping &mapping : ipLevelOptions)
        {
            if (mapping.guest != optname)
                continue;
            optname = mapping.host;
            return true;
        }
        log_warn("Network bridge: no Winsock equivalent for IPPROTO_IP option %d", optname);
        return false;
    }

    // IPPROTO_TCP and its TCP_NODELAY happen to agree on both sides.
    if (level == linuxIpprotoTcp)
        return true;

    log_warn("Network bridge: unknown socket option level %d", level);
    return false;
}

struct LinuxLinger
{
    int32_t onOff;
    int32_t seconds;
};

struct LinuxTimeval
{
    int32_t seconds;
    int32_t microseconds;
};
} // namespace

extern "C" int bridgeSetsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> setsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    const int guestLevel = level;
    const int guestOption = optname;
    if (!translateOption(level, optname))
    {
        errno = ENOPROTOOPT;
        return -1;
    }

    int ret;
    if (guestLevel == linuxSolSocket && guestOption == linuxSoLinger &&
        optval && optlen >= static_cast<int>(sizeof(LinuxLinger)))
    {
        // Linux uses two ints, Winsock two shorts.
        const LinuxLinger *guest = reinterpret_cast<const LinuxLinger *>(optval);
        struct linger host = {};
        host.l_onoff = static_cast<u_short>(guest->onOff);
        host.l_linger = static_cast<u_short>(guest->seconds);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&host), sizeof(host));
    }
    else if (guestLevel == linuxSolSocket &&
             (guestOption == linuxSoRcvtimeo || guestOption == linuxSoSndtimeo) &&
             optval && optlen >= static_cast<int>(sizeof(LinuxTimeval)))
    {
        // Linux takes a timeval, Winsock a millisecond count.
        const LinuxTimeval *guest = reinterpret_cast<const LinuxTimeval *>(optval);
        DWORD milliseconds = static_cast<DWORD>(guest->seconds) * 1000u +
                             static_cast<DWORD>(guest->microseconds / 1000);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&milliseconds),
                         sizeof(milliseconds));
    }
    else
    {
        ret = setsockopt(s, level, optname, optval, optlen);
    }

    if (ret == SOCKET_ERROR)
        errno = mapWSAErrorToErrno(WSAGetLastError());
    return ret;
}

extern "C" int bridgeGetsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> getsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    const int guestLevel = level;
    const int guestOption = optname;
    if (!translateOption(level, optname))
    {
        errno = ENOPROTOOPT;
        return -1;
    }

    if (guestLevel == linuxSolSocket && guestOption == linuxSoLinger &&
        optval && optlen && *optlen >= static_cast<int>(sizeof(LinuxLinger)))
    {
        struct linger host = {};
        int hostLength = sizeof(host);
        const int ret = getsockopt(s, level, optname, reinterpret_cast<char *>(&host), &hostLength);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        LinuxLinger *guest = reinterpret_cast<LinuxLinger *>(optval);
        guest->onOff = host.l_onoff;
        guest->seconds = host.l_linger;
        *optlen = sizeof(LinuxLinger);
        return 0;
    }

    if (guestLevel == linuxSolSocket &&
        (guestOption == linuxSoRcvtimeo || guestOption == linuxSoSndtimeo) &&
        optval && optlen && *optlen >= static_cast<int>(sizeof(LinuxTimeval)))
    {
        DWORD milliseconds = 0;
        int hostLength = sizeof(milliseconds);
        const int ret = getsockopt(s, level, optname, reinterpret_cast<char *>(&milliseconds), &hostLength);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        LinuxTimeval *guest = reinterpret_cast<LinuxTimeval *>(optval);
        guest->seconds = static_cast<int32_t>(milliseconds / 1000u);
        guest->microseconds = static_cast<int32_t>((milliseconds % 1000u) * 1000u);
        *optlen = sizeof(LinuxTimeval);
        return 0;
    }

    const int ret = getsockopt(s, level, optname, optval, optlen);
    if (ret == SOCKET_ERROR)
        errno = mapWSAErrorToErrno(WSAGetLastError());
    return ret;
}

/*
 * boost::asio puts every socket into non-blocking mode through ioctl(FIONBIO)
 * and asks for the pending byte count with FIONREAD.  Both requests carry the
 * same numbers on Linux and Windows, but they have to reach ioctlsocket()
 * rather than the CRT's file ioctl, which knows nothing about sockets.
 */
extern "C" int bridgeSocketIoctl(int descriptor, unsigned long request, void *argument)
{
    const SOCKET handle = NetworkBridge::hostSocket(descriptor);
    constexpr unsigned long linuxFionread = 0x541B;
    constexpr unsigned long linuxFionbio = 0x5421;

    u_long value = 0;
    if (request == linuxFionbio || request == static_cast<unsigned long>(FIONBIO))
    {
        value = argument ? static_cast<u_long>(*static_cast<const int *>(argument)) : 0;
        const int ret = ioctlsocket(handle, FIONBIO, &value);
        if (ret == SOCKET_ERROR)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return ret;
    }

    if (request == linuxFionread || request == static_cast<unsigned long>(FIONREAD))
    {
        const int ret = ioctlsocket(handle, FIONREAD, &value);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        if (argument)
            *static_cast<int *>(argument) = static_cast<int>(value);
        return 0;
    }

    log_warn("Network bridge: unsupported socket ioctl 0x%lx", request);
    errno = EINVAL;
    return -1;
}

namespace
{
constexpr int guestFdSetBits = 1024;
constexpr size_t guestFdSetBytes = guestFdSetBits / 8;

bool guestFdIsSet(int descriptor, const void *set)
{
    if (!set || descriptor < 0 || descriptor >= guestFdSetBits)
        return false;
    const uint32_t *words = static_cast<const uint32_t *>(set);
    return (words[descriptor / 32] & (1u << (descriptor % 32))) != 0;
}

void guestFdSet(int descriptor, void *set)
{
    if (!set || descriptor < 0 || descriptor >= guestFdSetBits)
        return;
    uint32_t *words = static_cast<uint32_t *>(set);
    words[descriptor / 32] |= (1u << (descriptor % 32));
}

void guestFdZero(void *set)
{
    if (set)
        memset(set, 0, guestFdSetBytes);
}
} // namespace

extern "C" int bridgeSelectDescriptors(int nfds, void *readSet, void *writeSet, void *exceptSet,
                                       void *timeoutArgument)
{
    fd_set hostRead;
    fd_set hostWrite;
    fd_set hostExcept;
    FD_ZERO(&hostRead);
    FD_ZERO(&hostWrite);
    FD_ZERO(&hostExcept);

    int watchedSockets = 0;
    int ignoredDescriptors = 0;

    const int limit = nfds < guestFdSetBits ? nfds : guestFdSetBits;
    const uint32_t *readWords = static_cast<const uint32_t *>(readSet);
    const uint32_t *writeWords = static_cast<const uint32_t *>(writeSet);
    const uint32_t *exceptWords = static_cast<const uint32_t *>(exceptSet);

    for (int word = 0; word * 32 < limit; word++)
    {
        const uint32_t readBits = readWords ? readWords[word] : 0;
        const uint32_t writeBits = writeWords ? writeWords[word] : 0;
        const uint32_t exceptBits = exceptWords ? exceptWords[word] : 0;
        uint32_t anyBits = readBits | writeBits | exceptBits;
        if (!anyBits)
            continue;

        while (anyBits)
        {
            const int bit = __builtin_ctz(anyBits);
            anyBits &= anyBits - 1;

            const int descriptor = word * 32 + bit;
            if (descriptor >= limit)
                break;

            if (!NetworkBridge::isSocketDescriptor(descriptor))
            {
                ignoredDescriptors++;
                continue;
            }

            const SOCKET handle = NetworkBridge::hostSocket(descriptor);
            const uint32_t mask = 1u << bit;
            if (readBits & mask)
                FD_SET(handle, &hostRead);
            if (writeBits & mask)
                FD_SET(handle, &hostWrite);
            if (exceptBits & mask)
                FD_SET(handle, &hostExcept);
            watchedSockets++;
        }
    }

    if (ignoredDescriptors)
        log_trace("Network bridge: select() skipped %d non-socket descriptor(s)", ignoredDescriptors);

    const LinuxTimeval *guestTimeout = static_cast<const LinuxTimeval *>(timeoutArgument);

    int selected = 0;
    if (watchedSockets != 0)
    {
        TIMEVAL hostTimeout = {};
        TIMEVAL *hostTimeoutPointer = nullptr;
        if (guestTimeout)
        {
            hostTimeout.tv_sec = guestTimeout->seconds;
            hostTimeout.tv_usec = guestTimeout->microseconds;
            hostTimeoutPointer = &hostTimeout;
        }

        selected = select(0, &hostRead, &hostWrite, &hostExcept, hostTimeoutPointer);
        if (selected == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return -1;
        }
    }
    else if (guestTimeout)
    {
        // Winsock refuses a select() with nothing in it, so a wait on no
        // sockets is served by sleeping the caller's timeout out.
        Sleep(static_cast<DWORD>(guestTimeout->seconds) * 1000u +
              static_cast<DWORD>(guestTimeout->microseconds / 1000));
    }

    guestFdZero(readSet);
    guestFdZero(writeSet);
    guestFdZero(exceptSet);

    // POSIX counts every ready (descriptor, set) pair, not every descriptor.
    int count = 0;
    if (selected > 0)
    {
        for (u_int i = 0; i < hostRead.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostRead.fd_array[i]), readSet);
            count++;
        }
        for (u_int i = 0; i < hostWrite.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostWrite.fd_array[i]), writeSet);
            count++;
        }
        for (u_int i = 0; i < hostExcept.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostExcept.fd_array[i]), exceptSet);
            count++;
        }
    }

    return count;
}

extern "C" int bridgeSocketPair(int descriptors[2])
{
    if (!descriptors)
    {
        errno = EINVAL;
        return -1;
    }

    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;

    auto fail = [&]() {
        const int error = WSAGetLastError();
        if (listener != INVALID_SOCKET)
            closesocket(listener);
        if (client != INVALID_SOCKET)
            closesocket(client);
        if (server != INVALID_SOCKET)
            closesocket(server);
        errno = mapWSAErrorToErrno(error);
        log_error("Network bridge: could not build a loopback pipe pair (WSAError=%d)", error);
        return -1;
    };

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        return fail();

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR)
        return fail();

    int length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == SOCKET_ERROR)
        return fail();

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
        return fail();
    if (connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR)
        return fail();

    server = accept(listener, nullptr, nullptr);
    if (server == INVALID_SOCKET)
        return fail();

    closesocket(listener);
    listener = INVALID_SOCKET;

    // The wakeup is a single byte; Nagle must not sit on it.
    const int enabled = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled), sizeof(enabled));
    setsockopt(server, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled), sizeof(enabled));

    const int readEnd = NetworkBridge::registerSocket(server);
    const int writeEnd = NetworkBridge::registerSocket(client);
    if (readEnd < 0 || writeEnd < 0)
    {
        NetworkBridge::forgetSocket(readEnd);
        NetworkBridge::forgetSocket(writeEnd);
        closesocket(server);
        closesocket(client);
        errno = EMFILE;
        return -1;
    }

    descriptors[0] = readEnd;
    descriptors[1] = writeEnd;
    return 0;
}

int NetworkBridge::bridgeGethostbyname_r(const char *name, void *ret, char *buf, size_t buflen, void **result, int *h_errnop)
{
    std::lock_guard<std::mutex> lock(g_net_mutex);
    struct hostent *he = gethostbyname(name);
    if (!he)
    {
        if (h_errnop)
            *h_errnop = WSAGetLastError();
        if (result)
            *result = nullptr;
        return -1;
    }
    memcpy(ret, he, sizeof(struct hostent));
    if (result)
        *result = ret;
    return 0;
}

int NetworkBridge::bridgeGethostbyaddr_r(const void *addr, int len, int type, void *ret, char *buf, size_t buflen, void **result,
                                         int *h_errnop)
{
    std::lock_guard<std::mutex> lock(g_net_mutex);
    struct hostent *he = gethostbyaddr((const char *)addr, len, type);
    if (!he)
    {
        if (h_errnop)
            *h_errnop = WSAGetLastError();
        if (result)
            *result = nullptr;
        return -1;
    }
    memcpy(ret, he, sizeof(struct hostent));
    if (result)
        *result = ret;
    return 0;
}

extern "C" int bridgeGethostname(char *name, size_t namelen)
{
    // fallback to localhost if gethostname fails
    if (name == nullptr || namelen == 0)
    {
        return -1;
    }
    strncpy(name, "localhost", namelen - 1);
    name[namelen - 1] = '\0';
    return 0;
}

#endif
