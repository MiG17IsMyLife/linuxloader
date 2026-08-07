#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libcShared.h"
#include "../config/config.h"
#include "loader/elfLoader/symbolResolver.hpp"

#ifdef __linux__
#include <dlfcn.h>
#define REAL_FUNC(name) dlsym(RTLD_NEXT, #name)
#else
#include <processenv.h>
#define REAL_FUNC(name) name
#endif

extern uint32_t gId;
extern int gGrp;
extern char envpath[100];

/**
 * Hook for the only function provided by kswapapi.so
 * @param p No idea this gets discarded
 */
int sharedKswap_collect(void *p)
{
    return 0;
}

int sharedSystem(const char *command)
{
    int (*_system)(const char *command) = REAL_FUNC(system);

    if (strcmp(command, "lsmod | grep basebd > /dev/null") == 0)
        return 0;

    if (strcmp(command, "cd /tmp/segaboot > /dev/null") == 0)
        return system("cd tmp/segaboot > /dev/null");

    if (strcmp(command, "mkdir /tmp/segaboot > /dev/null") == 0)
        return system("mkdir tmp/segaboot > /dev/null");

    if (strcmp(command, "lspci | grep \"Multimedia audio controller: %Creative\" > /dev/null") == 0)
        return 0;

    if (strcmp(command, "lsmod | grep ctaud") == 0)
        return 0;

    if (strcmp(command, "lspci | grep MPC8272 > /dev/null") == 0)
        return 0;

    if (strcmp(command, "uname -r | grep mvl") == 0)
        return 0;

    if (strstr(command, "hwclock") != NULL)
        return 0;

    if (strstr(command, "losetup") != NULL)
        return 0;

    if (strstr(command, "check_ip.sh") != NULL)
        return 0;

    if (strcmp(command, "ifconfig eth0 10.0.0.1 netmask 255.255.255.0") == 0)
        return 0;

    if (strcmp(command, "ifconfig eth0 10.0.0.2 netmask 255.255.255.0") == 0)
        return 0;

    if (strcmp(command, "touch /tmp/segaboot/test") == 0)
        command = "touch tmp/segaboot/test";

    return _system(command);
}

char *sharedStrncpy(char *dest, const char *src, size_t n)
{
    char *(*_strncpy)(char *dest, const char *src, size_t n) = REAL_FUNC(strncpy);

    switch (gId)
    {
        case HARLEY_DAVIDSON_SBRG:
        case RAMBO_SBQL:
        case RAMBO_SBSS_CHINA:
        case THE_HOUSE_OF_THE_DEAD_EX_SBRC:
        case TOO_SPICY_SBMV:
            if (getConfig()->GPUVendor != NVIDIA_GPU &&
                (strstr(src, "../fs/compiledshader") != NULL || strstr(src, "../fs/compiled") != NULL))
                return _strncpy(dest, "../fs/compiledmesa", n);
            break;
        case QUIZ_AXA_SBMS:
        case QUIZ_AXA_SBUR_LIVE:
            if (getConfig()->GPUVendor != NVIDIA_GPU && strstr(src, "../../data/compiledshader") != NULL)
                return _strncpy(dest, "../../data/compiledmesa", n);
            break;
    }
    return _strncpy(dest, src, n);
}

int sharedSetenv(const char *name, const char *value, int overwrite)
{
#ifdef __linux__
    int (*_setenv)(const char *name, const char *value, int overwrite) = dlsym(RTLD_NEXT, "setenv");
#endif

    if (strcmp(name, "DISPLAY") == 0)
    {
        return 0;
    }

#ifdef __linux__
    return _setenv(name, value, overwrite);
#else
    return _putenv_s(name, value);
#endif
}

char *sharedGetenv(const char *name)
{
    char *(*_getenv)(const char *name) = REAL_FUNC(getenv);

    if (strcmp(name, "TEA_DIR") == 0)
    {
#ifdef __linux__
        if (getcwd(envpath, 100) == NULL)
            return "";
#else
        if (GetCurrentDirectoryA(MAX_PATH, (LPSTR)envpath) == 0)
            return "";
#endif
        if (gGrp == GROUP_VT3 || gGrp == GROUP_VT3_TEST || gId == RAMBO_SBQL || gId == RAMBO_SBSS_CHINA || gId == TOO_SPICY_SBMV)
        {
#ifdef __linux__
            char *ptr = strrchr(envpath, '/');
            if (ptr == NULL)
                return "";
            *ptr = '\0';
            return envpath;
#else
            char *ptr = strrchr(envpath, '\\');
            if (ptr == NULL)
                return "";
            *ptr = '\0';
            return envpath;
#endif
        }
        else
        {
            return envpath;
        }
    }

    if (strcmp(name, "__GL_SYNC_TO_VBLANK") == 0)
    {
        return "";
    }
    return _getenv(name);
}

