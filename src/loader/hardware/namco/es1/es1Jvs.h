#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int es1JvsSerialEnabled(void);
int es1JvsSerialOpen(const char *path, int flags);
int es1JvsSerialIsDescriptor(int fd);
int es1JvsSerialBytesAvailable(int fd);
int es1JvsSerialRead(int fd, void *buffer, size_t count);
int es1JvsSerialWrite(int fd, const void *buffer, size_t count);
int es1JvsSerialClose(int fd);
int es1JvsSerialIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
