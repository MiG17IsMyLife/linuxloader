#include "n2.h"
#include "n2Audio.h"
#include "n2CardReader.h"
#include "n2Host.h"
#include "n2Hook.h"
#include "n2Jvio.h"
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
#include "../../../redirections/filesystemShared.h"
#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../elfLoader/glHooks.hpp"
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

    const char *configuredId = getConfig()->namcoN2.dongleId;
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
    const char *configuredId = getConfig()->namcoN2.dongleId;
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
    const char *configuredId = getConfig()->namcoN2.dongleId2;
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

uint16_t n2GearSwitches(int gear)
{
    /*
     * The WMMT3 shifter is wired to four switches in the first JVS player
     * word - BUTTON_3 through BUTTON_6, i.e. the top nibble of the second
     * switch byte. These are the masks the cabinet's JVIO board reports.
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

/*
 * What the cabinet's potentiometers put on the wire.
 *
 * The pedals rest at a fixed count and swing across the window the game
 * calibrates them in - clInputDeviceJamma::m_left_pedal_std and _w, and the
 * right pedal pair - so a fully pressed pedal lands exactly on the far edge of
 * that window.
 *
 * The wheel has considerably more electrical travel than its window, which is
 * what the steering initialisation screen in the test menu exists to measure:
 * it reports the raw count divided by 64, and a real cabinet reads about +/-520
 * there rather than the +/-384 that m_w alone would give.  So the wheel is
 * handed the whole 16 bit range and the game's own calibration decides where
 * full lock sits.
 */
uint16_t calibratedRaw(float normalized, int rawMin, int rawMax)
{
    rawMin = std::max(0, std::min(rawMin, 65535));
    rawMax = std::max(0, std::min(rawMax, 65535));
    // A reversed or collapsed pair would silently pin the axis, so the ends are
    // put back in order rather than trusted as configured.
    if (rawMax < rawMin)
        std::swap(rawMin, rawMax);

    /*
     * Rounded rather than truncated. A pedal that stops a hair short of 1.0 -
     * which is all it takes for the axis to report 65534 instead of 65535 -
     * otherwise reads one count low across its whole travel, and the test
     * screen shows 319 where the cabinet shows 320.
     */
    const float raw = static_cast<float>(rawMin) +
                      clamp01(normalized) * static_cast<float>(rawMax - rawMin) + 0.5f;
    return static_cast<uint16_t>(std::min(raw, 65535.0f));
}

bool switchActive(const JVSIO *io, JVSPlayer player, JVSInput input)
{
    return io && (io->state.inputSwitch[player] & input) != 0;
}
} // namespace

/*
 * Advances the six position shifter from the sequential GearUp/GearDown
 * bindings and reports the selected gear, or 0 while the four raw shifter
 * switches are being thrown directly.  Both the direct-write path and the JVS
 * bridge drive the same state machine, so a cabinet cannot end up with one
 * gear on the wire and another in the game.
 *
 * GearUp/GearDown live on PLAYER_2 because that is where initJvsMappings()
 * puts them for DRIVING titles.
 */
extern "C" int n2UpdateShifter(void)
{
    JVSIO *io = getJVSIO();
    // Only six shifter positions have switch patterns, so a controls.ini asking
    // for more gears than the cabinet has is capped rather than refused.
    const int topGear = std::max(1, std::min(getShifterGears(), 6));

    const bool gearUp = switchActive(io, PLAYER_2, BUTTON_UP);
    const bool gearDown = switchActive(io, PLAYER_2, BUTTON_DOWN);
    if (gearUp && !previousGearUp && currentGear < topGear)
        ++currentGear;
    if (gearDown && !previousGearDown && currentGear > 0)
        --currentGear;
    previousGearUp = gearUp;
    previousGearDown = gearDown;

    // A directly bound H-pattern position takes priority over the sequential
    // paddles. Releasing the button leaves the selected gear latched, which
    // also makes keyboard bindings practical and avoids a false neutral while
    // a physical shifter moves between gates.
    const int directGear = getWmmtDirectGear();
    if (directGear > 0)
        currentGear = directGear;

    const bool manualShifterSwitch =
        switchActive(io, PLAYER_1, BUTTON_3) || switchActive(io, PLAYER_1, BUTTON_4) ||
        switchActive(io, PLAYER_1, BUTTON_5) || switchActive(io, PLAYER_1, BUTTON_6);
    if (manualShifterSwitch)
        currentGear = 0;

    return currentGear;
}

