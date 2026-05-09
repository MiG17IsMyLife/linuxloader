#if defined(_WIN32) || defined(__MINGW32__)
#include "../redirections/filesystemShared.h"
#include "filesystemBridge.hpp"
#include "libcBridge.hpp"
#include "symbolResolver.hpp"
#include "../log/log.h"
#include <string>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <cstdarg>
#include "../config/config.h"

extern std::string g_absoluteElfPath;

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

static char g_dlErrorBuf[256];

namespace FileSystemBridge
{

    void initBridges()
    {
        log_info("Initializing FileSystemBridge...");

        // Standard I/O functions
        MAP("fopen", sharedFopen);
        MAP("fread", sharedFread);
        MAP("fwrite", bridgeFwrite);
        MAP("fseek", sharedFseek);
        MAP("ftell", sharedFtell);
        MAP("fclose", sharedFclose);
        MAP("ferror", bridgeFerror);
        MAP("rewind", sharedRewind);
        MAP("feof", bridgeFeof);
        MAP("fgets", sharedFgets);
        MAP("fgetc", bridgeFgetc);
        MAP("fflush", fflush);
        MAP("fputs", bridgeFputs);
        MAP("fputc", bridgeFputc);
        MAP("getc", bridgeGetc);
        MAP("ungetc", bridgeUngetc);

        MAP("flock", LibcBridge::bridgeStubSuccess);

        // LFS (Large File Support) functions
        MAP("fopen64", sharedFopen);
        MAP("fseeko64", sharedFseek);
        MAP("lseek64", _lseeki64);
        MAP("ftello64", sharedFtell);

        MAP("fileno", bridgeFileno);
        MAP("_fileno", bridgeFileno);

        // POSIX low-level I/O functions
        MAP("read", sharedRead);
        MAP("write", sharedWrite);
        MAP("__write", sharedWrite);
        MAP("close", sharedClose);
        MAP("lseek", bridgeLseek);
        MAP("open", sharedOpen);
        MAP("open64", sharedOpen);
        MAP("readlink", bridgeReadlink);
        MAP("fdopen", bridgeFdopen);
        MAP("setvbuf", bridgeSetvbuf);
        MAP("writev", bridgeWritev);
        MAP("readv", bridgeReadv);
        MAP("dup", bridgeDup);

        // file IO
        MAP("fsync", bridgeFsync);
        MAP("sync", bridgeSync);
        MAP("fdatasync", bridgeFdatasync);
        MAP("access", bridgeAccess);
        MAP("chmod", bridgeChmod);
        MAP("chdir", bridgeChdir);


        MAP("ioctl", sharedIoctl);
        MAP("select", sharedSelect);
        MAP("creat", bridgeCreat);
        MAP("mkdir", sharedMkdir);
        MAP("getcwd", bridgeGetcwd);
        MAP("opendir", bridgeOpendir);
        MAP("readdir", bridgeReaddir);
        MAP("closedir", bridgeClosedir);
        MAP("clearerr", clearerr);

        MAP("unlink", bridgeUnlink);
        MAP("remove", bridgeRemove);
        MAP("rename", bridgeRename);

        MAP("stat", bridgeStat);
        MAP("fstat", bridgeFstat);
        MAP("__xstat", bridgeXstat);
        MAP("__xstat64", bridgeXstat64);
        MAP("__lxstat", bridgeLxstat);
        MAP("__fxstat", bridgeFxstat);
        MAP("__fxstat64", bridgeFxstat64);
        MAP("__xmknod", bridgeXmknod);
        MAP("fcntl", bridgeFcntl);
        MAP("__lxstat64", bridgeLxstat64);
    }

    size_t bridgeFwrite(const void *ptr, size_t size, size_t count, FILE *stream)
    {
        log_trace("Intercepted fwrite: %p %zu %zu %p", ptr, size, count, stream);
        return fwrite(ptr, size, count, stream);
    }

