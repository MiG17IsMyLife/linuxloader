#pragma once

#ifdef __cplusplus
extern "C" {
#endif



#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <_timeval.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>
#include <sys/types.h>
#include <windows.h>

struct tm32
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int32_t tm_gmtoff;
    uint32_t tm_zone;
};

struct linux_timespec
{
    int32_t tv_sec;
    int32_t tv_nsec;
};

namespace LibcBridge
{
    void InitBridges();

    // STDIO interceptions
    int bridgePrintf(const char *format, ...);
    int bridgePuts(const char *str);
    int bridgeFprintf(void *stream, const char *format, ...);
    int bridgeSprintf(char *buffer, const char *format, ...);
    int bridgeSnprintf(char *buffer, size_t count, const char *format, ...);
    int bridgeVprintf(const char *format, va_list args);
    int bridgeVfprintf(void *stream, const char *format, va_list args);
    int bridgeVsprintf(char *buffer, const char *format, va_list args);
    int bridgeVsnprintf(char *buffer, size_t count, const char *format, va_list args);
    int bridgeFscanf(FILE *stream, const char *format, ...);
    int bridgeSscanf(const char *str, const char *format, ...);

    // Time interceptions
    int32_t bridgeTime(int32_t *tloc);
    int bridgeGettimeofday(struct timeval *tv, void *tz);
    tm32 *bridgeLocaltime(const int32_t *timer);
    int bridgeUtime(const char *filename, const struct linux_utimbuf *times);
    int bridgeUsleep(uint32_t microseconds);
    int bridgeSleep(uint32_t seconds);
    int bridgeNanosleep(const struct timespec *req, struct timespec *rem);
    int bridgeClockGettime(int clk_id, struct timespec *tp);
    time_t bridgeMktime(struct tm *tm);
    size_t bridgeStrftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);

    void bridgeAbort();
    void bridgeExit(int status);
    int bridgeGetpagesize();
    void *bridgeMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    int bridgeMunmap(void *addr, size_t length);

    int bridgeCxaAtexit(void (*func)(void *), void *arg, void *dso_handle);
    int bridgeCxaThreadAtexitImpl(void (*func)(void *), void *arg, void *dso_handle);
    void bridgeRegisterFrameInfoBases(void *begin, void *ob, void *tbase, void *dbase);
    void bridgeRegisterFrameInfo(void *begin, void *ob);
    void *bridgeDeregisterFrameInfo(void *begin);
    void bridgeCxaThrow(void *thrown_exception, void *tinfo, void (*dest)(void *));
    void *bridgeCxaAllocateException(size_t thrown_size);
    void bridgeCxaFreeException(void *thrown_exception);

    char *bridgeSetlocale(int category, const char *locale);
    void *bridgeNewlocale(int category_mask, const char *locale, void *base);
    void bridgeFreelocale(void *loc);
    void *bridgeUselocale(void *loc);

    uint32_t bridgeWctypeL(const char *property, void *locale);
    int bridgeIswctypeL(int wc, uint32_t desc, void *locale);
    size_t bridgeMbsrtowcs(uint32_t *dst, const char **src, size_t len, void *ps);

    char *bridgeGettext(const char *msgid);
    char *bridgeDgettext(const char *domainname, const char *msgid);

    void *bridgeVectorData(void *vec_this);
    void *bridgeOstreamString(void *ostream_this, const char *str);

    int bridgeSysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
    int bridgeStubSuccess();

    int bridgeSystem(const char *command);

    int bridgeFork(void);
    int bridgeVfork(void);
    int bridgeDaemon(int nochdir, int noclose);
    int bridgeExeclp(const char *file, const char *arg, ...);
    int bridgeKill(int pid, int sig);
    int bridgeWait(int *wstatus);

    void (*bridgeSignal(int signum, void (*handler)(int)))(int);

} // namespace LibcBridge

#endif
