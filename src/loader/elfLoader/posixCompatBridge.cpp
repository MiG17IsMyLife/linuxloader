#if defined(_WIN32) || defined(__MINGW32__)

#include "posixCompatBridge.hpp"
#include "filesystemBridge.hpp"
#include "libcBridge.hpp"
#include "memoryManager.hpp"
#include "symbolResolver.hpp"

#include <glad/gl.h>
#include "../log/log.h"

#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace
{
// sysconf() names on i386 Linux.  Only the ones a program is likely to ask.
constexpr int linuxScArgMax = 0;
constexpr int linuxScChildMax = 1;
constexpr int linuxScClkTck = 2;
constexpr int linuxScNgroupsMax = 3;
constexpr int linuxScOpenMax = 4;
constexpr int linuxScPagesize = 30;
constexpr int linuxScNprocessorsConf = 83;
constexpr int linuxScNprocessorsOnln = 84;
constexpr int linuxScPhysPages = 85;
constexpr int linuxScAvphysPages = 86;

// The i386 Linux shape of struct sysinfo.
#pragma pack(push, 4)
struct LinuxSysinfo
{
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char _f[8];
};
#pragma pack(pop)
static_assert(sizeof(LinuxSysinfo) == 64, "i386 Linux struct sysinfo is 64 bytes");

int processorCount()
{
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);
    return static_cast<int>(info.dwNumberOfProcessors ? info.dwNumberOfProcessors : 1);
}
} // namespace