    int bridgeFerror(FILE *stream)
    {
        log_trace("Intercepted ferror: %p", stream);
        return ferror(stream);
    }

    int bridgeFeof(FILE *stream)
    {
        log_trace("Intercepted feof: %p", stream);
        return feof(stream);
    }

    int bridgeFgetc(FILE *stream)
    {
        log_trace("Intercepted fgetc: %p", stream);
        return fgetc(stream);
    }

    int bridgeFileno(FILE *stream)
    {
        log_trace("Intercepted fileno: %p", stream);
        return fileno(stream);
    }

    long int bridgeLseek(int fd, long int offset, int whence)
    {
        log_trace("Intercepted lseek: %d %ld %d", fd, offset, whence);
        return lseek(fd, offset, whence);
    }

    int bridgeReadlink(const char *path, char *buf, size_t bufsiz)
    {
        log_trace("Intercepted readlink: %s", path ? path : "NULL");
        if (!path || !buf || bufsiz == 0)
            return -1;

        if (strcmp(path, "/proc/self/exe") == 0)
        {
            const char *elfPath = g_absoluteElfPath.c_str();
            if (elfPath[0] == '\0')
            {
                log_warn("readlink: /proc/self/exe requested but ELF path not set");
                return -1;
            }
            size_t len = strlen(elfPath);
            if (len > bufsiz)
                len = bufsiz;
            memcpy(buf, elfPath, len);
            return (int)len;
        }
        log_error("readlink not implemented for %s", path);
        return -1;
    }
    
    FILE* bridgeFdopen(int fd, const char* mode)
    {
        log_trace("Intercepted fdopen");
        return fdopen(fd, mode);
    }

    int bridgeSetvbuf(FILE *stream, char *buf, int mode, size_t size)
    {
        log_trace("Intercepted setvbuf");
        return setvbuf(stream, buf, mode, size);
    }

    size_t bridgeWritev(int fd, const struct iovec *iov, int iovcnt)
    {
        log_trace("Intercepted writev");
        size_t total_written = 0;
        for (int i = 0; i < iovcnt; ++i) {
            int written = _write(fd, iov[i].iov_base, iov[i].iov_len);
            if (written < 0) {
                if (total_written == 0) return -1;
                break;
            }
            total_written += written;
            if ((size_t)written < iov[i].iov_len) break;
        }
        return total_written;
    }

    ssize_t bridgeReadv(int fd, const struct iovec *iov, int iovcnt)
    {
        log_trace("Intercepted readv");
        ssize_t total_read = 0;
        for (int i = 0; i < iovcnt; ++i) {
            int bytes_read = _read(fd, iov[i].iov_base, iov[i].iov_len);
            if (bytes_read < 0) {
                if (total_read == 0) return -1;
                break;
            }
            total_read += bytes_read;
            if ((size_t)bytes_read < iov[i].iov_len) break;
            if (bytes_read == 0) break; // EOF
        }
        return total_read;
    }

    int bridgeDup(int fd)
    {
        log_trace("Intercepted dup: %d", fd);
        return dup(fd);
    }

    extern "C" int bridgeFsync(int fd)
    {
        return _commit(fd);
    }
    extern "C" int bridgeFdatasync(int fd)
    {
        return _commit(fd);
    }
    extern "C" void bridgeSync(void)
    {
        _flushall();
    }

    int bridgeAccess(const char *pathname, int mode)
    {
        if (pathname && strstr(pathname, "/dev/") != NULL)
        {
            return 0; // Success
        }
        if (mode == 1)
            mode = 0;
        return _access(pathname, mode);
    }

