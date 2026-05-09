#pragma once

#include <cstdio>
#include <stdint.h>
#include <string>
#include <windows.h>

#ifndef _DIRENT_H
#define _DIRENT_H

#include <windows.h>

struct linux_dirent
{
    long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[260];
};

struct DIR_Impl
{
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    struct linux_dirent ent;
    bool first_read;
    bool finished;
    std::string path;
};

#endif

struct linux_stat
{
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned short st_mode;
    unsigned short st_nlink;
    unsigned short st_uid;
    unsigned short st_gid;
    unsigned long st_rdev;
    unsigned long st_size;
    unsigned long st_blksize;
    unsigned long st_blocks;
    unsigned long st_atime;
    unsigned long st_atime_nsec;
    unsigned long st_mtime;
    unsigned long st_mtime_nsec;
    unsigned long st_ctime;
    unsigned long st_ctime_nsec;
    unsigned long __unused4;
    unsigned long __unused5;
};

struct linux_stat64
{
    unsigned long long st_dev;
    unsigned char __pad0[4];
    unsigned long __st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned long st_uid;
    unsigned long st_gid;
    unsigned long long st_rdev;
    unsigned char __pad3[4];
    long long st_size;
    unsigned long st_blksize;
    unsigned long long st_blocks;
    unsigned long st_atime;
    unsigned long st_atime_nsec;
    unsigned long st_mtime;
    unsigned long st_mtime_nsec;
    unsigned long st_ctime;
    unsigned long st_ctime_nsec;
    unsigned long long st_ino;
};

struct linux_stat_ver3
{
    uint64_t st_dev;
    uint16_t __pad1;
    uint16_t __reserved1;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint16_t __pad2;
    uint16_t __reserved2;
    int32_t st_size;
    uint32_t st_blksize;
    int32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

struct linux_stat64_safe
{
    uint64_t st_dev;
    uint8_t __pad0[4];
    uint32_t __st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint8_t __pad3[4];
    int64_t st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint64_t st_ino;
};

// =============================================================
//   File Type Flags (st_mode)
// =============================================================
#define LINUX_S_IFMT 0170000
#define LINUX_S_IFSOCK 0140000
#define LINUX_S_IFLNK 0120000
#define LINUX_S_IFREG 0100000
#define LINUX_S_IFBLK 0060000
#define LINUX_S_IFDIR 0040000
#define LINUX_S_IFCHR 0020000
#define LINUX_S_IFIFO 0010000

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

namespace FileSystemBridge
{
    void initBridges();

    // MSYS/Windows compatible file operations intercepting Linux calls
    size_t bridgeFwrite(const void *ptr, size_t size, size_t count, FILE *stream);
    int bridgeFerror(FILE *stream);
    int bridgeFeof(FILE *stream);
    int bridgeFgetc(FILE *stream);

    int bridgeFileno(FILE *stream);
    long int bridgeLseek(int fd, long int offset, int whence);
    int bridgeReadlink(const char *path, char *buf, size_t bufsiz);
    FILE* bridgeFdopen(int fd, const char* mode);
    int bridgeSetvbuf(FILE *stream, char *buf, int mode, size_t size);
    size_t bridgeWritev(int fd, const struct iovec *iov, int iovcnt);
    ssize_t bridgeReadv(int fd, const struct iovec *iov, int iovcnt);
    int bridgeDup(int fd);
    extern "C" int bridgeFsync(int fd);
    extern "C" int bridgeFdatasync(int fd);
    extern "C" void bridgeSync(void);
    int bridgeAccess(const char *pathname, int mode);
    int bridgeChdir(const char *path);
    int bridgeChmod(const char *filename, int pmode);

    int bridgeCreat(const char *pathname, int mode);
    char *bridgeGetcwd(char *buf, size_t size);
} // namespace FileSystemBridge
extern "C"
{
    int bridgeFputs(const char *str, FILE *stream);
    int bridgeFputc(int c, FILE *stream);
    int bridgeGetc(FILE *stream);
    int bridgeUngetc(int c, FILE *stream);

    void *bridgeOpendir(const char *name);
    struct linux_dirent *bridgeReaddir(void *dirp);
    int bridgeClosedir(void *dirp);

    int bridgeUnlink(const char *pathname);
    int bridgeRemove(const char *pathname);
    int bridgeRename(const char *oldpath, const char *newpath);

    int bridgeFstat(int fd, struct linux_stat64 *buf);
    int bridgeStat(const char *path, struct linux_stat64 *buf);
    int bridgeXstat(int ver, const char *path, struct linux_stat *buf);
    int bridgeXstat64(int ver, const char *path, struct linux_stat64 *buf);
    int bridgeLxstat(int ver, const char *path, struct linux_stat *buf);
    int bridgeLxstat64(int ver, const char *path, struct linux_stat64 *buf);
    int bridgeFxstat(int ver, int fd, struct linux_stat *buf);
    int bridgeFxstat64(int ver, int fd, struct linux_stat64 *buf);
    int bridgeXmknod(int ver, const char *path, unsigned int mode, void *dev);
    int bridgeFcntl(int fd, int cmd, ...);
}; // namespace FileSystemBridge