extern "C" uint16_t n2GearSwitchBits(int gear)
{
    return n2GearSwitches(gear);
}

extern "C" uint16_t n2AnalogueCount(int channel, float normalized)
{
    const EmulatorConfig *config = getConfig();
    switch (channel)
    {
        case N2_ANALOGUE_STEERING:
            return calibratedRaw(normalized, config->namcoN2.steering.minimum,
                                 config->namcoN2.steering.maximum);
        case N2_ANALOGUE_ACCELERATOR:
            return calibratedRaw(normalized, config->namcoN2.accelerator.minimum,
                                 config->namcoN2.accelerator.maximum);
        case N2_ANALOGUE_BRAKE:
            return calibratedRaw(normalized, config->namcoN2.brake.minimum,
                                 config->namcoN2.brake.maximum);
        default: return 0;
    }
}

namespace
{
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

static size_t patchWritableGlEntryPoints()
{
    if (!getSDLWindow())
        startSDL();

    const size_t patched =
        SymbolResolver::GetInstance().PatchNativeJumpStubs("gl", GLHooks_GetProcAddress);
    if (patched)
        log_info("Namco N2: initialized %zu writable OpenGL entry points", patched);
    return patched;
}

AdmWindow *admCreateWindow()
{
    patchWritableGlEntryPoints();
    std::memcpy(admWindow.ident, "WNDW", 4);
    admWindow.window = getSDLWindow();
    return admWindow.window ? &admWindow : nullptr;
}

int admMakeCurrent()
{
    SDL_Window *window = getSDLWindow();
    return window && getSDLContext() && SDL_GL_MakeCurrent(window, getSDLContext()) ? 1 : 0;
}

int admGetDeviceAttribi(int, int attribute, int *value)
{
    if (value)
        *value = 0;
    log_trace("Namco N2: display manager attribute 0x%X answered as zero", attribute);
    return 1;
}

int admSwapBuffers(AdmWindow *)
{
    const SDLFramePresentOptions present = {
        n2GetGameTitle(), true, true, nullptr, nullptr, nullptr, nullptr};
    return presentSDLFrame(&present);
}

//Counter-Strike Neo asks for the swap interval on every present
 
int admSwapInterval(int interval)
{
    static bool haveApplied = false;
    static bool announced = false;
    static int applied = 0;

    if (getConfig()->fpsLimiter && interval != 0)
    {
        if (!announced)
        {
            log_info("Namco N2: vsync suppressed; [Graphics] FPS_TARGET drives the frame rate");
            announced = true;
        }
        interval = 0;
    }

    if (haveApplied && applied == interval)
        return 1;

    haveApplied = true;
    applied = interval;
    return setSDLSwapInterval(interval) ? 1 : 0;
}

using CreateTextureHandle = int (*)(void *, int, int);
using SetTexture = int (*)(void *, int, int);
using SetTextureRegion = int (*)(void *, int, int, int, int, int, int, void *);

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
        static PoolSizeFn getTotalArenaSize = reinterpret_cast<PoolSizeFn>(
            n2ResolveSymbol("_ZNK3Gap4Core17igArenaMemoryPool17getTotalArenaSizeEv"));
        static PoolSizeFn getLargestAllocation = reinterpret_cast<PoolSizeFn>(
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

    static PoolAdaptorFunction systemPoolFunction = reinterpret_cast<PoolAdaptorFunction>(
        n2ResolveSymbol("_ZN3Gap4Core27igMemoryPoolSystem_functionEv"));
    static PoolAdaptorGet getPool = reinterpret_cast<PoolAdaptorGet>(
        n2ResolveSymbol("_ZNK3Gap4Core19igMemoryPoolAdaptorptEv"));

    if (!systemPoolFunction || !getPool)
        return nullptr;

    void *adaptor = systemPoolFunction();
    return adaptor ? getPool(adaptor) : nullptr;
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

bool isLoaderMainThread()
{
    return SDL_IsMainThread();
}

bool dispatchOnLoaderMainThread(SDL_MainThreadCallback callback, void *arguments)
{
    /*
     * SDL owns both the window and the OpenGL context used by the N2 bridge.
     * Keep cross-thread work inside that ownership boundary instead of
     * depending on undocumented game objects or fixed object offsets.
     * pollEvents() in admSwapBuffers() services SDL's main-thread callback
     * queue once per rendered frame.
     */
    return runOnSDLMainThread(callback, arguments, false);
}

struct TextureHandleCall
{
    void *self;
    int width;
    int height;
};

void SDLCALL createTextureHandleOnMain(void *opaque)
{
    TextureHandleCall *call = static_cast<TextureHandleCall *>(opaque);
    originalCreateTextureHandle(call->self, call->width, call->height);
    delete call;
}

int createTextureHandle(void *self, int width, int height)
{
    if (isLoaderMainThread())
        return originalCreateTextureHandle(self, width, height);

    TextureHandleCall *call = new TextureHandleCall{self, width, height};
    if (dispatchOnLoaderMainThread(createTextureHandleOnMain, call))
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

void SDLCALL setTextureOnMain(void *opaque)
{
    SetTextureCall *call = static_cast<SetTextureCall *>(opaque);
    originalSetTexture(call->self, call->texture, call->image);
    delete call;
}

int setTexture(void *self, int texture, int image)
{
    if (isLoaderMainThread())
        return originalSetTexture(self, texture, image);

    SetTextureCall *call = new SetTextureCall{self, texture, image};
    if (dispatchOnLoaderMainThread(setTextureOnMain, call))
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

void SDLCALL setTextureRegionOnMain(void *opaque)
{
    SetTextureRegionCall *call = static_cast<SetTextureRegionCall *>(opaque);
    originalSetTextureRegion(call->self, call->texture, call->x, call->y,
                             call->width, call->height, call->format, call->image);
    delete call;
}

int setTextureRegion(void *self, int texture, int x, int y, int width, int height,
                     int format, void *image)
{
    if (isLoaderMainThread())
        return originalSetTextureRegion(self, texture, x, y, width, height, format, image);

    SetTextureRegionCall *call =
        new SetTextureRegionCall{self, texture, x, y, width, height, format, image};
    if (dispatchOnLoaderMainThread(setTextureRegionOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal partial texture upload to the main thread");
    return originalSetTextureRegion(self, texture, x, y, width, height, format, image);
}
} // namespace

/*
 * Counter-Strike Neo ships a stripped launcher with nine exported symbols, so
 * there is nothing in it to recognise.  What is distinctive is the pairing: the
 * launcher sits next to the GoldSrc engine module that Namco built for the
 * board, and no other title on the loader's roster is laid out that way.
 */
static bool looksLikeCounterStrikeNeo(const char *elfPath)
{
    if (!elfPath || !*elfPath)
        return false;

    std::error_code failure;
    const std::filesystem::path executable(elfPath);
    if (executable.stem().string().rfind("hlds", 0) != 0)
        return false;

    return std::filesystem::exists(executable.parent_path() / "engine_amd.so", failure);
}

extern "C" int n2DetectGame(const char *elfPath)
{
    detectedGame = N2_GAME_NONE;
    detectedRevision.clear();

    RomInfo *romInfo = static_cast<RomInfo *>(n2ResolveSymbol("gRomInfo"));
    void *systemMarker = n2ResolveSymbol("_ZN10clSystemN212initSystemN2Ev");
    if (!romInfo || !systemMarker || !isPrintableString(romInfo->revisionName, sizeof(romInfo->revisionName)))
    {
        if (!looksLikeCounterStrikeNeo(elfPath))
            return 0;

        detectedGame = N2_GAME_CSNEO;
        log_info("Detected Namco System N2 title: Counter-Strike Neo");
        return 1;
    }

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

extern "C" int n2IsWanganTitle(void)
{
    return detectedGame != N2_GAME_NONE && detectedGame != N2_GAME_CSNEO;
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
        case N2_GAME_CSNEO: return "Counter-Strike Neo";
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
        case N2_GAME_CSNEO: return "CSNeo";
        default: return "WMMT3 series";
    }
}

extern "C" const char *n2GetGameId(void)
{
    if (!detectedRevision.empty())
        return detectedRevision.c_str();
    return detectedGame == N2_GAME_CSNEO ? "CSN1" : "NAMCO-N2";
}

extern "C" const char *n2GetRevision(void)
{
    return detectedRevision.c_str();
}

extern "C" int n2InstallAdmHooks(void)
{
    static bool installed = false;
    if (installed)
        return 1;
    if (!n2ResolveSymbol("admInitDevicei"))
        return 0;

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
    n2HookSymbol("admGetDeviceAttribi", reinterpret_cast<void *>(admGetDeviceAttribi));
    n2HookSymbol("admSwapBuffers", reinterpret_cast<void *>(admSwapBuffers));
    n2HookSymbol("admSetMonitorGamma", reinterpret_cast<void *>(returnSuccess));

    if (!n2ArmHooks())
        return 0;

    if (getSDLWindow())
        patchWritableGlEntryPoints();

    installed = true;
    log_info("Namco N2: display manager entry points redirected to the loader's GL context");
    return 1;
}

extern "C" int n2InstallHooks(void)
{
    if (!n2IsDetected())
        return 0;

    if (!n2IsWanganTitle())
    {
        std::error_code failure;
        std::filesystem::create_directories("freespace/contents2", failure);
        if (failure)
            log_warn("Namco N2: could not create freespace/contents2 (%s); the game will "
                     "start with an empty configuration",
                     failure.message().c_str());

        n2InstallAdmHooks();
        log_info("Namco N2 compatibility hooks installed");
        return 0;
    }

    n2HookSymbol("_ZN18clInputDeviceJamma8checkUseEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZN16clInputDevicePad12handleEventsEv", reinterpret_cast<void *>(returnSuccess));

    /*
     * The game runs its own JVS master: clSystemN2::initSystemN2() opens
     * /dev/ttyM3, resets the bus, assigns an address, reads the function list,
     * and n2JvioAckTxVsync() then decodes each frame's reply into n2jvio for
     * clInputDeviceJamma::handleEvents(). The loader only has to answer as the
     * I/O board, which n2Jvio.cpp does on top of the same JVS slave the
     * Lindbergh games use.
     */
    log_info("Namco N2 JVS: answering the game's JVIO master on /dev/ttyM3");

    /*
     * The cabinet checks its steering board before entering attract mode.
     * These used to be stubbed because the board was not emulated at all, but
     * that also left clKickback unopened: it only puts a frame on the wire once
     * its own initialisation has run, so stubbing the check kept /dev/ttyM1
     * silent and the cabinet stuck on PCB ERROR. The real sequence runs now and
     * n2Kickback.cpp answers it.
     */
    n2SteeringIoInstallHooks();

    // Silence is not fatal, missing openal32.dll must not abort startup.
    n2AudioInstallHooks();

    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService16requestGetStatusEv",
                             reinterpret_cast<void *>(getStatusCardDevice),
                             reinterpret_cast<void **>(&originalRequestGetStatus));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService11requestInitEb",
                             reinterpret_cast<void *>(initCardDevice),
                             reinterpret_cast<void **>(&originalRequestInit));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService21requestCheckDispenserEv",
                             reinterpret_cast<void *>(checkDispenserCardDevice),
                             reinterpret_cast<void **>(&originalRequestCheckDispenser));

    /*
     * Start the asynchronous bridge, but do not interpret its initial state as
     * a failed connection. The worker has only just been created here and may
     * not have reached CreateFile yet, even when YaCardEmu already owns the
     * pipe. openCardPipe() logs the authoritative connected/unavailable result
     * after the actual attempt and continues retrying when YaCardEmu starts
     * later.
     */
    (void)n2CardReaderIsConnected();
    log_info("Namco N2 card: connecting /dev/ttyM2 to external YaCardEmu at %s",
             getConfig()->namcoN2.card.pipeName);

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

    n2InstallAdmHooks();

    int textureHooks = 0;
    textureHooks += n2HookSymbolWithOriginal(
        "_ZN24clAlchemyTextureAccessor19createTextureHandleEii",
        reinterpret_cast<void *>(createTextureHandle),
        reinterpret_cast<void **>(&originalCreateTextureHandle));
    textureHooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext10setTextureEii",
        reinterpret_cast<void *>(setTexture),
        reinterpret_cast<void **>(&originalSetTexture));
    textureHooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext16setTextureRegionEiiiiiiPNS0_7igImageE",
        reinterpret_cast<void *>(setTextureRegion),
        reinterpret_cast<void **>(&originalSetTextureRegion));
    if (textureHooks)
        log_info("Namco N2: installed SDL-owned main-thread texture dispatch (%d hooks)",
                 textureHooks);

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

