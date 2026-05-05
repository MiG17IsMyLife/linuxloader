#if defined(_WIN32) || defined(__MINGW32__)
#include "../redirections/filesystemShared.h"
#include "libcBridge.hpp"
#include "../redirections/libcShared.h"
#include "../log/log.h"
#include "gccBridge.hpp"
#include "symbolResolver.hpp"
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <process.h>
#include <sys/time.h>
#include <sys/utime.h>
#include <wctype.h>
#include <windows.h>
#include <time.h>
#include <unistd.h>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

extern "C"
{
    // libgcc / compiler-rt internal integer division/conversion compiler builtins
    long long __divdi3(long long a, long long b);
    unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
    unsigned long long __umoddi3(unsigned long long a, unsigned long long b);
    long long __moddi3(long long a, long long b);
    unsigned long long __fixunsdfdi(double a);
    unsigned long long __fixunssfdi(float a);

    // glibc internal string to number conversion functions
    double __strtod_internal(const char *nptr, char **endptr, int group);
    long int __strtol_internal(const char *nptr, char **endptr, int base, int group);
    unsigned long int __strtoul_internal(const char *nptr, char **endptr, int base, int group);
}

namespace LibcBridge
{
    char bridgeLibcSingleThreaded = 1; // 1 = true (skip pthread locks), 0 = false

    FILE *native_stdin = stdin;
    FILE *native_stdout = stdout;
    FILE *native_stderr = stderr;

    void InitBridges()
    {
        log_info("Initializing Libc Bridges...");

        // printf family
        MAP("printf", bridgePrintf);
        MAP("puts", bridgePuts);
        MAP("fprintf", bridgeFprintf);
        MAP("sprintf", bridgeSprintf);
        MAP("snprintf", bridgeSnprintf);
        MAP("vprintf", bridgeVprintf);
        MAP("vfprintf", bridgeVfprintf);
        MAP("vsprintf", bridgeVsprintf);
        MAP("vsnprintf", bridgeVsnprintf);
        MAP("fscanf", bridgeFscanf);
        MAP("sscanf", bridgeSscanf);

        MAP("stdin", &native_stdin);
        MAP("stdout", &native_stdout);
        MAP("stderr", &native_stderr);

        // some static libraries refer to glibc's internal IO structs
        MAP("_IO_2_1_stdin_", stdin);
        MAP("_IO_2_1_stdout_", stdout);
        MAP("_IO_2_1_stderr_", stderr);

        // time functions
        MAP("time", bridgeTime);
        MAP("gettimeofday", bridgeGettimeofday);
        MAP("localtime", bridgeLocaltime);
        MAP("utime", bridgeUtime);
        MAP("usleep", bridgeUsleep);
        MAP("sleep", bridgeSleep);
        MAP("nanosleep", bridgeNanosleep);
        MAP("localtime_r", sharedLocaltime_R);
        MAP("clock_gettime", bridgeClockGettime);
        MAP("mktime", bridgeMktime);
        MAP("strftime", bridgeStrftime);

        // abort/exit
        MAP("abort", bridgeAbort);
        MAP("exit", bridgeExit);

        // C++ ABI functions
        MAP("__cxa_atexit", bridgeCxaAtexit);
        MAP("__cxa_thread_atexit_impl", bridgeCxaThreadAtexitImpl);
        MAP("__register_frame_info_bases", bridgeRegisterFrameInfoBases);
        MAP("__register_frame_info", bridgeRegisterFrameInfo);

        MAP("__libc_single_threaded", &bridgeLibcSingleThreaded);

        // locale functions
        MAP("setlocale", bridgeSetlocale);
        MAP("newlocale", bridgeNewlocale);
        MAP("__newlocale", bridgeNewlocale);
        MAP("freelocale", bridgeFreelocale);
        MAP("__freelocale", bridgeFreelocale);
        MAP("uselocale", bridgeUselocale);
        MAP("__uselocale", bridgeUselocale);

        // wide character functions
        MAP("__wctype_l", bridgeWctypeL);
        MAP("__iswctype_l", bridgeIswctypeL);
        MAP("mbsrtowcs", bridgeMbsrtowcs);

        // gettext
        MAP("gettext", bridgeGettext);
        MAP("dgettext", bridgeDgettext);

        // Stubs
        MAP("sysctl", bridgeSysctl);
        MAP("iopl", bridgeStubSuccess);

        // From other loader
        MAP("__divdi3", __divdi3);
        MAP("__udivdi3", __udivdi3);
        MAP("__umoddi3", __umoddi3);
        MAP("__moddi3", __moddi3);
        MAP("__fixunsdfdi", __fixunsdfdi);
        MAP("__fixunssfdi", __fixunssfdi);

        // system
        MAP("system", bridgeSystem);

        MAP("getpid", _getpid);
        MAP("fork", bridgeFork);
        MAP("vfork", bridgeVfork);
        MAP("daemon", bridgeDaemon);
        MAP("execlp", bridgeExeclp);
        MAP("kill", bridgeKill);
        MAP("wait", bridgeWait);
        MAP("getenv", sharedGetenv);
        MAP("setenv", sharedSetenv);
        MAP("unsetenv", sharedUnsetenv);

        MAP("rand", rand);
        MAP("signal", bridgeSignal);

        // Math
        MAP("atoi", atoi);
        MAP("atof", atof);

        // Memory
        MAP("getpagesize", bridgeGetpagesize);
        MAP("mmap", bridgeMmap);
        MAP("munmap", bridgeMunmap);

        // Library handles
        MAP("dlopen", sharedDlopen);
        MAP("dlsym", sharedDlsym);
        MAP("dlclose", sharedDlclose);
        MAP("dlerror", sharedDlerror);


        MAP("kswap_collect", sharedKswap_collect);
    }

