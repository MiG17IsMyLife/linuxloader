#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int n2CardReaderOpen(const char *path, int flags);
int n2CardReaderIsConnected(void);
int n2CardReaderIsDescriptor(int fd);
int n2CardReaderRead(int fd, void *buffer, size_t count);
int n2CardReaderWrite(int fd, const void *buffer, size_t count);
int n2CardReaderClose(int fd);
int n2CardReaderIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