    if (!isValidN2DongleId(getConfig()->namcoN2.dongleId))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId);
    if (!isValidN2DongleId(getConfig()->namcoN2.dongleId2))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID_2 is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId2);

    log_info("Namco N2 compatibility hooks installed");
    return 0;
}

extern "C" int n2InitializeGraphics(void)
{
    if (!n2IsDetected())
        return 0;

    if (patchWritableGlEntryPoints() == 0 && n2IsWanganTitle())
        return 1;
    return 0;
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
            std::ofstream output("tmp/find.txt", std::ios::trunc | std::ios::binary);
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

    if (std::strncmp(command, "perl etc/ifconfig.pl > ", 23) == 0)
    {
        std::error_code directoryError;
        std::filesystem::create_directories("tmp", directoryError);

        /*
         * etc/ifconfig.pl prints "$interface @address @mask @mac $link" - the
         * interface number, then IPv4, netmask and MAC as individual decimal
         * bytes, then the ethtool link flag.  Sixteen numbers in all.
         */
        int interfaceIndex = 0;
        int link = 0;
        unsigned char address[4] = {127, 0, 0, 1};
        unsigned char mask[4] = {255, 0, 0, 0};
        unsigned char mac[6] = {0, 0, 0, 0, 0, 0};

        const bool haveAdapter =
            n2HostNetworkInterface(&interfaceIndex, address, mask, mac, &link) != 0;

        std::ofstream output(redirectTempPath(command + 23), std::ios::trunc | std::ios::binary);
        output << interfaceIndex;
        for (unsigned char value : address)
            output << " " << static_cast<int>(value);
        for (unsigned char value : mask)
            output << " " << static_cast<int>(value);
        for (unsigned char value : mac)
            output << " " << static_cast<int>(value);
        output << " " << link << "\n";

        if (haveAdapter)
            log_info("Namco N2: reported host interface %d.%d.%d.%d (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
                     address[0], address[1], address[2], address[3],
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        else
            log_info("Namco N2: no usable host adapter; reported a loopback interface");
        return output ? 0 : 1;
    }

    if (std::strncmp(command, "sudo perl etc/usbsize.pl >", 26) == 0)
    {
        std::error_code directoryError;
        std::filesystem::create_directories("tmp", directoryError);

        /*
         * etc/usbsize.pl prints the second column of "busybox df", i.e. the
         * work disk's total size in 1K blocks.  The loader keeps that storage
         * next to the game, so report the volume it actually lives on.
         */
        const unsigned long long kilobytes = n2HostWorkDiskKilobytes();
        std::ofstream output(redirectTempPath(command + 26), std::ios::trunc | std::ios::binary);
        output << kilobytes << "\n";
        log_info("Namco N2: reported a work disk of %llu 1K blocks", kilobytes);
        return output ? 0 : 1;
    }

    if (std::strncmp(command, "stty ", 5) == 0)
    {
        log_debug("Namco N2: ignoring a console setting (%s)", command);
        return 0;
    }

    if (std::strncmp(command, "sudo ", 5) == 0)
    {
        const char *privileged = command + 5;

        const char *clock = privileged;
        if (std::strncmp(clock, "busybox ", 8) == 0)
            clock += 8;
        if (std::strncmp(clock, "date ", 5) == 0 || std::strncmp(clock, "hwclock", 7) == 0)
        {
            log_debug("Namco N2: ignoring the cabinet's clock update (%s)", command);
            return 0;
        }

        if (std::strncmp(privileged, "ifconfig ", 9) == 0 ||
            std::strncmp(privileged, "route ", 6) == 0)
        {
            log_info("Namco N2: leaving host networking untouched for: %s", command);
            return 0;
        }

        if (std::strncmp(privileged, "mkdir -p ", 9) == 0)
        {
            std::error_code error;
            std::filesystem::create_directories(redirectTempPath(privileged + 9), error);
            return error ? 1 : 0;
        }

        if (std::strncmp(privileged, "mount ", 6) == 0)
        {
            log_info("Namco N2: no USB medium is emulated; refusing %s", command);
            return 1;
        }
        if (std::strncmp(privileged, "umount ", 7) == 0)
            return 0;
        if (std::strncmp(privileged, "cp ", 3) == 0 || std::strncmp(privileged, "rm ", 3) == 0)
        {
            log_warn("Namco N2: refused a USB transfer with nothing mounted: %s", command);
            return 1;
        }

        log_warn("Namco N2: ignoring an unhandled privileged command: %s", command);
        return 1;
    }

    if (std::strncmp(command, "perl prepend-n2.pl", 18) != 0)
        return -1;

    const char *directories[] = {
        "tmp/data/target",
        // prepend-n2.pl creates target/old alongside target itself.
        "tmp/data/target/old",
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
