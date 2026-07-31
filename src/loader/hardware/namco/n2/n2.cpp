#include "n2.h"
#include "n2Audio.h"
#include "n2CardReader.h"
#include "n2Hook.h"
#include "n2SteeringIo.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "../../../config/config.h"
#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../elfLoader/glHooks.hpp"
#include "../../../graphics/fpsLimiter.h"
#include "../../../graphics/sdlCalls.h"
#include "../../../input/sdlInput.h"
#include "../../../log/log.h"

namespace
{
#pragma pack(push, 1)
struct RomInfo
{
    char name[32];
    char region[32];
    char releaseType[32];
    char date[32];
    char time[32];
    int32_t revision;
    char revisionName[32];
};

struct AdmMode
{
    char ident[4];
    uint32_t unknown[5];
    uint32_t width;
    uint32_t height;
    uint32_t refreshMilliHz;
};

struct AdmWindow
{
    char ident[4];
    SDL_Window *window;
};
#pragma pack(pop)

N2Game detectedGame = N2_GAME_NONE;
std::string detectedRevision;
uint8_t *n2Jvio = nullptr;
uint32_t nextHaspHandle = 1;
constexpr int n2HaspDataSize = 0xD40;
constexpr char defaultN2DongleId[] = "000001000001";
// A cabinet never carries two dongles with the same serial, and the attract
// screen prints both, so the fallbacks have to differ.
constexpr char defaultN2DongleId2[] = "000001000002";
uint8_t n2HaspData[n2HaspDataSize] = {};
bool n2HaspDataInitialized = false;
int currentGear = 0;
bool previousGearUp = false;
bool previousGearDown = false;
AdmMode admMode = {};
AdmMode *admModeList[2] = {};
uint32_t admFbConfig = 0;
AdmWindow admWindow = {};

bool isPrintableString(const char *value, size_t capacity)
{
    size_t length = 0;
    for (; length < capacity && value[length] != '\0'; ++length)
    {
        if (!std::isprint(static_cast<unsigned char>(value[length])))
            return false;
    }
    return length > 0 && length < capacity;
}

int returnSuccess()
{
    return 1;
}

int returnHaspSuccess()
{
    return 0;
}

/*
 * clHasp::getCount() counts "Product=HASP" entries in /proc/bus/usb/devices,
 * which Windows does not have, so it always returned zero.  Both seqTitle()
 * and clSeqTitleThread::run() treat a zero count as "no dongle attached":
 * they clear the cached serial and never call getSerialHi/getSerialLo, and
 * the attract screen skips the S/N digits entirely when both halves are zero.
 *
 * A WMMT3DX+ cabinet ships with two dongles, and seqTitle() only reads the
 * clHasp2 serial when the count is greater than one - that is the second S/N
 * line on the attract screen.
 */
int getN2HaspCount()
{
    return 2;
}

bool isValidN2DongleId(const char *dongleId)
{
    if (!dongleId || std::strlen(dongleId) != 12)
        return false;

    return std::all_of(dongleId, dongleId + 12, [](unsigned char value) {
        return value >= '0' && value <= '9';
    });
}

void setHaspRecordChecksum(uint8_t *record)
{
    uint8_t checksum = 0;
    for (int i = 0; i < 14; ++i)
        checksum = static_cast<uint8_t>(checksum + record[i]);
    record[14] = checksum;
    record[15] = static_cast<uint8_t>(checksum ^ 0xFF);
}

void initializeN2HaspData()
{
    if (n2HaspDataInitialized)
        return;

    std::memset(n2HaspData, 0, sizeof(n2HaspData));

    /*
     * clHasp2 expects a checksum-protected 16-byte card-state record at
     * offset zero.  An all-zero payload represents zero stored card IDs.
     */
    setHaspRecordChecksum(n2HaspData);

    const char *configuredId = getConfig()->n2DongleId;
    const char *dongleId = isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId;
    std::memcpy(n2HaspData + 0xD00, dongleId, 12);

    /*
     * The 64-byte identity block stores its checksum and one's complement
     * in the final two bytes.
     */
    uint8_t checksum = 0;
    for (int i = 0; i < 0x3E; ++i)
        checksum = static_cast<uint8_t>(checksum + n2HaspData[0xD00 + i]);
    n2HaspData[0xD3E] = checksum;
    n2HaspData[0xD3F] = static_cast<uint8_t>(checksum ^ 0xFF);

    n2HaspDataInitialized = true;
    log_info("Namco N2: virtual USB dongle initialized (S/N %.6s-%.6s)",
             dongleId, dongleId + 6);
}

uint32_t parseDongleSerialPart(const char *digits)
{
    uint32_t value = 0;
    for (int i = 0; i < 6; ++i)
        value = value * 10 + static_cast<uint32_t>(digits[i] - '0');
    return value;
}

const char *getN2DongleId()
{
    const char *configuredId = getConfig()->n2DongleId;
    return isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId;
}

uint32_t getN2DongleSerialHi()
{
    return parseDongleSerialPart(getN2DongleId());
}

uint32_t getN2DongleSerialLo()
{
    return parseDongleSerialPart(getN2DongleId() + 6);
}

const char *getN2Dongle2Id()
{
    const char *configuredId = getConfig()->n2DongleId2;
    return isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId2;
}

uint32_t getN2Dongle2SerialHi()
{
    return parseDongleSerialPart(getN2Dongle2Id());
}

uint32_t getN2Dongle2SerialLo()
{
    return parseDongleSerialPart(getN2Dongle2Id() + 6);
}

void openN2Hasp2(void *object)
{
    if (!object)
        return;

    uint8_t *state = *reinterpret_cast<uint8_t **>(object);
    if (!state)
        return;

    initializeN2HaspData();
    *reinterpret_cast<int32_t *>(state + 0x00) = 0;
    *reinterpret_cast<uint32_t *>(state + 0x04) = nextHaspHandle++;
    *reinterpret_cast<uint32_t *>(state + 0x08) = getN2Dongle2SerialHi();
    *reinterpret_cast<uint32_t *>(state + 0x0C) = getN2Dongle2SerialLo();
    std::memcpy(state + 0x10, n2HaspData, 16);
}

void openN2Hasp(void *object)
{
    if (!object)
        return;

    uint8_t *state = *reinterpret_cast<uint8_t **>(object);
    if (!state)
        return;

    initializeN2HaspData();
    *reinterpret_cast<int32_t *>(state + 0x00) = 0;
    *reinterpret_cast<uint32_t *>(state + 0x04) = nextHaspHandle++;
    state[0x08] = 0; // low-battery flag
    *reinterpret_cast<uint32_t *>(state + 0x0C) = getN2DongleSerialHi();
    *reinterpret_cast<uint32_t *>(state + 0x10) = getN2DongleSerialLo();
    std::memcpy(state + 0x14, n2HaspData, 16);
}

void testN2Hasp2(void *object)
{
    openN2Hasp2(object);
}

void testN2Hasp(void *object)
{
    openN2Hasp(object);
}

/*
 * emGCPResult value 3 is the cabinet's
 * "E51 リーダライターの接続を確認してください", i.e. the reader did not answer.
 */
constexpr int gcpResultReaderDisconnected = 3;

/*
 * clCardDeviceGameService keeps its public result/status block at +0x2c and a
 * non-null active process at +0x34.  Completing a request means filling that
 * block in and clearing the process so the caller stops waiting.
 */
int completeCardDeviceRequest(uint8_t *service, int result)
{
    if (!service)
        return 0;

    uint8_t *status = *reinterpret_cast<uint8_t **>(service + 0x2C);
    if (status)
    {
        *reinterpret_cast<int *>(status) = result;
        *reinterpret_cast<int *>(status + 0x08) = 0; // no card inserted
        *(status + 0x0C) = 0;                        // dispenser available
    }
    *reinterpret_cast<void **>(service + 0x34) = nullptr;
    return 1;
}

using CardRequest = int (*)(uint8_t *service);
using CardRequestFlag = int (*)(uint8_t *service, int flag);

CardRequest originalRequestGetStatus = nullptr;
CardRequestFlag originalRequestInit = nullptr;
CardRequest originalRequestCheckDispenser = nullptr;

int getStatusCardDevice(uint8_t *service)
{
    if (n2CardReaderIsConnected() && originalRequestGetStatus)
        return originalRequestGetStatus(service);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}

int initCardDevice(uint8_t *service, int flag)
{
    if (n2CardReaderIsConnected() && originalRequestInit)
        return originalRequestInit(service, flag);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}

int checkDispenserCardDevice(uint8_t *service)
{
    if (n2CardReaderIsConnected() && originalRequestCheckDispenser)
        return originalRequestCheckDispenser(service);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}

int haspLogin(int, int, uint32_t *handle)
{
    initializeN2HaspData();
    if (handle)
        *handle = nextHaspHandle++;
    return 0;
}

int haspGetSize(int, int, int *size)
{
    if (size)
        *size = 0xD40;
    return 0;
}

int haspRead(int, int, int offset, int length, uint8_t *buffer)
{
    if (!buffer || offset < 0 || length < 0 || offset > n2HaspDataSize ||
        length > n2HaspDataSize - offset)
        return 1;

    initializeN2HaspData();
    std::memcpy(buffer, n2HaspData + offset, static_cast<size_t>(length));
    return 0;
}

int haspWrite(int, int, int offset, int length, const uint8_t *buffer)
{
    if (!buffer || offset < 0 || length < 0 || offset > n2HaspDataSize ||
        length > n2HaspDataSize - offset)
        return 1;

    initializeN2HaspData();
    std::memcpy(n2HaspData + offset, buffer, static_cast<size_t>(length));
    return 0;
}

uint32_t gearBits(int gear)
{
    switch (gear)
    {
        case 1: return 0x01 | 0x04;
        case 2: return 0x01 | 0x08;
        case 3: return 0x04;
        case 4: return 0x08;
        case 5: return 0x02 | 0x04;
        case 6: return 0x02 | 0x08;
        default: return 0;
    }
}

uint16_t n2GearSwitches(int gear)
{
    /*
     * The WMMT3 shifter is wired to four switches in the first JVS player
     * word. These are the source masks used by the game's JAMMA adapter.
     */
    switch (gear)
    {
        case 1: return 0x20 | 0x80;
        case 2: return 0x20 | 0x40;
        case 3: return 0x80;
        case 4: return 0x40;
        case 5: return 0x10 | 0x80;
        case 6: return 0x10 | 0x40;
        default: return 0;
    }
}

float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

float normalizedAnalogue(const JVSIO *io, JVSInput channel)
{
    if (!io || io->analogueMax <= 0)
        return channel == ANALOGUE_1 ? 0.5f : 0.0f;
    return clamp01(static_cast<float>(io->state.analogueChannel[channel]) /
                   static_cast<float>(io->analogueMax));
}

uint16_t calibratedRaw(float normalized, uint16_t offset, uint16_t range)
{
    const float raw = static_cast<float>(offset) + clamp01(normalized) * static_cast<float>(range);
    return static_cast<uint16_t>(std::min(raw, 65535.0f));
}

bool switchActive(const JVSIO *io, JVSPlayer player, JVSInput input)
{
    return io && (io->state.inputSwitch[player] & input) != 0;
}

void handleJammaEvents(uint8_t *device)
{
    if (!device)
        return;

    pollEvents();

    /*
     * pollEvents() feeds the shared SDL/controls.ini path into the existing
     * JVS state. N2 only translates that state into Namco's memory layout, so
     * every switch below is whatever controls.ini bound to the corresponding
     * logical action, exactly like a Lindbergh driving game.
     *
     * GearUp/GearDown live on PLAYER_2 because that is where initJvsMappings()
     * puts them for DRIVING titles.
     */
    JVSIO *io = getJVSIO();
    const bool gearUp = switchActive(io, PLAYER_2, BUTTON_UP);
    const bool gearDown = switchActive(io, PLAYER_2, BUTTON_DOWN);
    if (gearUp && !previousGearUp && currentGear < 6)
        ++currentGear;
    if (gearDown && !previousGearDown && currentGear > 0)
        --currentGear;
    previousGearUp = gearUp;
    previousGearDown = gearDown;

    const float steerNormalized = normalizedAnalogue(io, ANALOGUE_1);
    const float steer = (steerNormalized - 0.5f) * 2.0f;
    const float gas = normalizedAnalogue(io, ANALOGUE_2);
    const float brake = normalizedAnalogue(io, ANALOGUE_3);
    const bool test = switchActive(io, SYSTEM, BUTTON_TEST);
    const bool service = switchActive(io, PLAYER_1, BUTTON_SERVICE);
    const bool start = switchActive(io, PLAYER_1, BUTTON_START);
    const bool view = switchActive(io, PLAYER_1, BUTTON_1);
    const bool music = switchActive(io, PLAYER_1, BUTTON_2);
    const bool shifterUp = switchActive(io, PLAYER_1, BUTTON_UP);
    const bool shifterDown = switchActive(io, PLAYER_1, BUTTON_DOWN);
    const bool shifterLeft = switchActive(io, PLAYER_1, BUTTON_LEFT);
    const bool shifterRight = switchActive(io, PLAYER_1, BUTTON_RIGHT);
    const bool manualShifterSwitch =
        shifterUp || shifterDown || shifterLeft || shifterRight;

    uint32_t bits = gearBits(currentGear);
    if (manualShifterSwitch)
    {
        currentGear = 0;
        bits = 0;
        if (shifterLeft)
            bits |= 0x01;
        if (shifterRight)
            bits |= 0x02;
        if (shifterUp)
            bits |= 0x04;
        if (shifterDown)
            bits |= 0x08;
    }
    if (test)
        bits |= 0x20000000;
    if (service || start)
        bits |= 0x80000000;
    if (view)
        bits |= 0x40;
    if (music)
        bits |= 0x80;
    if (gas > 0.0f)
        bits |= 0x10;
    if (brake > 0.0f)
        bits |= 0x20;

    *reinterpret_cast<uint32_t *>(device + 0x24) = 0;
    *reinterpret_cast<uint32_t *>(device + 0x08) = bits;
    *reinterpret_cast<float *>(device + 0x20) = steer;
    *reinterpret_cast<float *>(device + 0x30) = gas;
    *reinterpret_cast<float *>(device + 0x34) = brake;

    if (n2Jvio)
    {
        uint16_t playerSwitches = n2GearSwitches(currentGear);
        if (manualShifterSwitch)
        {
            playerSwitches = 0;
            if (shifterLeft)
                playerSwitches |= 0x20;
            if (shifterRight)
                playerSwitches |= 0x10;
            if (shifterUp)
                playerSwitches |= 0x80;
            if (shifterDown)
                playerSwitches |= 0x40;
        }
        if (view)
            playerSwitches |= 0x02;
        if (music)
            playerSwitches |= 0x01;
        if (service || start)
            playerSwitches |= 0x4000;

        *(n2Jvio + 0x104) = test ? 0x80 : 0x00;
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x108) = playerSwitches;

        /*
         * These are the live calibration values used by W3P100-1-NA-DAT0-B02:
         * steering center/range, accelerator rest/range, and brake rest/range.
         */
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x1A8) =
            calibratedRaw(steerNormalized, 0x197C, 0xC000);
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x1AA) =
            calibratedRaw(gas, 0x88B8, 0x5000);
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x1AC) =
            calibratedRaw(brake, 0x7148, 0x5000);

        const int coinCount = io ? io->state.coinCount[PLAYER_1 - 1] : 0;
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x128) =
            static_cast<uint16_t>(std::max(0, std::min(coinCount, 65535)));
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x12A) = 0;
        *reinterpret_cast<uint16_t *>(n2Jvio + 0x12C) = 0;
        *(n2Jvio + 0x12E) = 0;
    }
}

