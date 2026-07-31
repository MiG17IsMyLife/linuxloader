#include "n2Audio.h"
#include "n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstddef>
#include <windows.h>

#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../log/log.h"

/*
 * WMMT3 statically links NVIDIA's nForce OpenAL implementation instead of
 * pulling in libopenal.so, so the library substitution the loader performs for
 * Lindbergh titles never applies.  That embedded implementation reaches the
 * hardware through ioctl(0xc0044d6f) on /dev/dsp, a node owned by the nvsound
 * kernel module, and mmaps the APU registers behind it.  Neither exists on
 * Windows, so every mixer call lands in a dead back end and the game stays
 * silent while still reporting success.
 *
 * The game itself only uses plain OpenAL 1.1 entry points (the NV extensions
 * are internal to the bundled implementation), so the whole embedded stack is
 * bypassed by redirecting those exported symbols to the OpenAL Soft build that
 * already ships in ll-deps.  The ELF is i386 cdecl and AL_APIENTRY is __cdecl
 * on Windows, so the two calling conventions match and the DLL exports can be
 * used as MinHook detours directly.
 */

namespace
{
    HMODULE openalModule = nullptr;

    typedef void *(__cdecl *AlcOpenDeviceFn)(const char *deviceName);
    typedef void *(__cdecl *AlcCreateContextFn)(void *device, const int *attributes);
    typedef int(__cdecl *AlcGetErrorFn)(void *device);

    AlcOpenDeviceFn realAlcOpenDevice = nullptr;
    AlcCreateContextFn realAlcCreateContext = nullptr;
    AlcGetErrorFn realAlcGetError = nullptr;

    // Entry points forwarded verbatim to openal32.dll.  alcOpenDevice and
    // alcCreateContext are handled by the wrappers below instead.
    const char *const forwardedSymbols[] = {
        "alEnable",
        "alDisable",
        "alIsEnabled",
        "alGetString",
        "alGetBooleanv",
        "alGetIntegerv",
        "alGetFloatv",
        "alGetDoublev",
        "alGetBoolean",
        "alGetInteger",
        "alGetFloat",
        "alGetDouble",
        "alGetError",
        "alIsExtensionPresent",
        "alGetProcAddress",
        "alGetEnumValue",
        "alListenerf",
        "alListener3f",
        "alListenerfv",
        "alListeneri",
        "alGetListenerf",
        "alGetListener3f",
        "alGetListenerfv",
        "alGetListeneri",
        "alGenSources",
        "alDeleteSources",
        "alIsSource",
        "alSourcef",
        "alSource3f",
        "alSourcefv",
        "alSourcei",
        "alGetSourcef",
        "alGetSource3f",
        "alGetSourcefv",
        "alGetSourcei",
        "alSourcePlayv",
        "alSourceStopv",
        "alSourceRewindv",
        "alSourcePausev",
        "alSourcePlay",
        "alSourceStop",
        "alSourceRewind",
        "alSourcePause",
        "alSourceQueueBuffers",
        "alSourceUnqueueBuffers",
        "alGenBuffers",
        "alDeleteBuffers",
        "alIsBuffer",
        "alBufferData",
        "alGetBufferf",
        "alGetBufferi",
        "alDopplerFactor",
        "alDopplerVelocity",
        "alDistanceModel",
        "alcCloseDevice",
        "alcMakeContextCurrent",
        "alcProcessContext",
        "alcSuspendContext",
        "alcDestroyContext",
        "alcGetCurrentContext",
        "alcGetContextsDevice",
        "alcGetError",
        "alcIsExtensionPresent",
        "alcGetProcAddress",
        "alcGetEnumValue",
        "alcGetString",
        "alcGetIntegerv"};

