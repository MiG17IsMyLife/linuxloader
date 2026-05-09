#pragma once

#include <stdint.h>
#include <stddef.h>
#include <winsock2.h>

namespace NetworkBridge
{
    void initBridges();

    extern "C" unsigned long bridgeInet_addr(const char *cp);
    extern "C" int bridgeInet_aton(const char *cp, struct in_addr *inp);
    extern "C" int bridgeInet_pton(int af, const char *src, void *dst);
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
} // namespace NetworkBridge