const char *admGetString()
{
    return "Linux Loader Namco N2";
}

AdmMode **admChooseMode()
{
    std::memset(&admMode, 0, sizeof(admMode));
    std::memcpy(admMode.ident, "MOCF", 4);
    admMode.width = static_cast<uint32_t>(getConfig()->width);
    admMode.height = static_cast<uint32_t>(getConfig()->height);
    admMode.refreshMilliHz = 60000;
    admModeList[0] = &admMode;
    admModeList[1] = nullptr;
    return admModeList;
}

uint32_t *admChooseFbConfig()
{
    return &admFbConfig;
}

AdmWindow *admCreateWindow()
{
    if (!getSDLWindow())
        startSDL();
    const size_t patchedGlFunctions =
        SymbolResolver::GetInstance().PatchNativeJumpStubs("gl", GLHooks_GetProcAddress);
    log_info("Namco N2: initialized %zu writable OpenGL entry points", patchedGlFunctions);
    std::memcpy(admWindow.ident, "WNDW", 4);
    admWindow.window = getSDLWindow();
    return admWindow.window ? &admWindow : nullptr;
}

int admMakeCurrent()
{
    SDL_Window *window = getSDLWindow();
    return window && getSDLContext() && SDL_GL_MakeCurrent(window, getSDLContext()) ? 1 : 0;
}

