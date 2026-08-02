#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int n2CardReaderOpen(const char *path, int flags);
int n2CardReaderIsConnected(void);
const char *n2CardReaderConnectionText(void);
void n2CardReaderRequestInsert(void);
void n2CardReaderRequestEject(void);
int n2CardReaderIsDescriptor(int fd);

/*
 * Bytes the reader has ready for the game, so poll()/select() can report the
 * descriptor as readable.  Returns 0 when nothing is pending or the pipe is
 * down; the descriptor is always writable.
 */
int n2CardReaderBytesAvailable(int fd);
int n2CardReaderRead(int fd, void *buffer, size_t count);
int n2CardReaderWrite(int fd, const void *buffer, size_t count);
int n2CardReaderClose(int fd);
int n2CardReaderIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