    int bridgeChdir(const char *path)
    {
        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);
        log_debug("chdir: %s (as %s)", path, winPath);
        return _chdir(winPath);
    }

    int bridgeChmod(const char *filename, int pmode)
    {
        return _chmod(filename, pmode);
    }

    int bridgeCreat(const char *pathname, int mode)
    {
        if (strncmp(pathname, "/tmp", 4) == 0)
            pathname += 1;

        if (strncmp(pathname, "/var/tmp", 8) == 0)
            pathname += 5;

        char winPath[MAX_PATH];
        ConvertPath(winPath, pathname, MAX_PATH);
        return _creat(winPath, mode);
    }

    char *bridgeGetcwd(char *buf, size_t size)
    {
        if (buf != nullptr)
            return _getcwd(buf, (int)size);
        char *tmp = _getcwd(NULL, 0);
        if (!tmp)
            return nullptr;
        size_t len = strlen(tmp) + 1;
        if (size > 0 && size < len)
            len = size;
        char *aligned_ptr = (char *)_aligned_malloc(len, 16);
        if (aligned_ptr)
        {
            strncpy(aligned_ptr, tmp, len);
            aligned_ptr[len - 1] = '\0';
        }
        _aligned_free(tmp);
        return aligned_ptr;
    }
} // namespace FileSystemBridge

