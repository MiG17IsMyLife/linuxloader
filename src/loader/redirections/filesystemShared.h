#pragma once

#ifdef __linux__
#include <sys/select.h>
#else
#include <winsock2.h>
#endif
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
typedef enum
{
    NO_DEVICE = 0,
    BASEBOARD = 1,
    EEPROM = 2,
    SERIAL0 = 3,
    SERIAL1 = 4,
    PCI_CARD_000 = 5
} DeviceType;

typedef enum
{
    CPUINFO = 0,
    OSRELEASE = 1,
    PCI_CARD_1F0 = 2,
    FILE_RW1 = 3,
    FILE_RW2 = 4,
    FILE_HARLEY = 5,
    FILE_FONT_ABC = 6,
    FILE_FONT_TGA = 7,
    FILE_LOGO_TGA = 8,
    ROUTE = 9
} FileTypes;

extern FILE *fileHooks[10];
extern FileTypes fileRead[10];

#ifdef __cplusplus
extern "C" {
#endif

void ConvertPath(char *dst, const char *src, size_t size);
const char *redirectTempPath(const char *path);
DIR *sharedOpendir(const char *dirname);
int sharedRemove(const char *path);
int sharedMkdir(const char *path, mode_t mode);
int sharedXstat64(int ver, const char *path, struct stat64 *stat_buf);
int sharedOpen(const char *pathname, int flags, ...);
int sharedOpen64(const char *pathname, int flags, ...);
int sharedWrite(int fd, const void *buf, size_t count);
FILE *sharedFopen(const char *pathname, const char *mode);
FILE *sharedFopen64(const char *pathname, const char *mode);
int sharedFclose(FILE *stream);
int sharedOpenat(int dirfd, const char *pathname, int flags, ...);
int sharedClose(int fd);
char *sharedFgets(char *str, int n, FILE *stream);
ssize_t sharedRead(int fd, void *buf, size_t count);
size_t sharedFread(void *buf, size_t size, size_t count, FILE *stream);
long int sharedFtell(FILE *stream);
int sharedFseek(FILE *stream, long int offset, int whence);
int64_t sharedFtello64(FILE *stream);
int sharedFseeko64(FILE *stream, int64_t offset, int whence);
void sharedRewind(FILE *stream);
int sharedIoctl(int fd, unsigned long int request, ...);
int sharedSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#if defined(_WIN32) || defined(__MINGW32__)
/*
 * Implemented by the network bridge.  The descriptor sets stay in the guest's
 * i386 Linux layout, which is not Winsock's fd_set, so they cross as void *.
 */
int bridgeSelectDescriptors(int nfds, void *readSet, void *writeSet, void *exceptSet, void *timeout);
#endif

#ifdef __cplusplus
}
#endif
