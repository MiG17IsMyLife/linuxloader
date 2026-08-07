#include "es1.h"
#include "es1Network.h"
#include "es1TestModeCompat.h"

#include "../../../log/log.h"
#include "../../../graphics/sdlCalls.h"
#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../../minhook/include/MinHook.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstring>
#include <string>

namespace
{
bool g_detected = false;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

bool has(const std::string &contents, const char *needle)
{
    return contents.find(needle) != std::string::npos;
}

constexpr int Es1DongleSize = 0xD40;
std::array<unsigned char, Es1DongleSize> g_es1Dongle{};
uint32_t g_es1DongleHandle = 0x45533101;
bool g_es1DongleInitialized = false;

using Es1VolumeSetter = void (*)(int);
Es1VolumeSetter g_es1SetBgmVolumeOriginal = nullptr;
Es1VolumeSetter g_es1SetSeVolumeOriginal = nullptr;
Es1VolumeSetter g_es1SetEngineVolumeOriginal = nullptr;
Es1VolumeSetter g_es1SetVoiceVolumeOriginal = nullptr;
using Es1BoolVolumeSetter = void (*)(int, bool);
Es1BoolVolumeSetter g_es1SetMasterVolumeOriginal = nullptr;
Es1BoolVolumeSetter g_es1SetAttractVolumeOriginal = nullptr;

void es1SetBgmVolume(int volume)
{
    /* The ES1 cabinet exposes a 16-step volume table (0..15).  Keep the
     * game's own table access in range when the host audio device reports no
     * hardware mixer, as the physical cabinet does. */
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped BGM volume index %d to %d", volume, safeVolume);
    if (g_es1SetBgmVolumeOriginal)
        g_es1SetBgmVolumeOriginal(safeVolume);
}

void es1SetSeVolume(int volume)
{
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped SE volume index %d to %d", volume, safeVolume);
    if (g_es1SetSeVolumeOriginal)
        g_es1SetSeVolumeOriginal(safeVolume);
}

void es1SetEngineVolume(int volume)
{
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped engine volume index %d to %d", volume, safeVolume);
    if (g_es1SetEngineVolumeOriginal)
        g_es1SetEngineVolumeOriginal(safeVolume);
}

void es1SetVoiceVolume(int volume)
{
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped voice volume index %d to %d", volume, safeVolume);
    if (g_es1SetVoiceVolumeOriginal)
        g_es1SetVoiceVolumeOriginal(safeVolume);
}

void es1SetMasterVolume(int volume, bool enabled)
{
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped master volume index %d to %d", volume, safeVolume);
    if (g_es1SetMasterVolumeOriginal)
        g_es1SetMasterVolumeOriginal(safeVolume, enabled);
}

void es1SetAttractVolume(int volume, bool enabled)
{
    const int safeVolume = std::clamp(volume, 0, 15);
    if (safeVolume != volume)
        log_warn("System ES1 audio: clamped attract volume index %d to %d", volume, safeVolume);
    if (g_es1SetAttractVolumeOriginal)
        g_es1SetAttractVolumeOriginal(safeVolume, enabled);
}

void initializeEs1Dongle()
{
    if (g_es1DongleInitialized)
        return;

    g_es1Dongle.fill(0);
    static constexpr char serial[] = "880700000001";
    std::memcpy(g_es1Dongle.data() + 0xD00, serial, 12);
    unsigned char checksum = 0;
    for (int i = 0; i < 0x3E; ++i)
        checksum = static_cast<unsigned char>(checksum + g_es1Dongle[0xD00 + i]);
    g_es1Dongle[0xD3E] = checksum;
    g_es1Dongle[0xD3F] = static_cast<unsigned char>(checksum ^ 0xFF);
    g_es1DongleInitialized = true;
    log_info("System ES1: virtual dongle initialized (S/N %.6s-%.6s)", serial, serial + 6);
}

int es1HaspLogin(int, int, uint32_t *handle)
{
    initializeEs1Dongle();
    if (handle)
        *handle = g_es1DongleHandle;
    return 0;
}

int es1HaspGetSize(int, int, int *size)
{
    if (size)
        *size = Es1DongleSize;
    return 0;
}

int es1HaspRead(int, int, int offset, int length, unsigned char *buffer)
{
    initializeEs1Dongle();
    if (!buffer || offset < 0 || length < 0 || offset > Es1DongleSize ||
        length > Es1DongleSize - offset)
        return 1;
    std::memcpy(buffer, g_es1Dongle.data() + offset, static_cast<size_t>(length));
    return 0;
}

int es1HaspWrite(int, int, int offset, int length, const unsigned char *buffer)
{
    initializeEs1Dongle();
    if (!buffer || offset < 0 || length < 0 || offset > Es1DongleSize ||
        length > Es1DongleSize - offset)
        return 1;
    std::memcpy(g_es1Dongle.data() + offset, buffer, static_cast<size_t>(length));
    return 0;
}

int es1HaspSuccess()
{
    return 0;
}

int es1HaspSerial()
{
    return 0x45533101;
}

/*
 * Maximum Heat 3D uses a legacy clSystemN2 object for its cabinet-system
 * bootstrap even though the game is a System ES1 title.  The native helper
 * attempts to initialize the N2 JVIO service, which is not an ES1 board, and
 * leaves the object's error byte set.  clApplication::Update() then maps
 * that byte to error 0x0d ("Connection error", E30).
 *
 * This is deliberately an ES1 game hook, not an N2 backend change.  The
 * actual ES1 cabinet devices are provided by the ES1 virtual-device layer;
 * this getter only prevents the legacy N2-named status object from turning a
 * missing N2 JVIO device into the E30 screen.
 */
bool es1SystemIsError(void *)
{
    static bool logged = false;
    if (!logged)
    {
        log_info("System ES1: ignored legacy clSystemN2 bootstrap error");
        logged = true;
    }
    return false;
}

bool es1SystemIsErrorConnectionCheck(void *)
{
    return false;
}

void *resolveHookTarget(const char *name)
{
    std::string module;
    void *target = SymbolResolver::GetInstance().ResolveSymbol(name, &module);
    return module == "UNRESOLVED_STUB" ? nullptr : target;
}

int installEs1Hook(const char *name, void *replacement)
{
    void *target = resolveHookTarget(name);
    if (!target)
    {
        log_warn("System ES1: optional hook target not found: %s", name);
        return 0;
    }

    const MH_STATUS status = MH_CreateHook(target, replacement, nullptr);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        log_error("System ES1: failed to hook %s (MinHook status %d)", name, static_cast<int>(status));
        return 0;
    }
    log_info("System ES1: hooked %s at %p", name, target);
    return 1;
}