extern "C"
{
    void *bridgeOpendir(const char *name)
    {
        log_debug("opendir(\"%s\")", name);
        char winPath[MAX_PATH];
        ConvertPath(winPath, name, MAX_PATH);

        if (strcmp(winPath, "\\home\\disk1\\rankingdata") == 0 &&
            (getConfig()->gameGroup == GROUP_OUTRUN || getConfig()->gameGroup == GROUP_OUTRUN_TEST))
        {
            strcpy(winPath, ".\\rankingdata");
        }

        std::string searchPath = winPath;
        if (searchPath.empty())
            return NULL;

        if (searchPath.back() == '/' || searchPath.back() == '\\')
        {
            searchPath += "*";
        }
        else
        {
            searchPath += "\\*";
        }

        DIR_Impl *dir = new DIR_Impl();
        dir->path = name; // logging uses original name
        dir->hFind = FindFirstFileA(searchPath.c_str(), &dir->findData);

        if (dir->hFind == INVALID_HANDLE_VALUE)
        {
            delete dir;
            return NULL;
        }

        dir->first_read = true;
        dir->finished = false;
        return (void *)dir;
    }

    struct linux_dirent *bridgeReaddir(void *dirp)
    {
        if (!dirp)
            return NULL;
        DIR_Impl *dir = (DIR_Impl *)dirp;

        if (dir->finished)
            return NULL;

        if (dir->first_read)
        {
            dir->first_read = false;
        }
        else
        {
            if (!FindNextFileA(dir->hFind, &dir->findData))
            {
                dir->finished = true;
                return NULL;
            }
        }

        memset(&dir->ent, 0, sizeof(linux_dirent));
        dir->ent.d_ino = 1;
        dir->ent.d_off = 0;

        strncpy(dir->ent.d_name, dir->findData.cFileName, 260);
        dir->ent.d_reclen = (unsigned short)strlen(dir->ent.d_name);

        if (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            dir->ent.d_type = 4; // DT_DIR
        }
        else
        {
            dir->ent.d_type = 8; // DT_REG
        }

        return &dir->ent;
    }

    int bridgeClosedir(void *dirp)
    {
        if (!dirp)
            return -1;
        DIR_Impl *dir = (DIR_Impl *)dirp;
        if (dir->hFind != INVALID_HANDLE_VALUE)
        {
            FindClose(dir->hFind);
        }
        delete dir;
        return 0;
    }


    int bridgeUnlink(const char *pathname)
    {
        if (strncmp(pathname, "/tmp", 4) == 0)
        {
            pathname += 1;
        }
        return _unlink(pathname);
    }

    int bridgeRemove(const char *pathname)
    {
        if (strncmp(pathname, "/home/disk1/rankingdata/", 25) == 0)
        {
            pathname += 11;
            char winPath[MAX_PATH];
            ConvertPath(winPath, pathname, MAX_PATH);
            return remove(winPath);
        }
        return remove(pathname);
    }

    int bridgeRename(const char *oldpath, const char *newpath)
    {
        log_debug("rename(\" % s\", \" % s\")", oldpath, newpath);
        char winOld[MAX_PATH];
        char winNew[MAX_PATH];
        ConvertPath(winOld, oldpath, MAX_PATH);
        ConvertPath(winNew, newpath, MAX_PATH);
        return rename(winOld, winNew);
    }

    static void CopyStatVer3(const struct _stat64 &src, void *dst_ptr)
    {
        struct linux_stat_ver3 *dst = (struct linux_stat_ver3 *)dst_ptr;
        memset(dst, 0, sizeof(struct linux_stat_ver3));

        dst->st_dev = (uint64_t)src.st_dev;
        dst->st_ino = (uint32_t)src.st_ino;
        dst->st_nlink = src.st_nlink;
        dst->st_uid = src.st_uid;
        dst->st_gid = src.st_gid;
        dst->st_rdev = (uint64_t)src.st_rdev;
        dst->st_size = (int32_t)src.st_size; // 32-bit size
        dst->st_blksize = 4096;
        dst->st_blocks = (int32_t)((src.st_size + 511) / 512);
        dst->st_atime = (uint32_t)src.st_atime;
        dst->st_mtime = (uint32_t)src.st_mtime;
        dst->st_ctime = (uint32_t)src.st_ctime;

        dst->st_mode = 0;
        if (src.st_mode & _S_IFDIR)
            dst->st_mode |= LINUX_S_IFDIR;
        if (src.st_mode & _S_IFREG)
            dst->st_mode |= LINUX_S_IFREG;
        if (src.st_mode & _S_IFCHR)
            dst->st_mode |= LINUX_S_IFCHR;
        if (src.st_mode & _S_IFIFO)
            dst->st_mode |= LINUX_S_IFIFO;
        dst->st_mode |= (src.st_mode & 0777);
    }

    static void CopyStat64(const struct _stat64 &src, void *dst_ptr)
    {
        struct linux_stat64_safe dst;
        memset(&dst, 0, sizeof(dst));

        dst.st_dev = (unsigned long long)src.st_dev;
        dst.__st_ino = (uint32_t)src.st_ino;
        dst.st_ino = (unsigned long long)src.st_ino;
        dst.st_nlink = src.st_nlink;
        dst.st_uid = src.st_uid;
        dst.st_gid = src.st_gid;
        dst.st_rdev = (unsigned long long)src.st_rdev;
        dst.st_size = src.st_size;
        dst.st_blksize = 4096;
        dst.st_blocks = (src.st_size + 511) / 512;
        dst.st_atime = (unsigned long)src.st_atime;
        dst.st_mtime = (unsigned long)src.st_mtime;
        dst.st_ctime = (unsigned long)src.st_ctime;

        dst.st_mode = 0;
        if (src.st_mode & _S_IFDIR)
            dst.st_mode |= LINUX_S_IFDIR;
        if (src.st_mode & _S_IFREG)
            dst.st_mode |= LINUX_S_IFREG;
        if (src.st_mode & _S_IFCHR)
            dst.st_mode |= LINUX_S_IFCHR;
        if (src.st_mode & _S_IFIFO)
            dst.st_mode |= LINUX_S_IFIFO;
        dst.st_mode |= (src.st_mode & 0777);

        memcpy(dst_ptr, &dst, sizeof(dst));
    }

    static int myStatImpl(const char *path, void *buf, bool use_stat64)
    {
        if (strncmp(path, "/tmp", 4) == 0)
            path += 1;

        if (strncmp(path, "/var/tmp", 8) == 0)
            path += 5;

        if (path && (strstr(path, "/dev/") != NULL || strstr(path, "i2c/") != NULL || strstr(path, "ttyS") != NULL))
        {
            log_debug("stat: Spoofing virtual device for %s", path);

            if (use_stat64)
            {
                struct linux_stat64_safe *s = (struct linux_stat64_safe *)buf;
                memset(s, 0, sizeof(*s));
                s->st_mode = LINUX_S_IFCHR | 0666;
                s->st_rdev = 1;
                s->st_nlink = 1;
            }
            else
            {
                struct linux_stat_ver3 *s = (struct linux_stat_ver3 *)buf;
                memset(s, 0, sizeof(*s));
                s->st_mode = LINUX_S_IFCHR | 0666;
                s->st_rdev = 1;
                s->st_nlink = 1;
            }
            return 0;
        }

        struct _stat64 win_stat;
        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);
        log_debug("stat: %s (as %s)", path, winPath);
        if (_stat64(winPath, &win_stat) != 0)
        {
            log_debug("stat failed: %s", path);
            errno = ENOENT;
            return -1;
        }

        if (use_stat64)
        {
            CopyStat64(win_stat, buf);
        }
        else
        {
            CopyStatVer3(win_stat, buf);
        }

        return 0;
    }

    int bridgeStat(const char *path, struct linux_stat64 *buf)
    {
        return myStatImpl(path, buf, false);
    }

    int bridgeFstat(int fd, struct linux_stat64 *buf)
    {
        log_debug("fstat called: fd=%d", fd);
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;
        CopyStatVer3(win_stat, buf);
        return 0;
    }

    int bridgeXstat(int ver, const char *path, struct linux_stat *buf)
    {
        log_debug("__xstat called: ver=%d, path=\"%s\"", ver, path);
        return myStatImpl(path, buf, false);
    }

    int bridgeXstat64(int ver, const char *path, struct linux_stat64 *buf)
    {
        log_debug("__xstat64 called: ver=%d, path=\" % s\"", ver, path);
        return myStatImpl(path, buf, true);
    }

    int bridgeLxstat(int ver, const char *path, struct linux_stat *buf)
    {
        log_debug("__lxstat called: ver=%d, path=\" % s\"", ver, path);
        return bridgeXstat(ver, path, buf);
    }

    int bridgeLxstat64(int ver, const char *path, struct linux_stat64 *buf)
    {
        log_debug("__lxstat64 called: ver=%d, path=\" % s\"", ver, path);
        return bridgeXstat64(ver, path, buf);
    }

    int bridgeFxstat(int ver, int fd, struct linux_stat *buf)
    {
        log_debug("__fxstat called: ver=%d, fd=%d", ver, fd);
        if (!buf)
            return -1;
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;

        CopyStatVer3(win_stat, buf);
        return 0;
    }

    int bridgeFxstat64(int ver, int fd, struct linux_stat64 *buf)
    {
        log_debug("__fxstat64 called: ver=%d, fd=%d", ver, fd);
        if (!buf)
            return -1;
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;

        if (ver == 3)
        {
            CopyStatVer3(win_stat, buf);
        }
        else
        {
            CopyStat64(win_stat, buf);
        }
        return 0;
    }

    int bridgeXmknod(int ver, const char *path, unsigned int mode, void *dev)
    {
        log_debug("__xmknod called: ver=%d, path=\"%s\", mode=0x%x", ver, path, mode);
        return 0;
    }

    int bridgeFcntl(int fd, int cmd, ...)
    {
        log_debug("fcntl(fd=%d, cmd=%d)", fd, cmd);
        return 0;
    }

    int bridgeFputc(int c, FILE *stream)
    {
        if (!stream)
            return EOF;

        int ret = EOF;
        ret = fputc(c, stream);

        if (ret == EOF)
        {
            return c;
        }
        return ret;
    }

    int bridgeFputs(const char *str, FILE *stream)
    {
        if (!stream)
            return EOF;

        int ret = EOF;
        ret = fputs(str, stream);

        if (ret == EOF)
        {
            return 0;
        }
        return ret;
    }

    int bridgeGetc(FILE *stream)
    {
        log_trace("Intercepted getc: %p", stream);
        return fgetc(stream);
    }

    int bridgeUngetc(int c, FILE *stream)
    {
        log_trace("Intercepted ungetc: %d %p", c, stream);
        return ungetc(c, stream);
    }

}

#endif