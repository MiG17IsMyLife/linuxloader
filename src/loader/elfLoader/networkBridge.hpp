#pragma once

#include <stdint.h>
#include <stddef.h>
#include <winsock2.h>

/*
 * Layout of the i386 Linux scatter/gather structures the game passes to
 * sendmsg()/recvmsg().  They are not the same shape as Winsock's WSABUF, whose
 * length field comes first, so the bridge translates rather than casts.
 */
struct LinuxIovec
{
    void *base;
    uint32_t length;
};

struct LinuxMsghdr
{
    void *name;
    uint32_t nameLength;
    LinuxIovec *iov;
    uint32_t iovCount;
    void *control;
    uint32_t controlLength;
    int32_t flags;
};

namespace NetworkBridge
{
    void initBridges();

    extern "C" unsigned long bridgeInet_addr(const char *cp);
    extern "C" int bridgeInet_aton(const char *cp, struct in_addr *inp);
    extern "C" int bridgeInet_pton(int af, const char *src, void *dst);
    extern "C" const char *bridgeInet_ntop(int af, const void *src, char *dst, size_t size);
    extern "C" char* bridgeInet_ntoa(struct in_addr in);
    extern "C" SOCKET bridgeSocket(int af, int type, int protocol);
    extern "C" int bridgeConnect(SOCKET s, const struct sockaddr *name, int namelen);
    extern "C" int bridgeBind(SOCKET s, const struct sockaddr *name, int namelen);
    extern "C" int bridgeListen(SOCKET s, int backlog);
    extern "C" SOCKET bridgeAccept(SOCKET s, struct sockaddr *addr, int *addrlen);
    extern "C" int bridgeRecv(SOCKET s, char *buf, int len, int flags);
    extern "C" int bridgeSend(SOCKET s, const char *buf, int len, int flags);
    extern "C" int bridgeRecvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
    extern "C" int bridgeSendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
    extern "C" int bridgeSendmsg(SOCKET s, const LinuxMsghdr *message, int flags);
    extern "C" int bridgeRecvmsg(SOCKET s, LinuxMsghdr *message, int flags);
    extern "C" int bridgeGetpeername(SOCKET s, struct sockaddr *name, int *namelen);
    extern "C" int bridgeGetsockname(SOCKET s, struct sockaddr *name, int *namelen);
    extern "C" int bridgeSetsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);
    extern "C" int bridgeGetsockopt(SOCKET s, int level, int optname, char *optval, int *optlen);
    extern "C" int bridgeShutdown(SOCKET s, int how);
    int bridgeGethostbyname_r(const char *name, void *ret, char *buf, size_t buflen, void **result, int *h_errnop);
    int bridgeGethostbyaddr_r(const void *addr, int len, int type, void *ret, char *buf, size_t buflen, void **result,
                                        int *h_errnop);
    extern "C" int bridgeGethostname(char *name, size_t namelen);
    extern "C" uint16_t bridgeNtohs(uint16_t netshort);
    extern "C" uint16_t bridgeHtons(uint16_t hostshort);
    extern "C" uint32_t bridgeNtohl(uint32_t netlong);
    extern "C" uint32_t bridgeHtonl(uint32_t hostlong);
    bool isSocketDescriptor(int descriptor);
    int bridgeSocketRead(int descriptor, void *buffer, size_t length);
    int bridgeSocketWrite(int descriptor, const void *buffer, size_t length);
    int bridgeSocketClose(int descriptor);
} // namespace NetworkBridge