int installEs1VolumeHook(const char *name, void *replacement, void **original)
{
    void *target = resolveHookTarget(name);
    if (!target)
    {
        log_warn("System ES1 audio: optional volume target not found: %s", name);
        return 0;
    }

    const MH_STATUS status = MH_CreateHook(target, replacement, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        log_warn("System ES1 audio: failed to hook %s (MinHook status %d)",
                 name, static_cast<int>(status));
        return 0;
    }
    log_info("System ES1 audio: clamped volume setter %s at %p", name, target);
    return 1;
}

int installEs1DongleHooks()
{
    int installed = 0;
    installed += installEs1Hook("hasp_login", reinterpret_cast<void *>(es1HaspLogin));
    installed += installEs1Hook("hasp_logout", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_get_size", reinterpret_cast<void *>(es1HaspGetSize));
    installed += installEs1Hook("hasp_read", reinterpret_cast<void *>(es1HaspRead));
    installed += installEs1Hook("hasp_write", reinterpret_cast<void *>(es1HaspWrite));
    installed += installEs1Hook("hasp_cleanup", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_encrypt", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_decrypt", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_free", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_get_rtc", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("hasp_get_sessioninfo", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("_ZNK6clHASP7IsErrorEv", reinterpret_cast<void *>(es1HaspSuccess));
    installed += installEs1Hook("_ZNK6clHASP11GetSerialNoEv", reinterpret_cast<void *>(es1HaspSerial));
    installed += installEs1Hook("_ZN10clSystemN27isErrorEv",
                                reinterpret_cast<void *>(es1SystemIsError));
    installed += installEs1Hook("_ZN10clSystemN222isErrorConnectionCheckEv",
                                reinterpret_cast<void *>(es1SystemIsErrorConnectionCheck));
    installed += installEs1VolumeHook("_ZN7nsAudio15SNDSetBgmVolumeEi",
                                      reinterpret_cast<void *>(es1SetBgmVolume),
                                      reinterpret_cast<void **>(&g_es1SetBgmVolumeOriginal));
    installed += installEs1VolumeHook("_ZN7nsAudio14SNDSetSeVolumeEi",
                                      reinterpret_cast<void *>(es1SetSeVolume),
                                      reinterpret_cast<void **>(&g_es1SetSeVolumeOriginal));
    installed += installEs1VolumeHook("_ZN7nsAudio18SNDSetEngineVolumeEi",
                                      reinterpret_cast<void *>(es1SetEngineVolume),
                                      reinterpret_cast<void **>(&g_es1SetEngineVolumeOriginal));
    installed += installEs1VolumeHook("_ZN7nsAudio17SNDSetVoiceVolumeEi",
                                      reinterpret_cast<void *>(es1SetVoiceVolume),
                                      reinterpret_cast<void **>(&g_es1SetVoiceVolumeOriginal));
    installed += installEs1VolumeHook("_ZN7nsAudio18SNDSetMasterVolumeEib",
                                      reinterpret_cast<void *>(es1SetMasterVolume),
                                      reinterpret_cast<void **>(&g_es1SetMasterVolumeOriginal));
    installed += installEs1VolumeHook("_ZN7nsAudio19SNDSetAttractVolumeEib",
                                      reinterpret_cast<void *>(es1SetAttractVolume),
                                      reinterpret_cast<void **>(&g_es1SetAttractVolumeOriginal));
    log_info("System ES1: installed %d independent HASP/legacy compatibility hooks", installed);
    return installed > 0 ? 0 : -1;
}

}

extern "C" int es1PrepareLoad(const char *elfPath)
{
    return es1PrepareTestModeCompat(elfPath);
}

extern "C" int es1DetectGame(const char *elfPath)
{
    g_detected = false;
    if (!elfPath || !*elfPath)
        return 0;

    const std::filesystem::path elf(elfPath);
    const std::filesystem::path root = elf.parent_path().parent_path().parent_path();
    const std::string info = readFile(root / "info");
    const std::string csv = readFile(root / "data" / "csv" / "config.csv");

    /*
     * Maximum Heat 3D has no N2 gRomInfo marker.  Its package metadata and
     * JAMMA/camera/display configuration are stable across the known dump.
     * Require the package evidence instead of relying on the directory name.
     */
    const bool packageName = has(info, "US DRIVE") || has(info, "Maximum Heat 3D");
    const bool es1Config = has(csv, "USE_JAMMA_DEVICE") &&
                           has(csv, "USE_CAMERA_DEVICE") &&
                           has(csv, "VIDEO_XSIZE=1360") &&
                           has(csv, "VIDEO_YSIZE=768");
    if (!packageName && !es1Config)
        return 0;

    g_detected = true;
    log_info("Detected Namco System ES1 title: Maximum Heat 3D");
    return 1;
}

extern "C" int es1IsDetected(void)
{
    return g_detected ? 1 : 0;
}

extern "C" int es1InstallHooks(void)
{
    log_info("System ES1: installing ES1-only cabinet and license compatibility hooks");
    if (es1InstallTestModeCompatHook() != 0)
        return -1;
    const int dongleHooks = installEs1DongleHooks();
    return dongleHooks == 0 ? 0 : -1;
}

extern "C" int es1InstallLateHooks(void)
{
    return 0;
}

extern "C" int es1InitializeGraphics(void)
{
    /* N2 creates the shared context in its own graphics backend. ES1 has no
     * N2 graphics hook, so it must explicitly bring up the common SDL/GL
     * window before pacloader reports GL capabilities. */
    startSDL();
    return 0;
}

extern "C" int es1HandleSystemCommand(const char *command)
{
    if (!command)
        return 0;

    const std::string requested(command);
    const int networkResult = es1HostNetworkCommand(command);
    if (networkResult >= 0)
        return networkResult;

    /* Maximum Heat 3D's cabinet bootstrap launches helper daemons. They are
     * not gameplay dependencies on a host loader, and must not be forwarded
     * to Windows. */
    if (requested.rfind("su ", 0) == 0 || requested.rfind("sudo ", 0) == 0 ||
        requested.find("arping.sh") != std::string::npos ||
        requested.find("pinger.pl") != std::string::npos ||
        requested.find("killall perl") != std::string::npos ||
        requested.find("/proc/sys/net/") != std::string::npos)
        return 0;
    return -1;
}

extern "C" const char *es1GetGameTitle(void)
{
    return "Maximum Heat 3D";
}

extern "C" const char *es1GetGameShortTitle(void)
{
    return "Maximum Heat 3D";
}

extern "C" const char *es1GetGameId(void)
{
    return "MHEAT3D";
}

extern "C" const char *es1GetRevision(void)
{
    return "8807";
}