    void bridgeAbort()
    {
        log_fatal("abort() called by ELF!");
        ::abort();
    }

    void bridgeExit(int status)
    {
        log_fatal("exit(%d) called by ELF!", status);
        ::exit(status);
    }

    int bridgeCxaAtexit(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_atexit called");
        return 0;
    }

    int bridgeCxaThreadAtexitImpl(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_thread_atexit_impl called");
        return 0;
    }

    void bridgeCxaVecCtor(void *vec_this, size_t size)
    {
        log_debug("__cxa_vec_ctor called");
    }

    void bridgeCxaVecDtor(void *vec_this, size_t size, void (*dest)(void *), void (*tr1)(void *))
    {
        log_debug("__cxa_vec_dtor called");
    }

    void bridgeCxaVecNew(void *vec_this, size_t size)
    {
        log_debug("__cxa_vec_new called");
    }

    void bridgeRegisterFrameInfoBases(void *begin, void *ob, void *tbase, void *dbase)
    {
    }

    void bridgeRegisterFrameInfo(void *begin, void *ob)
    {
    }

    void bridgeCxaThrow(void *thrown_exception, void *tinfo, void (*dest)(void *))
    {
        void *addr = __builtin_return_address(0);
        log_fatal("__cxa_throw intercepted! Exception thrown. Throw value ptr: %p, "
                  "type_info ptr: %p. Aborting process to avoid unwinder crash. Addr: 0x%p",
                  thrown_exception, tinfo, addr);
        bridgeAbort();
    }

    char *bridgeSetlocale(int category, const char *locale)
    {
        return setlocale(category, locale);
    }

    struct FakeLocaleStruct
    {
        void *localeData[13]; // __locale_data* per LC_* category (unused, keep NULL)
        const unsigned short *ctypeB;      // == __ctype_b
        const int32_t *ctypeTolower;       // == __ctype_tolower
        const int32_t *ctypeToUpper;       // == __ctype_toupper
        const char *names[13];             // locale category name strings (unused)
    };

    static FakeLocaleStruct g_FakeClassicLocale = {};
    static bool g_FakeLocaleInitialized = false;

    static FakeLocaleStruct *GetFakeLocale()
    {
        if (!g_FakeLocaleInitialized)
        {
            memset(&g_FakeClassicLocale, 0, sizeof(g_FakeClassicLocale));
            g_FakeClassicLocale.ctypeB       = GccBridge::GetCtypeBPtr();
            g_FakeClassicLocale.ctypeTolower = GccBridge::GetCtypeTolowerPtr();
            g_FakeClassicLocale.ctypeToUpper = GccBridge::GetCtypeToUpperPtr();
            g_FakeLocaleInitialized = true;
        }
        return &g_FakeClassicLocale;
    }

    void *bridgeNewlocale(int category_mask, const char *locale, void *base)
    {
        log_debug("bridgeNewlocale: category_mask=%d, locale=%s", category_mask,
                  locale ? locale : "(null)");
        return GetFakeLocale();
    }

    void bridgeFreelocale(void *loc)
    {
    }

    void *bridgeUselocale(void *loc)
    {
        return GetFakeLocale();
    }

    uint32_t bridgeWctypeL(const char *property, void *locale)
    {
        return (uint32_t)wctype(property);
    }

