#if defined(_WIN32) || defined(__MINGW32__)
// #include <mutex>
#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "../log/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#include <cerrno>

static std::mutex g_net_mutex;

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
        MAP("shutdown", shutdown);
        MAP("setsockopt", bridgeSetsockopt);
        MAP("getsockopt", bridgeGetsockopt);
        MAP("inet_pton", bridgeInet_pton);
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
}; // namespace NetworkBridge

extern "C" SOCKET bridgeSocket(int af, int type, int protocol)
{
    log_info(">>> socket called: af=%d, type=%d, protocol=%d", af, type, protocol);
    SOCKET s = socket(af, type, protocol);
    if (s == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }
    log_info(">>> socket EXIT: returning %lld", (long long)s);
    return s;
}

extern "C" int bridgeConnect(SOCKET s, const struct sockaddr *name, int namelen)
{
    log_info(">>> connect called: socket=%lld", (long long)s);
    int ret = connect(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> connect EXIT: returning %d (WSAError=%d)", ret, ret < 0 ? WSAGetLastError() : 0);
    return ret;
}

extern "C" int bridgeBind(SOCKET s, const struct sockaddr *name, int namelen)
{
    log_info(">>> bind called: socket=%lld", (long long)s);
    int ret = bind(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> bind EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeListen(SOCKET s, int backlog)
{
    log_info(">>> listen called: socket=%lld, backlog=%d", (long long)s, backlog);
    int ret = listen(s, backlog);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> listen EXIT: returning %d", ret);
    return ret;
}

extern "C" SOCKET bridgeAccept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    log_info(">>> accept ENTRY: socket=%lld (THIS MAY BLOCK!)", (long long)s);
    SOCKET ret = accept(s, addr, addrlen);
    if (ret == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }
    log_info(">>> accept EXIT: returning %lld (WSAError=%d)", (long long)ret, ret == INVALID_SOCKET ? WSAGetLastError() : 0);
    return ret;
}

extern "C" int bridgeRecv(SOCKET s, char *buf, int len, int flags)
{
    log_info(">>> recv ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);

    if (flags & 0x40)
    {
        flags &= ~0x40;
    }

    int ret = recv(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> recv EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSend(SOCKET s, const char *buf, int len, int flags)
{
    log_info(">>> send called: socket=%lld, len=%d", (long long)s, len);

    flags &= ~(0x4000 | 0x40);

    int ret = send(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> send EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeRecvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    log_info(">>> recvfrom ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);
    if (flags & 0x40)
        flags &= ~0x40;
    int ret = recvfrom(s, buf, len, flags, from, fromlen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> recvfrom EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    log_info(">>> sendto called: socket=%lld, len=%d", (long long)s, len);
    flags &= ~(0x4000 | 0x40);
    int ret = sendto(s, buf, len, flags, to, tolen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> sendto EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSetsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    log_info(">>> setsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    if (level == 1)
    {
        level = 0xFFFF;
        if (optname == 2)
            optname = 0x0004;
    }

    int ret = setsockopt(s, level, optname, optval, optlen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> setsockopt EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeGetsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
    log_info(">>> getsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    if (level == 1)
    {
        level = 0xFFFF;
        if (optname == 2)
            optname = 0x0004;
    }

    int ret = getsockopt(s, level, optname, optval, optlen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    log_info(">>> getsockopt EXIT: returning %d", ret);
    return ret;
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