int admSwapBuffers(AdmWindow *)
{
    pollEvents();
    SDL_Window *window = getSDLWindow();
    if (!window)
        return 0;
    SDL_GL_SwapWindow(window);

    /*
     * [Graphics] FPS_LIMITER_ENABLED / FPS_TARGET are applied from the present
     * call, which for Lindbergh titles is glXSwapBuffers or glutSwapBuffers.
     * N2 presents through the ADM library instead, so the limiter has to be
     * driven from here or the settings do nothing.
     */
    if (getConfig()->fpsLimiter)
        frameTiming();

    // Same readout the Lindbergh present path puts in the title bar.
    static char windowTitle[128] = {};
    std::snprintf(windowTitle, sizeof(windowTitle), "%s - FPS: %.2f",
                  n2GetGameTitle(), calculateFps());
    SDL_SetWindowTitle(window, windowTitle);

    return 1;
}

int admSwapInterval(int interval)
{
    /*
     * alchemy.ini asks for VSYNC, which pins presentation to the panel's
     * refresh.  A cabinet monitor runs at the 60Hz the game is written for, but
     * on a desktop panel of any other rate the vblank cadence cannot express
     * the requested frame time: at 75Hz the frames land on 13.3ms or 26.7ms and
     * the software limiter can only average 60 by alternating between them,
     * which is the 55-60 wobble.  When the limiter is doing the pacing it has
     * to own it alone, so vsync is left off.
     */
    if (getConfig()->fpsLimiter && interval != 0)
    {
        log_info("Namco N2: vsync suppressed; [Graphics] FPS_TARGET drives the frame rate");
        interval = 0;
    }
    return SDL_GL_SetSwapInterval(interval) ? 1 : 0;
}