    int bridgeIswctypeL(int wc, uint32_t desc, void *locale)
    {
        return iswctype(wc, (wctype_t)desc);
    }

    size_t bridgeMbsrtowcs(uint32_t *dst, const char **src, size_t len, void *ps)
    {
        if (!src || !*src)
            return (size_t)-1;

        const char *s = *src;
        size_t count = 0;
        mbstate_t localState;
        memset(&localState, 0, sizeof(localState));

        if (dst == nullptr)
        {
            while (*s)
            {
                wchar_t wc;
                size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
                if (nb == (size_t)-1 || nb == (size_t)-2)
                    return (size_t)-1;
                if (nb == 0)
                    break;
                s += nb;
                count++;
            }
            return count;
        }

        while (count < len)
        {
            if (*s == '\0')
            {
                dst[count] = 0;
                *src = nullptr;
                return count;
            }

            wchar_t wc;
            size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
            if (nb == (size_t)-1 || nb == (size_t)-2)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }

            dst[count] = (uint32_t)wc;
            s += (nb == 0) ? 1 : nb;
            count++;
        }

        *src = s;
        return count;
    }

    int bridgeIsinf(double x)
    {
        return __builtin_isinf(x);
    }

    int bridgeIsnan(double x)
    {
        return __builtin_isnan(x);
    }

    int bridgeWcscoll_l(const uint32_t *s1, const uint32_t *s2, void *locale)
    {
        size_t len1 = 0, len2 = 0;
        while (s1[len1]) len1++;
        while (s2[len2]) len2++;

        wchar_t *w1 = (wchar_t *)alloca((len1 + 1) * sizeof(wchar_t));
        wchar_t *w2 = (wchar_t *)alloca((len2 + 1) * sizeof(wchar_t));

        for (size_t i = 0; i <= len1; i++)
            w1[i] = (wchar_t)s1[i];
        for (size_t i = 0; i <= len2; i++)
            w2[i] = (wchar_t)s2[i];

        return wcscoll(w1, w2);
    }

    size_t bridgeWcsxfrm_l(uint32_t *dst, const uint32_t *src, size_t n, void *locale)
    {
        size_t srcLen = 0;
        while (src[srcLen]) srcLen++;

        wchar_t *wSrc = (wchar_t *)alloca((srcLen + 1) * sizeof(wchar_t));
        for (size_t i = 0; i <= srcLen; i++)
            wSrc[i] = (wchar_t)src[i];

        if (dst == nullptr || n == 0)
            return wcsxfrm(nullptr, wSrc, 0);

        wchar_t *wDst = (wchar_t *)alloca((n + 1) * sizeof(wchar_t));
        size_t result = wcsxfrm(wDst, wSrc, n);

        size_t copyLen = (result < n) ? result : n - 1;
        for (size_t i = 0; i < copyLen; i++)
            dst[i] = (uint32_t)wDst[i];
        if (n > 0)
            dst[copyLen] = 0;

        return result;
    }

    int bridgeTowlower_l(int wc, void *locale)
    {
        return (int)towlower((wint_t)wc);
    }

    int bridgeTowupper_l(int wc, void *locale)
    {
        return (int)towupper((wint_t)wc);
    }

    #define LINUX_CODESET 14

    char *bridgeNlLanginfo(int item)
    {
        static char codeset[] = "ANSI_X3.4-1968";
        static char empty[] = "";

        if (item == LINUX_CODESET)
            return codeset;
        return empty;
    }

    char *bridgeGettext(const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    char *bridgeDgettext(const char *domainname, const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    int bridgePrintf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);

        log_trace("Intercepted printf");
        int ret = ::vprintf(format, args);

        va_end(args);
        return ret;
    }

    int bridgePuts(const char *str)
    {
        log_trace("Intercepted puts");
        return ::puts(str);
    }

    int bridgeFprintf(void *stream, const char *format, ...)
    {
        va_list args;
        va_start(args, format);

        log_trace("Intercepted fprintf");
        int ret = ::vfprintf((FILE *)stream, format, args);

        va_end(args);
        return ret;
    }

    int bridgeSprintf(char *buffer, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        log_trace("Intercepted sprintf");
        int ret = ::vsprintf(buffer, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSnprintf(char *buffer, size_t count, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        log_trace("Intercepted snprintf");
        if (format == NULL)
            return 0;
        int ret = _vsnprintf(buffer, count, format, args);
        va_end(args);
        return ret;
    }

    int bridgeVprintf(const char *format, va_list args)
    {
        log_trace("Intercepted vprintf");
        return ::vprintf(format, args);
    }

    int bridgeVfprintf(void *stream, const char *format, va_list args)
    {
        log_trace("Intercepted vfprintf");
        if (stream == nullptr)
        {
            stream = stdout;
        }
        return ::vfprintf((FILE *)stream, format, args);
    }

    int bridgeVsprintf(char *buffer, const char *format, va_list args)
    {
        log_trace("Intercepted vsprintf");
        return ::vsprintf(buffer, format, args);
    }

    int bridgeVsnprintf(char *buffer, size_t count, const char *format, va_list args)
    {
        log_trace("Intercepted vsnprintf");
        return ::vsnprintf(buffer, count, format, args);
    }

    int bridgeFscanf(FILE *stream, const char *format, ...)
    {
        if (!stream)
            return 0;

        va_list args;
        va_start(args, format);
        int ret = 0;
        ret = vfscanf(stream, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSscanf(const char *str, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        int ret = vsscanf(str, format, args);
        va_end(args);
        return ret;
    }

    int32_t bridgeTime(int32_t *tloc)
    {
        log_trace("Intercepted time");
        time_t t = time(NULL);
        if (tloc)
        {
            *tloc = (int32_t)t;
        }
        return (int32_t)t;
    }

    int bridgeUtime(const char *filename, const struct linux_utimbuf *times)
    {
        log_trace("Intercepted utime");
        char winPath[MAX_PATH];
        ConvertPath(winPath, filename, MAX_PATH);
        return _utime(winPath, (struct _utimbuf *)times);
    }

    int bridgeGettimeofday(struct timeval *tv, void *tz)
    {
        log_trace("Intercepted gettimeofday");
        if (tv)
        {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            unsigned __int64 t = (unsigned __int64)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
            t -= 116444736000000000ULL;
            t /= 10;
            tv->tv_sec = (int32_t)(t / 1000000);
            tv->tv_usec = (int32_t)(t % 1000000);
        }
        return 0;
    }

    int bridgeUsleep(uint32_t microseconds)
    {
        log_trace("Intercepted usleep");
        Sleep(microseconds / 1000);
        return 0;
    }

    int bridgeSleep(uint32_t seconds)
    {
        log_trace("Intercepted sleep");
        Sleep(seconds * 1000);
        return 0;
    }

    int bridgeNanosleep(const struct timespec *req, struct timespec *rem)
    {
        long long duration_ns = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
        if (duration_ns <= 0) return 0;

        long long freq;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
        long long wait_ticks = (duration_ns * freq) / 1000000000LL;

        long long start, current;
        QueryPerformanceCounter((LARGE_INTEGER*)&start);

        while (true)
        {
            QueryPerformanceCounter((LARGE_INTEGER*)&current);
            long long elapsed = current - start;
            if (elapsed >= wait_ticks)
                break;

            long long remaining_us = ((wait_ticks - elapsed) * 1000000LL) / freq;
            
            if (remaining_us > 2000) {
                Sleep(1); 
            }
            else if (remaining_us > 100) {
                Sleep(0);
            }
        }

        return 0;
    }

    tm32 *bridgeLocaltime(const int32_t *timer)
    {
        static tm32 t32;
        time_t t = (time_t)*timer;
        struct tm *tm_ptr = localtime(&t);

        if (tm_ptr)
        {
            t32.tm_sec = tm_ptr->tm_sec;
            t32.tm_min = tm_ptr->tm_min;
            t32.tm_hour = tm_ptr->tm_hour;
            t32.tm_mday = tm_ptr->tm_mday;
            t32.tm_mon = tm_ptr->tm_mon;
            t32.tm_year = tm_ptr->tm_year;
            t32.tm_wday = tm_ptr->tm_wday;
            t32.tm_yday = tm_ptr->tm_yday;
            t32.tm_isdst = tm_ptr->tm_isdst;
            t32.tm_gmtoff = 0;
            t32.tm_zone = 0;
            return &t32;
        }
        return nullptr;
    }

    int bridgeClockGettime(int clk_id, struct timespec *tp)
    {
        log_trace("Intercepted clock_gettime");
        if (clk_id == CLOCK_REALTIME)
        {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            unsigned __int64 t = (unsigned __int64)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
            t -= 116444736000000000ULL;
            t /= 10;
            tp->tv_sec = (long)(t / 1000000);
            tp->tv_nsec = (long)(t % 1000000) * 1000;
            return 0;
        }
        return -1;
    }

    time_t bridgeMktime(struct tm *tm)
    {
        log_trace("Intercepted mktime");
        return mktime(tm);
    }

    size_t bridgeStrftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr)
    {
        log_trace("Intercepted strftime");
        return strftime(s, maxsize, format, timeptr);
    }

    int bridgeSysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
    {
        log_trace("Intercepted sysctl");
        return -1;
    }

    int bridgeStubSuccess()
    {
        log_trace("Intercepted stub success");
        return 0;
    }

    int bridgeSystem(const char *command)
    {
        log_debug("system(\"%s\")", command);
        if (strcmp(command, "touch /var/tmp/mwlogo") == 0)
            command = "type nul > .\\tmp\\mwlogo";

        if (strcmp(command, "cd /tmp/segaboot > /dev/null") == 0)
            return -1;

        if (strcmp(command, "mkdir /tmp/segaboot > /dev/null") == 0)
            command = "md .\\tmp\\segaboot > nul";

        if (strcmp(command, "touch /tmp/segaboot/test") == 0)
            command = "type nul > .\\tmp\\segaboot\\test";

        if (strcmp(command, "touch /var/tmp/atr_init") == 0)
            command = "type nul > .\\tmp\\atr_init";

        if (strcmp(command, "touch /var/tmp/atr_err") == 0)
            command = "type nul > .\\tmp\\atr_err";

        if (strncmp(command, "ifconfig eth0", 11) == 0)
            return 0;

        return system(command);
    }

    int bridgeFork(void)
    {
        log_debug("fork() called - returning fake parent PID 1000");
        return 1000;
    }

    int bridgeVfork(void)
    {
        log_debug("vfork() called - returning fake parent PID 1000");
        return bridgeFork();
    }

    int bridgeDaemon(int nochdir, int noclose)
    {
        log_debug("daemon(%d, %d) called - stubbed success", nochdir, noclose);
        return 0;
    }

    int bridgeExeclp(const char *file, const char *arg, ...)
    {
        log_info("execlp(\"%s\", \"%s\", ...)", file, arg);
        return 0;
    }

    void (*bridgeSignal(int signum, void (*handler)(int)))(int)
    {
        log_debug("signal() called");

        // Map Linux signals to Windows signals where possible
        int win_sig = -1;
        switch (signum)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_warn("signal: unsupported signal %d", signum);
                return (void (*)(int))-1; // SIG_ERR
        }

        return signal(win_sig, handler);
    }

    int bridgeKill(int pid, int sig)
    {
        int my_pid = _getpid();
        log_info("kill(%d, %d) called", pid, sig);

        if (pid == my_pid || pid == 0)
        {
            // Signal 0 is existence check
            if (sig == 0)
                return 0;

            // SIGKILL (9) or SIGTERM (15) -> Terminate self
            if (sig == 9 || sig == 15)
            {
                log_warn("kill: Process requested self-termination");
                exit(0);
            }
        }

        return 0;
    }

    int bridgeWait(int *wstatus)
    {
        log_warn("wait() called: No child processes to wait for. Returning ECHILD.");

        Sleep(10);

        if (wstatus)
            *wstatus = 0;
        errno = ECHILD;
        return -1;
    }

    int bridgeGetpagesize()
    {
        log_trace("Intercepted getpagesize");
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return sysInfo.dwPageSize;
    }

#define L_MAP_FAILED ((void *)-1)
#define L_MAP_ANONYMOUS 0x20
#define L_PROT_EXEC 0x4

    void *bridgeMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    {
        log_trace("Intercepted mmap: addr=%p, len=%zu, prot=%d, flags=%d, fd=%d, offset=%ld", addr, length, prot, flags, fd, (long)offset);

        if (flags & L_MAP_ANONYMOUS)
        {
            DWORD winProt = PAGE_READWRITE;
            if (prot & L_PROT_EXEC)
                winProt = PAGE_EXECUTE_READWRITE;

            void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, winProt);
            if (!ret)
                return L_MAP_FAILED;
            return ret;
        }

        // Basic fallback for file mapping
        void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (ret && fd >= 0)
        {
            log_warn("mmap: Emulating file mapping by reading into VirtualAlloc memory (fd=%d)", fd);
            _lseek(fd, offset, SEEK_SET);
            _read(fd, ret, length);
        }
        return ret ? ret : L_MAP_FAILED;
    }

    int bridgeMunmap(void *addr, size_t length)
    {
        log_trace("Intercepted munmap: addr=%p, len=%zu", addr, length);
        if (VirtualFree(addr, 0, MEM_RELEASE))
        {
            return 0;
        }
        return -1;
    }


} // namespace LibcBridge

#endif