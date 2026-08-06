#include "config/config.h"

#include "graphics/fpsLimiter.h"
#include "graphics/gpuVendor.h"
#include "graphics/sdlCalls.h"
#include "frontend/frontendApi.h"
#include "hardware/common/jvs.h"
#include "hardware/ffb/sdlFfbBackend.h"
#include "platform/platformBackend.h"
#include "input/sdlInput.h"
#include "log/log.h"
#include "mainShared.h"
#include "../minhook/include/MinHook.h"

#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

uint32_t gId = 0;
int gGrp = -1;
int gWidth = 0;
int gHeight = 0;

extern SDLControllers sdlJoysticks;
extern void pacGraphicsReportCapabilities(void);

static UINT originalConsoleCodePage = 0;

static void restoreConsoleCodePage(void)
{
    if (originalConsoleCodePage)
        SetConsoleOutputCP(originalConsoleCodePage);
}

static void applyConsoleCodePage(void)
{
    if (gGrp != GROUP_WMMT3)
        return;

    const UINT current = GetConsoleOutputCP();
    if (current == 0 || current == CP_UTF8)
        return;

    if (!SetConsoleOutputCP(CP_UTF8))
    {
        log_warn("Could not switch the console to UTF-8; this game's Japanese output will be garbled");
        return;
    }

    originalConsoleCodePage = current;
    atexit(restoreConsoleCodePage);
    log_info("Console switched to UTF-8 for this game's output, was codepage %u", current);
}

void initMain(char *configPath, char *controlsPath)
{
    initConfig(configPath);

    gId = getConfig()->crc32;
    gGrp = getConfig()->gameGroup;
    gWidth = getConfig()->width;
    gHeight = getConfig()->height;

    if (!platformIsDetected())
    {
        log_fatal("pacloader could not identify a supported Namco platform");
        exit(1);
    }

    platformRegisterCardControl();
    applyConsoleCodePage();
    initFpsLimiter();

    getConfig()->GPUVendor = getGPUVendorID();
    if (getConfig()->GPUVendor == ERROR_GPU)
    {
        log_error("Failed to detect GPU; using generic OpenGL behavior");
        getConfig()->GPUVendor = UNKNOWN_GPU;
    }

    if (MH_Initialize() != MH_OK)
    {
        log_fatal("Failed to initialize MinHook");
        exit(1);
    }

    if (platformInstallHooks() != 0)
        exit(1);

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log_fatal("Failed to enable hooks");
        exit(1);
    }

    if (initSDL() != 0)
        exit(1);
    if (platformInitializeGraphics() != 0)
        exit(1);
    pacGraphicsReportCapabilities();
    if (initJVS() != 0)
        exit(1);
    if (initSdlInput(controlsPath) != 0)
        exit(1);

    /*
     * Give the wheel back on the way out. Nothing was releasing the haptic
     * effects, so whatever was last applied stayed loaded in the device's own
     * firmware after the loader had gone - the wheel came up stiff and stayed
     * that way until it was power cycled.
     */
    if (platformIsN2())
        atexit(sdlFfbShutdown);

    printf("\npacloader\n\n");
    printf("  GAME:        %s\n", getGameName());
    printf("  PLATFORM:    %s\n", platformName());
    printf("  GAME ID:     %s\n", getGameId());
    printf("  DVP:         %s\n", getDvpName());
    printf("  GPU VENDOR:  %s\n", getConfig()->GPUVendorString);

    for (int i = 0; i < MAX_JOYSTICKS; ++i)
    {
        SDL_Joystick *joystick = NULL;
        if (sdlJoysticks.controllers[i])
            joystick = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joystick = sdlJoysticks.joysticks[i];

        if (joystick)
            printf("  SDL CONTROLLER %d: %s\n", i, SDL_GetJoystickName(joystick));
    }
    printf("\n");
    pacFrontendReport("running", getGameName());
}
