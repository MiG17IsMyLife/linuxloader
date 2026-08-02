#include "filesystem.h"

#if defined(PACLOADER_BUILD)

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <string.h>

#include "../config/config.h"
#include "../log/log.h"

char envpath[100] = {0};

const char *redirectTempPath(const char *path)
{
    if (!path)
        return path;
    if (strncmp(path, "/var/tmp", 8) == 0)
        return path + 5;
    if (strncmp(path, "/tmp", 4) == 0)
        return path + 1;
    if (strncmp(path, "../../../freespace", 18) == 0)
        return path + 9;

    static _Thread_local char rewritten[4][MAX_PATH_LENGTH];
    static _Thread_local unsigned int nextSlot = 0;
    const char *boardPath = NULL;
    if (strncmp(path, "/app/mnt/contents2/", 19) == 0)
        boardPath = path + 18;
    else if (strcmp(path, "/etc/axiscpp.conf") == 0)
        boardPath = path;

    if (!boardPath)
        return path;

    char *result = rewritten[nextSlot++ % 4];
    snprintf(result, MAX_PATH_LENGTH, "../..%s", boardPath);
    return result;
}

void ConvertPath(char *destination, const char *source, size_t size)
{
    if (!destination || !source || size == 0)
        return;
    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
    for (char *cursor = destination; *cursor; ++cursor)
        if (*cursor == '/')
            *cursor = '\\';
}

static const char *nativePath(const char *path, char buffer[MAX_PATH_LENGTH])
{
    ConvertPath(buffer, redirectTempPath(path), MAX_PATH_LENGTH);
    return buffer;
}

static int translateOpenFlags(int flags)
{
    int result = _O_BINARY;
    switch (flags & 3)
    {
        case 1: result |= _O_WRONLY; break;
        case 2: result |= _O_RDWR; break;
        default: result |= _O_RDONLY; break;
    }
    if (flags & 0x40) result |= _O_CREAT;
    if (flags & 0x80) result |= _O_EXCL;
    if (flags & 0x200) result |= _O_TRUNC;
    if (flags & 0x400) result |= _O_APPEND;
    if (flags & 0x80000) result |= _O_NOINHERIT;
    return result;
}

static int translateOpenMode(int mode)
{
    int result = 0;
    if (mode & 0400) result |= _S_IREAD;
    if (mode & 0200) result |= _S_IWRITE;
    return result ? result : (_S_IREAD | _S_IWRITE);
}

DIR *sharedOpendir(const char *path)
{
    char converted[MAX_PATH_LENGTH];
    return opendir(nativePath(path, converted));
}

int sharedRemove(const char *path)
{
    char converted[MAX_PATH_LENGTH];
    return remove(nativePath(path, converted));
}

int sharedMkdir(const char *path, mode_t mode)
{
    (void)mode;
    char converted[MAX_PATH_LENGTH];
    return _mkdir(nativePath(path, converted));
}

int sharedXstat64(int version, const char *path, struct stat64 *result)
{
    (void)version;
    char converted[MAX_PATH_LENGTH];
    return stat64(nativePath(path, converted), result);
}

int sharedOpen(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & 0x40)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    char converted[MAX_PATH_LENGTH];
    return _open(nativePath(path, converted), translateOpenFlags(flags), translateOpenMode(mode));
}

int sharedOpen64(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & 0x40)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    return sharedOpen(path, flags, mode);
}

FILE *sharedFopen(const char *path, const char *mode)
{
    char binaryMode[16];
    if (mode && strchr(mode, 'b') == NULL)
    {
        snprintf(binaryMode, sizeof(binaryMode), "%sb", mode);
        mode = binaryMode;
    }
    char converted[MAX_PATH_LENGTH];
    return fopen(nativePath(path, converted), mode);
}

FILE *sharedFopen64(const char *path, const char *mode)
{
    return sharedFopen(path, mode);
}

int sharedFclose(FILE *stream) { return fclose(stream); }

int sharedOpenat(int directory, const char *path, int flags, ...)
{
    (void)directory;
    int mode = 0;
    if (flags & 0x40)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    return sharedOpen(path, flags, mode);
}

int sharedClose(int fd) { return _close(fd); }
char *sharedFgets(char *buffer, int count, FILE *stream) { return fgets(buffer, count, stream); }
ssize_t sharedRead(int fd, void *buffer, size_t count)
{
    ssize_t result = _read(fd, buffer, (unsigned int)count);
    return result;
}
ssize_t sharedWrite(int fd, const void *buffer, size_t count) { return _write(fd, buffer, (unsigned int)count); }
size_t sharedFread(void *buffer, size_t size, size_t count, FILE *stream)
{
    size_t result = fread(buffer, size, count, stream);
    return result;
}
long int sharedFtell(FILE *stream) { return ftell(stream); }
int sharedFseek(FILE *stream, long int offset, int origin) { return fseek(stream, offset, origin); }
int64_t sharedFtello64(FILE *stream) { return _ftelli64(stream); }
int sharedFseeko64(FILE *stream, int64_t offset, int origin) { return _fseeki64(stream, offset, origin); }
void sharedRewind(FILE *stream) { rewind(stream); }

int sharedIoctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    errno = ENOTTY;
    return -1;
}

int sharedSelect(int nfds, fd_set *readSet, fd_set *writeSet,
                 fd_set *exceptSet, struct timeval *timeout)
{
    return bridgeSelectDescriptors(nfds, readSet, writeSet, exceptSet, timeout);
}

#endif