extern "C"
{
    long bridgeSysconf(int name)
    {
        switch (name)
        {
            case linuxScArgMax: return 32768;
            case linuxScChildMax: return 256;
            case linuxScClkTck: return 100;
            case linuxScNgroupsMax: return 32;
            case linuxScOpenMax: return 2048;
            case linuxScPagesize: return 4096;
            case linuxScNprocessorsConf:
            case linuxScNprocessorsOnln: return processorCount();
            case linuxScPhysPages:
            case linuxScAvphysPages:
            {
                MEMORYSTATUSEX status = {};
                status.dwLength = sizeof(status);
                if (!GlobalMemoryStatusEx(&status))
                    return -1;
                const unsigned long long bytes =
                    name == linuxScPhysPages ? status.ullTotalPhys : status.ullAvailPhys;
                return static_cast<long>(bytes / 4096ull);
            }
            default:
                log_debug("sysconf: no answer for name %d", name);
                errno = EINVAL;
                return -1;
        }
    }

    int bridgeSysinfo(struct LinuxSysinfo *info)
    {
        if (!info)
        {
            errno = EFAULT;
            return -1;
        }

        MEMORYSTATUSEX status = {};
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);

        std::memset(info, 0, sizeof(*info));
        info->uptime = static_cast<long>(GetTickCount64() / 1000ull);
        info->mem_unit = 1;
        info->totalram = static_cast<unsigned long>(status.ullTotalPhys > 0xFFFFFFFFull ? 0xFFFFFFFFull : status.ullTotalPhys);
        info->freeram = static_cast<unsigned long>(status.ullAvailPhys > 0xFFFFFFFFull ? 0xFFFFFFFFull : status.ullAvailPhys);
        info->totalswap = static_cast<unsigned long>(status.ullTotalPageFile > 0xFFFFFFFFull ? 0xFFFFFFFFull : status.ullTotalPageFile);
        info->freeswap = static_cast<unsigned long>(status.ullAvailPageFile > 0xFFFFFFFFull ? 0xFFFFFFFFull : status.ullAvailPageFile);
        info->procs = static_cast<unsigned short>(processorCount());
        return 0;
    }

    // Windows has no user database; the cabinet ran everything as one account.
    unsigned int bridgeGetuidLike() { return 0; }

    // struct passwd as i386 glibc lays it out.  All members are pointer-or-int
    // sized, so the host's natural alignment already matches.
    struct LinuxPasswd
    {
        char *pw_name;
        char *pw_passwd;
        unsigned int pw_uid;
        unsigned int pw_gid;
        char *pw_gecos;
        char *pw_dir;
        char *pw_shell;
    };
    static_assert(sizeof(struct LinuxPasswd) == 28, "i386 struct passwd is 28 bytes");


    int bridgeGetpwuid_r(unsigned int, void *passwd, char *buffer, size_t bufferSize, void **result)
    {
        if (result)
            *result = nullptr;
        if (!passwd || !buffer)
            return EINVAL;

        static const char *const fields[] = {"root", "x", "root", "/root", "/bin/sh"};
        size_t needed = 0;
        for (const char *field : fields)
            needed += std::strlen(field) + 1;
        if (bufferSize < needed)
            return ERANGE;

        char *copies[sizeof(fields) / sizeof(fields[0])];
        char *at = buffer;
        for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); index++)
        {
            copies[index] = at;
            const size_t length = std::strlen(fields[index]) + 1;
            std::memcpy(at, fields[index], length);
            at += length;
        }

        LinuxPasswd *entry = static_cast<LinuxPasswd *>(passwd);
        entry->pw_name = copies[0];
        entry->pw_passwd = copies[1];
        entry->pw_uid = 0;
        entry->pw_gid = 0;
        entry->pw_gecos = copies[2];
        entry->pw_dir = copies[3];
        entry->pw_shell = copies[4];

        if (result)
            *result = entry;
        return 0;
    }

    int bridgeChown(const char *, unsigned int, unsigned int) { return 0; }
    int bridgeSetpgid(int, int) { return 0; }
    int bridgeSetsid() { return 0; }
    int bridgeTcflow(int, int) { return 0; }
    int bridgePause()
    {
        // Waiting for a signal that cannot arrive; sleeping keeps the caller
        // off the CPU without pretending a signal was delivered.
        Sleep(INFINITE);
        return -1;
    }

    int bridgeMlock(const void *, size_t) { return 0; }
    int bridgeMunlock(const void *, size_t) { return 0; }
    int bridgeMsync(void *, size_t, int) { return 0; }

    int bridgeSetitimer(int, const void *, void *)
    {
        log_debug("setitimer: interval timers are not delivered");
        return 0;
    }

    void *bridgeSetmntent(const char *, const char *)
    {
        static int emptyTable = 0;
        return &emptyTable;
    }
    void *bridgeGetmntent(void *) { return nullptr; }
    int bridgeEndmntent(void *) { return 1; }

    // Message catalogues: always "not installed", so callers use their built-in
    // strings, which is what an untranslated cabinet build did anyway.
    void *bridgeCatopen(const char *, int) { return reinterpret_cast<void *>(-1); }
    char *bridgeCatgets(void *, int, int, const char *fallback) { return const_cast<char *>(fallback); }
    int bridgeCatclose(void *) { return -1; }

    const char *bridgeGnuGetLibcVersion() { return "2.3.6"; }

    int *bridgeHErrnoLocation()
    {
        static thread_local int hostError = 0;
        return &hostError;
    }

    const char *bridgeHstrerror(int error)
    {
        switch (error)
        {
            case 1: return "Unknown host";
            case 2: return "Host name lookup failure";
            case 3: return "Unknown server error";
            case 4: return "No address associated with name";
            default: return "Resolver error";
        }
    }

    char *bridgeCtermid(char *buffer)
    {
        static char console[] = "/dev/tty";
        if (!buffer)
            return console;
        std::strcpy(buffer, console);
        return buffer;
    }

    // glibc's reentrant float-to-string helpers.  snprintf reproduces them
    // closely enough for the digit strings callers actually use.
    int bridgeEcvt_r(double value, int digits, int *decimalPoint, int *sign, char *buffer, size_t length)
    {
        if (!buffer || !decimalPoint || !sign)
            return -1;
        char formatted[512];
        std::snprintf(formatted, sizeof(formatted), "%.*e", digits > 0 ? digits - 1 : 0, value);
        *sign = value < 0 ? 1 : 0;
        const char *exponent = std::strchr(formatted, 'e');
        *decimalPoint = exponent ? std::atoi(exponent + 1) + 1 : 0;

        size_t written = 0;
        for (const char *at = formatted; *at && written + 1 < length; at++)
        {
            if (*at == 'e')
                break;
            if (*at >= '0' && *at <= '9')
                buffer[written++] = *at;
        }
        buffer[written] = '\0';
        return 0;
    }

    int bridgeFcvt_r(double value, int digits, int *decimalPoint, int *sign, char *buffer, size_t length)
    {
        return bridgeEcvt_r(value, digits, decimalPoint, sign, buffer, length);
    }

    int bridgeMkstemp(char *templateName)
    {
        if (!templateName || _mktemp(templateName) == nullptr)
            return -1;
        return _open(templateName, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    }

    // The locale argument is ignored: the loader runs the guest in the "C"
    // locale, which is what a cabinet with no locale data had too.
    int bridgeStrcoll_l(const char *left, const char *right, void *) { return std::strcmp(left, right); }
    size_t bridgeStrxfrm_l(char *destination, const char *source, size_t length, void *)
    {
        const size_t needed = std::strlen(source);
        if (destination && length)
        {
            const size_t copied = needed < length - 1 ? needed : length - 1;
            std::memcpy(destination, source, copied);
            destination[copied] = '\0';
        }
        return needed;
    }

    /*
     * mmap64 differs from mmap only in taking a 64-bit offset, which arrives as
     * two stack words.  Nothing the guest maps is beyond 2 GB into a file, so
     * the low word is what gets passed on.
     */
    void *bridgeMmap64(void *address, size_t length, int protection, int flags, int descriptor, long long offset)
    {
        if (offset > 0x7FFFFFFFll || offset < 0)
        {
            log_error("mmap64: offset %lld is beyond what the file mapping bridge handles", offset);
            errno = EINVAL;
            return reinterpret_cast<void *>(-1);
        }
        return LibcBridge::bridgeMmap(address, length, protection, flags, descriptor, static_cast<long>(offset));
    }

    long bridgeTimegm(struct tm *brokenDown)
    {
        if (!brokenDown)
        {
            errno = EINVAL;
            return -1;
        }
        return static_cast<long>(_mkgmtime(brokenDown));
    }

    /*
     * A practical subset of strptime: the conversions a configuration or log
     * timestamp actually uses.  Anything else stops the scan and reports how far
     * it got, which is what the caller checks.
     */
    char *bridgeStrptime(const char *input, const char *format, struct tm *brokenDown)
    {
        if (!input || !format || !brokenDown)
            return nullptr;

        static const char *const monthNames[] = {"January", "February", "March", "April", "May", "June", "July",
                                                 "August", "September", "October", "November", "December"};
        static const char *const dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

        auto readNumber = [&](int maximumDigits, int *out) {
            while (*input == ' ')
                input++;
            int value = 0;
            int digits = 0;
            const int sign = (*input == '-') ? (input++, -1) : 1;
            while (digits < maximumDigits && *input >= '0' && *input <= '9')
            {
                value = value * 10 + (*input++ - '0');
                digits++;
            }
            if (digits == 0)
                return false;
            *out = value * sign;
            return true;
        };

        auto readName = [&](const char *const *names, int count, int *out) {
            for (int index = 0; index < count; index++)
            {
                const size_t full = std::strlen(names[index]);
                if (_strnicmp(input, names[index], full) == 0)
                {
                    input += full;
                    *out = index;
                    return true;
                }
                if (_strnicmp(input, names[index], 3) == 0)
                {
                    input += 3;
                    *out = index;
                    return true;
                }
            }
            return false;
        };

        for (const char *at = format; *at; at++)
        {
            if (*at != '%')
            {
                if (*at == ' ')
                {
                    while (*input == ' ')
                        input++;
                }
                else if (*input == *at)
                {
                    input++;
                }
                else
                {
                    return nullptr;
                }
                continue;
            }

            int value = 0;
            switch (*++at)
            {
                case 'Y': if (!readNumber(4, &value)) return nullptr; brokenDown->tm_year = value - 1900; break;
                case 'y': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_year = value < 69 ? value + 100 : value; break;
                case 'm': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_mon = value - 1; break;
                case 'd':
                case 'e': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_mday = value; break;
                case 'H': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_hour = value; break;
                case 'M': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_min = value; break;
                case 'S': if (!readNumber(2, &value)) return nullptr; brokenDown->tm_sec = value; break;
                case 'j': if (!readNumber(3, &value)) return nullptr; brokenDown->tm_yday = value - 1; break;
                case 'b':
                case 'B':
                case 'h': if (!readName(monthNames, 12, &value)) return nullptr; brokenDown->tm_mon = value; break;
                case 'a':
                case 'A': if (!readName(dayNames, 7, &value)) return nullptr; brokenDown->tm_wday = value; break;
                case 'n':
                case 't': while (*input == ' ' || *input == '\t' || *input == '\n') input++; break;
                case '%': if (*input++ != '%') return nullptr; break;
                default:
                    log_debug("strptime: conversion %%%c is not handled", *at);
                    return nullptr;
            }
        }

        return const_cast<char *>(input);
    }

    int bridgeGluBuild2DMipmaps(unsigned int target, int internalFormat, int width, int height,
                                unsigned int format, unsigned int type, const void *data)
    {
        constexpr unsigned int glGenerateMipmapParameter = 0x8191; // GL_GENERATE_MIPMAP
        glad_glTexParameteri(target, glGenerateMipmapParameter, 1);
        glad_glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
        return 0;
    }

    int bridgeAlphasort(const struct linux_dirent **left, const struct linux_dirent **right)
    {
        return std::strcoll((*left)->d_name, (*right)->d_name);
    }

    int bridgeScandir(const char *path, struct linux_dirent ***namelist,
                      int (*filter)(const struct linux_dirent *),
                      int (*compare)(const struct linux_dirent **, const struct linux_dirent **))
    {
        if (!path || !namelist)
        {
            errno = EINVAL;
            return -1;
        }

        void *directory = bridgeOpendir(path);
        if (!directory)
        {
            log_debug("scandir: cannot open %s", path);
            return -1;
        }

        struct linux_dirent **entries = nullptr;
        size_t count = 0;
        size_t capacity = 0;

        for (struct linux_dirent *entry = bridgeReaddir(directory); entry; entry = bridgeReaddir(directory))
        {
            if (filter && !filter(entry))
                continue;

            if (count == capacity)
            {
                const size_t wanted = capacity ? capacity * 2 : 32;
                auto *grown = static_cast<struct linux_dirent **>(
                    MemoryManager::customMalloc(wanted * sizeof(struct linux_dirent *)));
                if (!grown)
                    break;
                if (entries)
                {
                    std::memcpy(grown, entries, count * sizeof(struct linux_dirent *));
                    MemoryManager::customFree(entries);
                }
                entries = grown;
                capacity = wanted;
            }

            // Only the name that is actually there needs carrying, which is
            // what a real scandir hands back too.
            const size_t nameLength = std::strlen(entry->d_name) + 1;
            const size_t entrySize = offsetof(struct linux_dirent, d_name) + nameLength;
            auto *copy = static_cast<struct linux_dirent *>(MemoryManager::customMalloc(entrySize));
            if (!copy)
                break;
            std::memcpy(copy, entry, offsetof(struct linux_dirent, d_name));
            std::memcpy(copy->d_name, entry->d_name, nameLength);
            copy->d_reclen = static_cast<unsigned short>(entrySize);
            entries[count++] = copy;
        }

        bridgeClosedir(directory);

        if (compare && count > 1)
            std::qsort(entries, count, sizeof(struct linux_dirent *),
                       reinterpret_cast<int (*)(const void *, const void *)>(compare));

        *namelist = entries;
        return static_cast<int>(count);
    }

    ssize_t bridgeGetdelim(char **line, size_t *capacity, int delimiter, FILE *stream)
    {
        if (!line || !capacity || !stream)
        {
            errno = EINVAL;
            return -1;
        }

        size_t written = 0;
        for (;;)
        {
            const int character = fgetc(stream);
            if (character == EOF)
                break;

            if (!*line || written + 2 > *capacity)
            {
                const size_t wanted = *capacity ? *capacity * 2 : 128;
                char *grown = static_cast<char *>(realloc(*line, wanted));
                if (!grown)
                {
                    errno = ENOMEM;
                    return -1;
                }
                *line = grown;
                *capacity = wanted;
            }

            (*line)[written++] = static_cast<char>(character);
            if (character == delimiter)
                break;
        }

        if (written == 0)
            return -1;
        (*line)[written] = '\0';
        return static_cast<ssize_t>(written);
    }
}