int sharedUnsetenv(const char *name)
{
#ifdef __linux__
    int (*_unsetenv)(const char *name) = dlsym(RTLD_NEXT, "unsetenv");
#endif

    if (strcmp(name, "DISPLAY") == 0)
    {
        return 0;
    }

#ifdef __linux__
    return _unsetenv(name);
#else
    return _putenv_s(name, "");
#endif
}

char *shared__strdup(const char *string)
{
#ifdef __linux__
    char *(*___strdup)(const char *string) = dlsym(RTLD_NEXT, "__strdup");
    if (strcmp(string, "plughw:0, 0") == 0)
    {
        return ___strdup("default");
    }
    return ___strdup(string);
#else
    if (!string)
        return NULL;
    size_t len = strlen(string) + 1;
    void *ptr = _aligned_malloc(len, 16);
    if (ptr)
        memcpy(ptr, string, len);
    return (char *)ptr;
#endif
}

struct tm *sharedLocaltime_R(const time_t *timep, struct tm *result)
{
#ifdef __linux__
    struct tm *(*_gmtime_r)(const time_t *, struct tm *) = (struct tm * (*)(const time_t *, struct tm *)) dlsym(RTLD_NEXT, "gmtime_r");

    if ((gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA) && getConfig()->mj4EnabledAtT == 1)
    {
        time_t target_time = 1735286445;
        struct tm *res = _gmtime_r(&target_time, result);
        return res;
    }
    return _gmtime_r(timep, result);
#else
    const int32_t *t32 = (const int32_t *)timep;
    time_t t = (time_t)*t32;

    /*
     * System N2 cabinets run with TZ=UTC and a system clock that already holds
     * local wall time, so localtime() must not shift it a second time.
     */
    if (getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2)
        return gmtime_s(result, &t) == 0 ? result : NULL;

    if (localtime_s(result, &t) == 0)
        return result;
    return NULL;
#endif
}

#ifdef __linux__
struct tm *sharedGmtime_r(const time_t *timep, struct tm *result)
{

    struct tm *(*_gmtime_r)(const time_t *, struct tm *) = (struct tm * (*)(const time_t *, struct tm *)) dlsym(RTLD_NEXT, "gmtime_r");

    if ((gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE) && getConfig()->mj4EnabledAtT == 1)
    {
        time_t target_time = 1735286445;
        struct tm *res = _gmtime_r(&target_time, result);
        return res;
    }
    return _gmtime_r(timep, result);
}
#endif

float sharedPowf(float base, float exponent)
{
    return (float)pow((double)base, (double)exponent);
}

int sharedIopl(int level)
{
    return 0;
}

void *sharedDlopen(const char *filename, int flags)
{
#ifdef _WIN32
    void *handle = NULL;

    if (!filename)
        return (void *)-1; // RTLD_DEFAULT handle

    if (getConfig()->platform == ARCADE_PLATFORM_NAMCO_ES1 &&
        es1AudioDlopen(filename, &handle))
        return handle;

    bridgeLoadNeededLibrary(filename);

    handle = bridgeLibraryHandle(filename);
    return handle ? handle : (void *)0xDEADBEEF;
#else
    return dlopen(filename, flags);
#endif
}

void *sharedDlsym(void *handle, const char *symbol)
{
#ifdef _WIN32
    void *es1Function = es1AudioDlsym(handle, symbol);
    if (es1Function)
        return es1Function;

    void *func = bridgeResolveSymbolInModule(handle, symbol);
    if (func)
        return func;

    return bridgeResolveSymbolOptional(symbol);
#else
    return dlsym(handle, symbol);
#endif
}

int sharedDlclose(void *handle)
{
#ifdef _WIN32
    if (es1AudioDlclose(handle) == 0)
        return 0;
    return 0;
#else
    return dlclose(handle);
#endif
}

char *sharedDlerror()
{
#ifdef _WIN32
    return NULL;
#else
    return dlerror();
#endif
}

#ifdef __linux__
int kswap_collect(void *p)
{
    return sharedKswap_collect(p);
}

int system(const char *command)
{
    return sharedSystem(command);
}

char *strncpy(char *dest, const char *src, size_t n)
{
    return sharedStrncpy(dest, src, n);
}

int setenv(const char *name, const char *value, int overwrite)
{
    return sharedSetenv(name, value, overwrite);
}

char *getenv(const char *name)
{
    return sharedGetenv(name);
}

int unsetenv(const char *name)
{
    return sharedUnsetenv(name);
}

char *__strdup(const char *string)
{
    return shared__strdup(string);
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return sharedLocaltime_R(timep, result);
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    return sharedGmtime_r(timep, result);
}

float powf(float base, float exponent)
{
    return sharedPowf(base, exponent);
}

int iopl(int level)
{
    return sharedIopl(level);
}
#endif