using ClAppGetInstance = void *(*)();
using ClAppIsMainThread = bool (*)(void *);
using ThreadManagerCurrent = void *(*)(void *);
using CallFromMainThread = void (*)(void *, void (*)(void *), void *);
using CreateTextureHandle = int (*)(void *, int, int);
using SetTexture = int (*)(void *, int, int);
using SetTextureRegion = int (*)(void *, int, int, int, int, int, int, void *);

ClAppGetInstance clAppGetInstance = nullptr;
ClAppIsMainThread clAppIsMainThread = nullptr;
void **clMainInstance = nullptr;
ThreadManagerCurrent threadManagerCurrent = nullptr;
CallFromMainThread callFromMainThread = nullptr;
CreateTextureHandle originalCreateTextureHandle = nullptr;
SetTexture originalSetTexture = nullptr;
SetTextureRegion originalSetTextureRegion = nullptr;
using FindMetaType = void *(*)(const char *);
FindMetaType originalFindMetaType = nullptr;
using ArenaMallocAligned = void *(*)(void *, uint32_t, uint32_t);
ArenaMallocAligned originalArenaMallocAligned = nullptr;
using InstantiateImage = void *(*)(void *);
InstantiateImage originalInstantiateImage = nullptr;
using AllocateImageMemory = void (*)(void *);
AllocateImageMemory originalAllocateImageMemory = nullptr;
using AutoSetImageParameters = void (*)(void *);
AutoSetImageParameters originalAutoSetImageParameters = nullptr;
thread_local bool registeringN2ShaderMetadata = false;
std::atomic_bool n2ShaderMetadataRegistered = false;
std::recursive_mutex n2ShaderMetadataMutex;

void registerN2ShaderMetadata()
{
    static const char *registrationSymbols[] = {
        "_ZN3Gap3Gfx19igGfxShaderConstant11arkRegisterEv",
        "_ZN3Gap3Gfx23igGfxShaderConstantList11arkRegisterEv",
        "_ZN3Gap3Gfx17igGfxShaderDefine11arkRegisterEv",
        "_ZN3Gap3Gfx21igGfxShaderDefineList11arkRegisterEv",
        "_ZN3Gap3Gfx22igTextureSamplerSource11arkRegisterEv",
        "_ZN3Gap3Gfx26igTextureSamplerSourceList11arkRegisterEv",
        "_ZN3Gap5Attrs17igPixelShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs21igPixelShaderAttrList11arkRegisterEv",
        "_ZN3Gap5Attrs18igVertexShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs22igVertexShaderAttrList11arkRegisterEv"
    };

    for (const char *symbol : registrationSymbols)
    {
        void (*registration)() = reinterpret_cast<void (*)()>(n2ResolveSymbol(symbol));
        if (registration)
            registration();
    }
}

void *findN2MetaType(const char *name)
{
    if (!name)
        return originalFindMetaType(name);
    const bool isShaderType =
        std::strstr(name, "igGfxShader") == name ||
        std::strstr(name, "igTextureSampler") == name ||
        std::strcmp(name, "igPixelShaderAttr") == 0 ||
        std::strcmp(name, "igPixelShaderAttrList") == 0 ||
        std::strcmp(name, "igVertexShaderAttr") == 0 ||
        std::strcmp(name, "igVertexShaderAttrList") == 0;

    if (isShaderType && !registeringN2ShaderMetadata &&
        !n2ShaderMetadataRegistered.load(std::memory_order_acquire))
    {
        std::lock_guard<std::recursive_mutex> lock(n2ShaderMetadataMutex);
        if (!n2ShaderMetadataRegistered.load(std::memory_order_relaxed))
        {
            registeringN2ShaderMetadata = true;
            registerN2ShaderMetadata();
            registeringN2ShaderMetadata = false;
            n2ShaderMetadataRegistered.store(true, std::memory_order_release);
            log_info("Namco N2: registered Alchemy shader metadata");
        }
    }
    return originalFindMetaType(name);
}

