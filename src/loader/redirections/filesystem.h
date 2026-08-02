#pragma once

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <winsock2.h>

#ifdef __cplusplus
extern "C" {
#endif

void ConvertPath(char *destination, const char *source, size_t size);
const char *redirectTempPath(const char *path);
DIR *sharedOpendir(const char *path);
int sharedRemove(const char *path);
int sharedMkdir(const char *path, mode_t mode);
int sharedXstat64(int version, const char *path, struct stat64 *result);
int sharedOpen(const char *path, int flags, ...);
int sharedOpen64(const char *path, int flags, ...);
int sharedWrite(int fd, const void *buffer, size_t count);
FILE *sharedFopen(const char *path, const char *mode);
FILE *sharedFopen64(const char *path, const char *mode);
int sharedFclose(FILE *stream);
int sharedOpenat(int directory, const char *path, int flags, ...);
int sharedClose(int fd);
char *sharedFgets(char *buffer, int count, FILE *stream);
ssize_t sharedRead(int fd, void *buffer, size_t count);
size_t sharedFread(void *buffer, size_t size, size_t count, FILE *stream);
long int sharedFtell(FILE *stream);
int sharedFseek(FILE *stream, long int offset, int origin);
int64_t sharedFtello64(FILE *stream);
int sharedFseeko64(FILE *stream, int64_t offset, int origin);
void sharedRewind(FILE *stream);
int sharedIoctl(int fd, unsigned long request, ...);
int sharedSelect(int nfds, fd_set *readSet, fd_set *writeSet,
                 fd_set *exceptSet, struct timeval *timeout);
int bridgeSelectDescriptors(int nfds, void *readSet, void *writeSet,
                            void *exceptSet, void *timeout);

#ifdef __cplusplus
}
#endif