namespace PosixCompatBridge
{
    void initBridges()
    {
        log_info("Initializing POSIX compatibility Bridges...");

        MAP("sysconf", bridgeSysconf);
        MAP("sysinfo", bridgeSysinfo);

        MAP("geteuid", bridgeGetuidLike);
        MAP("getegid", bridgeGetuidLike);
        MAP("getgid", bridgeGetuidLike);
        MAP("getppid", bridgeGetuidLike);
        MAP("getpwuid_r", bridgeGetpwuid_r);
        MAP("chown", bridgeChown);
        MAP("fchown", bridgeChown);
        MAP("setpgid", bridgeSetpgid);
        MAP("setsid", bridgeSetsid);
        MAP("tcflow", bridgeTcflow);
        MAP("pause", bridgePause);

        MAP("mlock", bridgeMlock);
        MAP("munlock", bridgeMunlock);
        MAP("msync", bridgeMsync);
        MAP("setitimer", bridgeSetitimer);

        MAP("setmntent", bridgeSetmntent);
        MAP("getmntent", bridgeGetmntent);
        MAP("endmntent", bridgeEndmntent);

        MAP("catopen", bridgeCatopen);
        MAP("catgets", bridgeCatgets);
        MAP("catclose", bridgeCatclose);

        MAP("gnu_get_libc_version", bridgeGnuGetLibcVersion);
        MAP("__h_errno_location", bridgeHErrnoLocation);
        MAP("hstrerror", bridgeHstrerror);
        MAP("ctermid", bridgeCtermid);

        MAP("ecvt_r", bridgeEcvt_r);
        MAP("fcvt_r", bridgeFcvt_r);
        MAP("qecvt_r", bridgeEcvt_r);
        MAP("qfcvt_r", bridgeFcvt_r);

        MAP("scandir", bridgeScandir);
        MAP("scandir64", bridgeScandir);
        MAP("alphasort", bridgeAlphasort);
        MAP("alphasort64", bridgeAlphasort);
        MAP("versionsort", bridgeAlphasort);

        MAP("mkstemp", bridgeMkstemp);
        MAP("mkstemp64", bridgeMkstemp);
        MAP("__getdelim", bridgeGetdelim);
        MAP("getdelim", bridgeGetdelim);

        MAP("__strcoll_l", bridgeStrcoll_l);
        MAP("__strxfrm_l", bridgeStrxfrm_l);
        MAP("strcoll_l", bridgeStrcoll_l);
        MAP("strxfrm_l", bridgeStrxfrm_l);
        MAP("__wcscoll_l", LibcBridge::bridgeWcscoll_l);
        MAP("__wcsxfrm_l", LibcBridge::bridgeWcsxfrm_l);
        MAP("__towlower_l", LibcBridge::bridgeTowlower_l);
        MAP("__towupper_l", LibcBridge::bridgeTowupper_l);
        MAP("nl_langinfo_l", LibcBridge::bridgeNlLanginfo);

        MAP("mmap64", bridgeMmap64);
        MAP("timegm", bridgeTimegm);
        MAP("strptime", bridgeStrptime);
        MAP("gluBuild2DMipmaps", bridgeGluBuild2DMipmaps);

        // Names the CRT already provides under the same meaning.
        MAP("_longjmp", LibcBridge::bridgeLongjmp);
        MAP("clock", clock);
        MAP("ctime", ctime);
        MAP("gmtime", gmtime);
        MAP("difftime", difftime);
        MAP("putenv", putenv);
        MAP("rmdir", rmdir);
        MAP("setbuf", setbuf);
        MAP("tmpfile", tmpfile);
        MAP("freopen", freopen);
        MAP("freopen64", freopen);
        MAP("fgetpos", fgetpos);
        MAP("fsetpos", fsetpos);
        MAP("execl", execl);
        MAP("execvp", execvp);
    }
} // namespace PosixCompatBridge

#endif