void *n2ArenaMallocAligned(void *pool, uint32_t size, uint32_t alignment)
{
    void *memory = originalArenaMallocAligned(pool, size, alignment);
    if (!memory)
    {
        using PoolSizeFn = uint32_t (*)(void *);
        PoolSizeFn getTotalArenaSize = reinterpret_cast<PoolSizeFn>(
            n2ResolveSymbol("_ZNK3Gap4Core17igArenaMemoryPool17getTotalArenaSizeEv"));
        PoolSizeFn getLargestAllocation = reinterpret_cast<PoolSizeFn>(
            n2ResolveSymbol("_ZNK3Gap4Core17igArenaMemoryPool33getLargestAvailableAllocationSizeEv"));
        const char *poolName = pool
            ? reinterpret_cast<const char *>(static_cast<uint8_t *>(pool) + 8)
            : "(null)";
        log_error("Namco N2: arena allocation failed pool=%s size=%u alignment=%u "
                  "total=%u largest=%u",
                  poolName, size, alignment,
                  pool && getTotalArenaSize ? getTotalArenaSize(pool) : 0,
                  pool && getLargestAllocation ? getLargestAllocation(pool) : 0);
    }
    return memory;
}

void *getN2FallbackMemoryPool()
{
    using PoolAdaptorFunction = void *(*)();
    using PoolAdaptorGet = void *(*)(void *);
    PoolAdaptorFunction systemPoolFunction = reinterpret_cast<PoolAdaptorFunction>(
        n2ResolveSymbol("_ZN3Gap4Core27igMemoryPoolSystem_functionEv"));
    PoolAdaptorGet getPool = reinterpret_cast<PoolAdaptorGet>(
        n2ResolveSymbol("_ZNK3Gap4Core19igMemoryPoolAdaptorptEv"));
    void *adaptor = systemPoolFunction ? systemPoolFunction() : nullptr;
    return adaptor && getPool ? getPool(adaptor) : nullptr;
}

void *n2InstantiateImage(void *pool)
{
    void *image = originalInstantiateImage(pool);
    if (!image && !pool)
    {
        void *fallbackPool = getN2FallbackMemoryPool();
        if (fallbackPool)
            image = originalInstantiateImage(fallbackPool);
    }
    return image;
}

void n2AllocateImageMemory(void *image)
{
    if (!image)
        return;

    uint8_t *object = static_cast<uint8_t *>(image);
    void *&data = *reinterpret_cast<void **>(object + 0x34);
    uint32_t &imageSize = *reinterpret_cast<uint32_t *>(object + 0x30);
    const uint32_t compressedImageSize = GLHooks_ConsumeCompressedImageSize();

    /*
     * WMMT3 queries the exact compressed mip size from OpenGL immediately
     * before constructing the corresponding igImage.  Carry that authoritative
     * value across the Alchemy allocation instead of trusting its occasionally
     * stale _imageSize field.
     */
    if (compressedImageSize && compressedImageSize <= 256 * 1024 * 1024)
    {
        const uint32_t staleSize = imageSize;
        if (originalAutoSetImageParameters)
            originalAutoSetImageParameters(image);
        imageSize = compressedImageSize;
        if (!data)
        {
            void *fallbackPool = getN2FallbackMemoryPool();
            if (fallbackPool && originalArenaMallocAligned)
            {
                data = originalArenaMallocAligned(fallbackPool, imageSize, 128);
                if (data)
                {
                    object[0x3c] = 1;
                    if (staleSize != imageSize)
                        log_debug("Namco N2: corrected loadBuffer image size %u -> %u",
                                  staleSize, imageSize);
                    return;
                }
            }
        }

        originalAllocateImageMemory(image);
        imageSize = compressedImageSize;
        return;
    }

    originalAllocateImageMemory(image);
    if (!data && imageSize && imageSize <= 64 * 1024 * 1024)
    {
        void *fallbackPool = getN2FallbackMemoryPool();
        if (fallbackPool && originalArenaMallocAligned)
        {
            data = originalArenaMallocAligned(fallbackPool, imageSize, 128);
            if (data)
                object[0x3c] = 1;
        }
    }
}

bool isN2MainThread()
{
    return clAppGetInstance && clAppIsMainThread &&
           clAppIsMainThread(clAppGetInstance());
}

bool dispatchFromN2Worker(void (*callback)(void *), void *arguments)
{
    if (!clMainInstance || !*clMainInstance || !threadManagerCurrent || !callFromMainThread)
        return false;

    // clMain owns clNPThreadManager at offset 0x40 in the WMMT3 N2 executable.
    void *threadManager = *reinterpret_cast<void **>(
        reinterpret_cast<uint8_t *>(*clMainInstance) + 0x40);
    if (!threadManager)
        return false;

    void *currentThread = threadManagerCurrent(threadManager);
    if (!currentThread)
        return false;

    callFromMainThread(currentThread, callback, arguments);
    return true;
}

struct TextureHandleCall
{
    void *self;
    int width;
    int height;
};

void createTextureHandleOnMain(void *opaque)
{
    TextureHandleCall *call = static_cast<TextureHandleCall *>(opaque);
    originalCreateTextureHandle(call->self, call->width, call->height);
    delete call;
}