    void *__cdecl bridgeAlcOpenDevice(const char *deviceName)
    {
        /*
         * The nForce implementation understands NVIDIA specific device names
         * that OpenAL Soft has never heard of, so always ask for the default
         * device.  WMMT3 passes NULL anyway.
         */
        void *device = realAlcOpenDevice(nullptr);
        if (!device)
        {
            log_error("Namco N2 audio: alcOpenDevice failed; no output device is available");
            return nullptr;
        }

        if (deviceName && deviceName[0] != '\0')
            log_info("Namco N2 audio: opened default device instead of '%s'", deviceName);
        else
            log_info("Namco N2 audio: opened default OpenAL device");
        return device;
    }

    void *__cdecl bridgeAlcCreateContext(void *device, const int *attributes)
    {
        /*
         * The attribute list would carry nForce specific hints; OpenAL Soft
         * picks sane defaults on its own.  WMMT3 passes NULL here as well.
         */
        (void)attributes;

        void *context = realAlcCreateContext(device, nullptr);
        if (!context)
            log_error("Namco N2 audio: alcCreateContext failed (ALC error %d)",
                      realAlcGetError ? realAlcGetError(device) : 0);
        return context;
    }

    void *resolveExport(const char *name)
    {
        return reinterpret_cast<void *>(GetProcAddress(openalModule, name));
    }

    bool belongsToOpenal(void *address)
    {
        HMODULE owner = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(address), &owner))
            return false;
        return owner == openalModule;
    }

    bool forwardSymbol(const char *name, void *replacement)
    {
        void *target = n2ResolveSymbol(name);
        if (!target)
        {
            log_debug("Namco N2 audio: %s is not exported by the game", name);
            return false;
        }

        // Guard against hooking openal32.dll onto itself should the resolver
        // ever prefer the DLL export over the game's own copy.
        if (belongsToOpenal(target))
        {
            log_debug("Namco N2 audio: %s already resolves to openal32.dll", name);
            return false;
        }

        return n2HookSymbol(name, replacement) != 0;
    }

    HMODULE loadOpenal()
    {
        HMODULE module = GetModuleHandleA("openal32.dll");
        if (module)
            return module;

        SymbolResolver::GetInstance().RegisterLibrary("libopenal.so.0", "openal32.dll");
        SymbolResolver::GetInstance().LoadNeededLibrary("libopenal.so.0");

        module = GetModuleHandleA("openal32.dll");
        if (module)
            return module;

        return LoadLibraryA("openal32.dll");
    }
} // namespace

extern "C" int n2AudioInstallHooks(void)
{
    openalModule = loadOpenal();
    if (!openalModule)
    {
        log_error("Namco N2 audio: openal32.dll was not found in ll-deps; the game will stay silent");
        return 1;
    }

    realAlcOpenDevice = reinterpret_cast<AlcOpenDeviceFn>(resolveExport("alcOpenDevice"));
    realAlcCreateContext = reinterpret_cast<AlcCreateContextFn>(resolveExport("alcCreateContext"));
    realAlcGetError = reinterpret_cast<AlcGetErrorFn>(resolveExport("alcGetError"));

    if (!realAlcOpenDevice || !realAlcCreateContext)
    {
        log_error("Namco N2 audio: openal32.dll does not export the ALC context API; the game will stay silent");
        return 1;
    }

    size_t redirected = 0;
    if (forwardSymbol("alcOpenDevice", reinterpret_cast<void *>(bridgeAlcOpenDevice)))
        redirected++;
    if (forwardSymbol("alcCreateContext", reinterpret_cast<void *>(bridgeAlcCreateContext)))
        redirected++;

    for (const char *name : forwardedSymbols)
    {
        void *implementation = resolveExport(name);
        if (!implementation)
        {
            log_warn("Namco N2 audio: openal32.dll does not export %s", name);
            continue;
        }

        if (forwardSymbol(name, implementation))
            redirected++;
    }

    if (redirected == 0)
    {
        log_error("Namco N2 audio: no OpenAL entry point could be redirected; the game will stay silent");
        return 1;
    }

    log_info("Namco N2 audio: redirected %zu OpenAL entry points to openal32.dll", redirected);
    return 0;
}

#else

int n2AudioInstallHooks(void)
{
    return 0;
}

#endif
