#include "es1AudioCompat.h"

#include "../../../log/log.h"
#include "../../../../SDL3/SDL3/SDL.h"

#include <cstdint>
#include <cstring>

namespace
{
constexpr uintptr_t Es1AudioHandleValue = 0x45534101u;
void *const Es1AudioHandle = reinterpret_cast<void *>(Es1AudioHandleValue);

SDL_AudioStream *g_stream = nullptr;
bool g_audioInitialized = false;

bool isNsAdrv(const char *filename)
{
    if (!filename)
        return false;

    const char *slash = std::strrchr(filename, '/');
    const char *backslash = std::strrchr(filename, '\\');
    const char *basename = filename;
    if (slash && slash + 1 > basename)
        basename = slash + 1;
    if (backslash && backslash + 1 > basename)
        basename = backslash + 1;
    return std::strcmp(basename, "nsAdrv.dll") == 0;
}

bool initializeAudio(int channels, int rate)
{
    if (g_audioInitialized)
        return g_stream != nullptr;

    g_audioInitialized = true;

    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        log_warn("System ES1 audio: SDL audio initialization failed: %s", SDL_GetError());
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S32;
    spec.channels = channels > 0 ? channels : 2;
    spec.freq = rate > 0 ? rate : 48000;

    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_stream)
    {
        log_warn("System ES1 audio: SDL audio stream creation failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(g_stream))
        log_warn("System ES1 audio: unable to resume SDL audio stream: %s", SDL_GetError());

    log_info("System ES1 audio: nsAdrv bridged to SDL (%d channels, %d Hz)", spec.channels, spec.freq);
    return true;
}

extern "C" int es1NsAdrvInit(const char *, const char *, int channels, int, int rate, int, void *)
{
    /* Match line-mt4's proven nsAdrv ABI: initialization is successful even
     * when the host has no playback device, so the game can continue with
     * silent audio rather than blocking cabinet startup. */
    initializeAudio(channels, rate);
    return 0;
}

extern "C" int es1NsAdrvTerm(const char *)
{
    if (g_stream)
    {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    }
    g_audioInitialized = false;
    return 0;
}

extern "C" int es1NsAdrvWait(int)
{
    if (!g_stream)
        return 256;

    /* nsAdrv reports the number of frames the game should wait for. Keep the
     * same low-water behavior as line-mt4's SDL stream bridge. */
    return SDL_GetAudioStreamAvailable(g_stream) >= 6144 ? 0 : 256;
}

extern "C" int es1NsAdrvStart()
{
    if (g_stream)
        SDL_ResumeAudioStreamDevice(g_stream);
    return 0;
}

extern "C" void es1NsAdrvMixup(void *)
{
}

extern "C" int es1NsAdrvMixsts(int)
{
    return 0;
}

extern "C" void es1NsAdrvWrite(uint8_t *data, int size)
{
    if (!g_stream || !data)
        return;

    /*
     * nsAdrv_write's second argument is not a reliable byte count on the
     * ES1 ELF.  The cabinet driver feeds one 6144-byte PCM block per call;
     * line-mt4 follows the same ABI and deliberately ignores this argument.
     * Passing the guest value through can enqueue only a partial block (or
     * no samples at all), which leaves the SDL stream silent.
     */
    constexpr int Es1PcmBlockBytes = 6144;
    (void)size;
    if (!SDL_PutAudioStreamData(g_stream, data, Es1PcmBlockBytes))
        log_warn("System ES1 audio: nsAdrv_write failed: %s", SDL_GetError());
}
}

extern "C" int es1AudioDlopen(const char *filename, void **handle)
{
    if (filename && std::strstr(filename, "nsAdrv"))
        log_debug("System ES1 audio: dlopen request for %s", filename);
    if (!isNsAdrv(filename))
        return 0;

    if (handle)
        *handle = Es1AudioHandle;
    return 1;
}

extern "C" void *es1AudioDlsym(void *handle, const char *symbol)
{
    if (handle != Es1AudioHandle || !symbol)
        return nullptr;

    if (std::strcmp(symbol, "nsAdrv_init") == 0)
        return reinterpret_cast<void *>(es1NsAdrvInit);
    if (std::strcmp(symbol, "nsAdrv_term") == 0)
        return reinterpret_cast<void *>(es1NsAdrvTerm);
    if (std::strcmp(symbol, "nsAdrv_wait") == 0)
        return reinterpret_cast<void *>(es1NsAdrvWait);
    if (std::strcmp(symbol, "nsAdrv_start") == 0)
        return reinterpret_cast<void *>(es1NsAdrvStart);
    if (std::strcmp(symbol, "nsAdrv_mixup") == 0)
        return reinterpret_cast<void *>(es1NsAdrvMixup);
    if (std::strcmp(symbol, "nsAdrv_mixsts") == 0)
        return reinterpret_cast<void *>(es1NsAdrvMixsts);
    if (std::strcmp(symbol, "nsAdrv_write") == 0)
        return reinterpret_cast<void *>(es1NsAdrvWrite);

    return nullptr;
}

extern "C" int es1AudioDlclose(void *handle)
{
    return handle == Es1AudioHandle ? 0 : -1;
}