int createTextureHandle(void *self, int width, int height)
{
    if (isN2MainThread())
        return originalCreateTextureHandle(self, width, height);

    TextureHandleCall *call = new TextureHandleCall{self, width, height};
    if (dispatchFromN2Worker(createTextureHandleOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal texture creation to the main thread");
    return originalCreateTextureHandle(self, width, height);
}

struct SetTextureCall
{
    void *self;
    int texture;
    int image;
};

void setTextureOnMain(void *opaque)
{
    SetTextureCall *call = static_cast<SetTextureCall *>(opaque);
    originalSetTexture(call->self, call->texture, call->image);
    delete call;
}

int setTexture(void *self, int texture, int image)
{
    if (isN2MainThread())
        return originalSetTexture(self, texture, image);

    SetTextureCall *call = new SetTextureCall{self, texture, image};
    if (dispatchFromN2Worker(setTextureOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal texture upload to the main thread");
    return originalSetTexture(self, texture, image);
}

struct SetTextureRegionCall
{
    void *self;
    int texture;
    int x;
    int y;
    int width;
    int height;
    int format;
    void *image;
};

void setTextureRegionOnMain(void *opaque)
{
    SetTextureRegionCall *call = static_cast<SetTextureRegionCall *>(opaque);
    originalSetTextureRegion(call->self, call->texture, call->x, call->y,
                             call->width, call->height, call->format, call->image);
    delete call;
}

int setTextureRegion(void *self, int texture, int x, int y, int width, int height,
                     int format, void *image)
{
    if (isN2MainThread())
        return originalSetTextureRegion(self, texture, x, y, width, height, format, image);

    SetTextureRegionCall *call =
        new SetTextureRegionCall{self, texture, x, y, width, height, format, image};
    if (dispatchFromN2Worker(setTextureRegionOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal partial texture upload to the main thread");
    return originalSetTextureRegion(self, texture, x, y, width, height, format, image);
}
} // namespace

extern "C" int n2DetectGame(void)
{
    detectedGame = N2_GAME_NONE;
    detectedRevision.clear();

    RomInfo *romInfo = static_cast<RomInfo *>(n2ResolveSymbol("gRomInfo"));
    void *systemMarker = n2ResolveSymbol("_ZN10clSystemN212initSystemN2Ev");
    if (!romInfo || !systemMarker || !isPrintableString(romInfo->revisionName, sizeof(romInfo->revisionName)))
        return 0;

    detectedRevision.assign(romInfo->revisionName, strnlen(romInfo->revisionName, sizeof(romInfo->revisionName)));
    if (detectedRevision.find("WM3100") != std::string::npos)
        detectedGame = N2_GAME_WMMT3;
    else if (detectedRevision.rfind("W3P", 0) == 0)
        detectedGame = N2_GAME_WMMT3DX_PLUS;
    else if (detectedRevision.rfind("W3X", 0) == 0)
        detectedGame = N2_GAME_WMMT3DX;
    else
        detectedGame = N2_GAME_WMMT3_FAMILY;
    log_info("Detected Namco System N2 title, revision %s", detectedRevision.c_str());
    return 1;
}

extern "C" int n2IsDetected(void)
{
    return detectedGame != N2_GAME_NONE;
}

extern "C" N2Game n2GetGame(void)
{
    return detectedGame;
}

extern "C" const char *n2GetGameTitle(void)
{
    switch (detectedGame)
    {
        case N2_GAME_WMMT3: return "Wangan Midnight Maximum Tune 3";
        case N2_GAME_WMMT3DX: return "Wangan Midnight Maximum Tune 3DX";
        case N2_GAME_WMMT3DX_PLUS: return "Wangan Midnight Maximum Tune 3DX+";
        default: return "Wangan Midnight Maximum Tune 3 series";
    }
}

extern "C" const char *n2GetGameShortTitle(void)
{
    switch (detectedGame)
    {
        case N2_GAME_WMMT3: return "WMMT3";
        case N2_GAME_WMMT3DX: return "WMMT3DX";
        case N2_GAME_WMMT3DX_PLUS: return "WMMT3DX+";
        default: return "WMMT3 series";
    }
}

extern "C" const char *n2GetGameId(void)
{
    return detectedRevision.empty() ? "NAMCO-N2" : detectedRevision.c_str();
}

extern "C" const char *n2GetRevision(void)
{
    return detectedRevision.c_str();
}

extern "C" int n2InstallHooks(void)
{
    if (!n2IsDetected())
        return 0;

    n2Jvio = static_cast<uint8_t *>(n2ResolveSymbol("n2jvio"));

    n2HookSymbol("_ZN10clSystemN24initEb", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZN10clSystemN212initSystemN2Ev", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZN18clInputDeviceJamma8checkUseEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZN18clInputDeviceJamma12handleEventsEv", reinterpret_cast<void *>(handleJammaEvents));
    n2HookSymbol("_ZN16clInputDevicePad12handleEventsEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("n2JvioTxVsync", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("n2JvioAckTxVsync", reinterpret_cast<void *>(returnSuccess));

    // WMMT3DX+ performs these cabinet checks before entering attract mode.
    // SDL/JVS continues to provide steering and switch input after the check.
    n2HookSymbol("_ZN10clKickback16requestSelfCheckEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZN10clKickback13waitSelfCheckEv", reinterpret_cast<void *>(returnSuccess));
    n2SteeringIoInstallHooks();

    // Silence is not fatal, so a missing openal32.dll must not abort startup.
    n2AudioInstallHooks();

    /*
     * These are always hooked so the reported reader state follows the live
     * YaCardEmu connection instead of a config flag.  When the pipe is up the
     * detours forward to the game's own request, so the real serial
     * conversation still happens; when it is down they answer E51 the way a
     * cabinet with an unplugged reader/writer does.
     */
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService16requestGetStatusEv",
                             reinterpret_cast<void *>(getStatusCardDevice),
                             reinterpret_cast<void **>(&originalRequestGetStatus));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService11requestInitEb",
                             reinterpret_cast<void *>(initCardDevice),
                             reinterpret_cast<void **>(&originalRequestInit));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService21requestCheckDispenserEv",
                             reinterpret_cast<void *>(checkDispenserCardDevice),
                             reinterpret_cast<void **>(&originalRequestCheckDispenser));

    if (n2CardReaderIsConnected())
        log_info("Namco N2 card: YaCardEmu reader connected");
    else
        log_warn("Namco N2 card: no YaCardEmu reader on %s; the cabinet will report E51 "
                 "until it is running",
                 getConfig()->n2YaCardEmuPipe);

    n2HookSymbol("hasp_cleanup", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_decrypt", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_encrypt", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_free", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_rtc", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_sessioninfo", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_size", reinterpret_cast<void *>(haspGetSize));
    n2HookSymbol("hasp_login", reinterpret_cast<void *>(haspLogin));
    n2HookSymbol("hasp_logout", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_read", reinterpret_cast<void *>(haspRead));
    n2HookSymbol("hasp_write", reinterpret_cast<void *>(haspWrite));
    n2HookSymbol("_ZNK6clHasp7isAvailEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK7clHasp27isAvailEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK6clHasp8getCountEv", reinterpret_cast<void *>(getN2HaspCount));
    n2HookSymbol("_ZNK7clHasp28getCountEv", reinterpret_cast<void *>(getN2HaspCount));
    /*
     * Both classes probe /proc/bus/usb/devices inside open(), before calling
     * the HASP API.  Windows has no such procfs entry, so initialize the
     * already-constructed x86 class state directly.
     */
    n2HookSymbol("_ZN6clHasp4openEv", reinterpret_cast<void *>(openN2Hasp));
    n2HookSymbol("_ZN7clHasp24openEv", reinterpret_cast<void *>(openN2Hasp2));
    n2HookSymbol("_ZN6clHasp4testEv", reinterpret_cast<void *>(testN2Hasp));
    n2HookSymbol("_ZN7clHasp24testEv", reinterpret_cast<void *>(testN2Hasp2));
    n2HookSymbol("_ZNK6clHaspcvbEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK7clHasp2cvbEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK6clHasp8getErrorEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK7clHasp28getErrorEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK6clHasp12isLowBatteryEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK6clHasp11getSerialHiEv", reinterpret_cast<void *>(getN2DongleSerialHi));
    n2HookSymbol("_ZNK6clHasp11getSerialLoEv", reinterpret_cast<void *>(getN2DongleSerialLo));
    n2HookSymbol("_ZNK7clHasp211getSerialHiEv", reinterpret_cast<void *>(getN2Dongle2SerialHi));
    n2HookSymbol("_ZNK7clHasp211getSerialLoEv", reinterpret_cast<void *>(getN2Dongle2SerialLo));

    n2HookSymbol("admvt_setup", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admShutdown", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admGetString", reinterpret_cast<void *>(admGetString));
    n2HookSymbol("admGetNumDevices", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admInitDevicei", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admChooseModeConfigi", reinterpret_cast<void *>(admChooseMode));
    n2HookSymbol("admModeConfigi", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admChooseFBConfigi", reinterpret_cast<void *>(admChooseFbConfig));
    n2HookSymbol("admCreateScreeni", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admCreateGraphicsContext", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admCreateWindowi", reinterpret_cast<void *>(admCreateWindow));
    n2HookSymbol("admDisplayScreen", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admMakeContextCurrent", reinterpret_cast<void *>(admMakeCurrent));
    n2HookSymbol("admSwapInterval", reinterpret_cast<void *>(admSwapInterval));
    n2HookSymbol("admCursorAttribi", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admGetDeviceAttribi", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admSwapBuffers", reinterpret_cast<void *>(admSwapBuffers));
    n2HookSymbol("admSetMonitorGamma", reinterpret_cast<void *>(returnSuccess));

    clAppGetInstance = reinterpret_cast<ClAppGetInstance>(
        n2ResolveSymbol("_ZN11clAppSystem11getInstanceEv"));
    clAppIsMainThread = reinterpret_cast<ClAppIsMainThread>(
        n2ResolveSymbol("_ZN11clAppSystem12isMainThreadEv"));
    clMainInstance = static_cast<void **>(
        n2ResolveSymbol("_ZN11teSingletonI10teSequenceI6clMainEE11sm_instanceE"));
    threadManagerCurrent = reinterpret_cast<ThreadManagerCurrent>(
        n2ResolveSymbol("_ZN17clNPThreadManager7currentEv"));
    callFromMainThread = reinterpret_cast<CallFromMainThread>(
        n2ResolveSymbol("_ZN10clNPThread26callFunctionFromMainThreadEPFvPvES0_"));

    if (clAppGetInstance && clAppIsMainThread && clMainInstance &&
        threadManagerCurrent && callFromMainThread)
    {
        n2HookSymbolWithOriginal(
            "_ZN24clAlchemyTextureAccessor19createTextureHandleEii",
            reinterpret_cast<void *>(createTextureHandle),
            reinterpret_cast<void **>(&originalCreateTextureHandle));
        n2HookSymbolWithOriginal(
            "_ZN3Gap3Gfx19igAGLEVisualContext10setTextureEii",
            reinterpret_cast<void *>(setTexture),
            reinterpret_cast<void **>(&originalSetTexture));
        n2HookSymbolWithOriginal(
            "_ZN3Gap3Gfx19igAGLEVisualContext16setTextureRegionEiiiiiiPNS0_7igImageE",
            reinterpret_cast<void *>(setTextureRegion),
            reinterpret_cast<void **>(&originalSetTextureRegion));
        log_info("Namco N2: installed main-thread texture dispatch");
    }
    else
    {
        log_warn("Namco N2: game thread dispatch symbols are incomplete; "
                 "worker-thread texture uploads remain unpatched");
    }

    n2HookSymbolWithOriginal(
        "_ZN3Gap4Core12igMetaObject8findTypeEPKc",
        reinterpret_cast<void *>(findN2MetaType),
        reinterpret_cast<void **>(&originalFindMetaType));
    n2HookSymbolWithOriginal(
        "_ZN3Gap4Core17igArenaMemoryPool13mallocAlignedEjj",
        reinterpret_cast<void *>(n2ArenaMallocAligned),
        reinterpret_cast<void **>(&originalArenaMallocAligned));
    n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx7igImage20_instantiateFromPoolEPNS_4Core12igMemoryPoolE",
        reinterpret_cast<void *>(n2InstantiateImage),
        reinterpret_cast<void **>(&originalInstantiateImage));
    originalAutoSetImageParameters = reinterpret_cast<AutoSetImageParameters>(
        n2ResolveSymbol("_ZN3Gap3Gfx7igImage25autoSetUnfilledParametersEv"));
    n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx7igImage19allocateImageMemoryEv",
        reinterpret_cast<void *>(n2AllocateImageMemory),
        reinterpret_cast<void **>(&originalAllocateImageMemory));

    if (!isValidN2DongleId(getConfig()->n2DongleId))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId);
    if (!isValidN2DongleId(getConfig()->n2DongleId2))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID_2 is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId2);

    log_info("Namco N2 compatibility hooks installed");
    return 0;
}

extern "C" int n2InitializeGraphics(void)
{
    if (!n2IsDetected())
        return 0;
    if (!getSDLWindow())
        startSDL();
    const size_t patchedGlFunctions =
        SymbolResolver::GetInstance().PatchNativeJumpStubs("gl", GLHooks_GetProcAddress);
    log_info("Namco N2: initialized %zu writable OpenGL entry points", patchedGlFunctions);
    return patchedGlFunctions > 0 ? 0 : 1;
}

extern "C" int n2HandleSystemCommand(const char *command)
{
    if (!n2IsDetected() || !command)
        return -1;

    if (std::strncmp(command, "find ", 5) == 0 && std::strstr(command, ">/tmp/find.txt"))
    {
        struct FindCommand
        {
            const char *prefix;
            const char *directory;
            const char *extension;
        };
        const FindCommand commands[] = {
            {"find /tmp/data/target/", "tmp/data/target", ".target.gz"},
            {"find data/target/jp", "data/target/jp", ".target.gz"},
            {"find data/target/us", "data/target/us", ".target.gz"},
            {"find /tmp/data/ranking/", "tmp/data/ranking", ".rank"},
            {"find /tmp/data/maxicoin/", "tmp/data/maxicoin", ".maxicoin"},
            {"find /tmp/data/joinstar/", "tmp/data/joinstar", ".joinstar"}
        };
        for (const FindCommand &item : commands)
        {
            if (std::strncmp(command, item.prefix, std::strlen(item.prefix)) != 0)
                continue;

            std::vector<std::string> matches;
            std::error_code iteratorError;
            if (std::filesystem::exists(item.directory))
            {
                for (const auto &entry : std::filesystem::directory_iterator(item.directory, iteratorError))
                {
                    const std::string name = entry.path().filename().string();
                    if (entry.is_regular_file() && name.size() >= std::strlen(item.extension) &&
                        name.compare(name.size() - std::strlen(item.extension), std::strlen(item.extension), item.extension) == 0)
                        matches.push_back(name);
                }
            }
            std::sort(matches.begin(), matches.end());
            std::ofstream output("tmp/find.txt", std::ios::trunc);
            for (const std::string &name : matches)
                output << item.directory << "/" << name << "\n";
            return output ? 0 : 1;
        }
    }

    if (std::strcmp(command, "cp -f data/target/*.target.gz /tmp/data/target/ 2>/dev/null") == 0)
    {
        std::error_code copyError;
        std::filesystem::create_directories("tmp/data/target", copyError);
        for (const auto &entry : std::filesystem::directory_iterator("data/target", copyError))
        {
            const std::string name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() >= 10 && name.compare(name.size() - 10, 10, ".target.gz") == 0)
                std::filesystem::copy_file(entry.path(), std::filesystem::path("tmp/data/target") / name,
                                           std::filesystem::copy_options::overwrite_existing, copyError);
        }
        return 0;
    }

    if (std::strcmp(command, "perl etc/ifconfig.pl > /tmp/ifconfig.txt") == 0)
    {
        std::error_code directoryError;
        std::filesystem::create_directories("tmp", directoryError);
        std::ofstream output("tmp/ifconfig.txt", std::ios::trunc);
        /*
         * Original script format: interface, IPv4, netmask, MAC, link.
         * A deterministic loopback interface is suitable for one cabinet.
         */
        output << "0 127 0 0 1 255 0 0 0 0 0 0 0 0 0 1\n";
        log_info("Namco N2: generated standalone network interface data");
        return output ? 0 : 1;
    }

    if (std::strncmp(command, "perl prepend-n2.pl", 18) != 0)
        return -1;

    const char *directories[] = {
        "tmp/data/target",
        "tmp/data/tournament",
        "tmp/data/ranking",
        "tmp/data/maxicoin",
        "tmp/data/joinstar",
        "tmp/data/card",
        "tmp/data/etc",
        "tmp/data2/tournament"
    };
    std::error_code error;
    for (const char *directory : directories)
    {
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            log_error("Namco N2: failed to prepare %s: %s", directory, error.message().c_str());
            return 1;
        }
    }

    struct CopyItem
    {
        const char *source;
        const char *destination;
    };
    const CopyItem copies[] = {
        {"data/sound/bgm/maxi3/sys_04.wav", "tmp/sys_04.wav"},
        {"data/sprite/Full_white.png", "tmp/joinshot.png"}
    };
    for (const CopyItem &item : copies)
    {
        if (std::filesystem::exists(item.source))
            std::filesystem::copy_file(item.source, item.destination, std::filesystem::copy_options::overwrite_existing, error);
        error.clear();
    }

    log_info("Namco N2: prepared virtual work disk directories");
    return 0;
}

#endif
