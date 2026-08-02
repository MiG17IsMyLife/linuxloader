#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int n2CsNeoPcbEnabled(void);
int n2CsNeoPcbOpen(const char *path, int flags);
int n2CsNeoPcbIsDescriptor(int fd);
int n2CsNeoPcbBytesAvailable(int fd);
int n2CsNeoPcbRead(int fd, void *buffer, size_t count);
int n2CsNeoPcbWrite(int fd, const void *buffer, size_t count);
int n2CsNeoPcbClose(int fd);
int n2CsNeoPcbIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
