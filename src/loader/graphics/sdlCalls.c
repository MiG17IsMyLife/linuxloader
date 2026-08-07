#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#define GL_GLEXT_PROTOTYPES
#include <glad/gl.h>

#ifdef __linux__
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <stdbool.h>
#include <unistd.h>

#include "blitStretching.h"
#include "../config/config.h"
#include "fpsLimiter.h"
#include "../input/sdlInput.h"
#include "../hardware/common/jvs.h"
#include "../hardware/namco/n2/n2.h"
#if defined(_WIN32) || defined(__MINGW32__)
#include "../hardware/common/cardControl.h"
#include "../platform/platformBackend.h"
extern void bridgeX11PumpInput(void);
#endif
#include "../log/log.h"
#include "sdlCalls.h"

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;

#ifdef __linux__
Display *x11Display = NULL;
Window x11Window;
#endif


bool creatingWindow = false;
SDL_Window *g_SdlWindow = NULL;
SDL_GLContext g_SdlContext = NULL;
bool sdlInputInitialized = false;

int glutInitialized = 0;

bool isFullScreen = false;

void GLAPIENTRY openglDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message,
                                    const void *userParam)
{
    if (id == 1099)
        return;
    printf("OpenGL Debug Message:\n");
    printf("Source: 0x%x\n", source);
    printf("Type: 0x%x\n", type);
    printf("ID: %u\n", id);
    printf("Severity: 0x%x\n", severity);
    printf("Message: %s\n", message);

    if (severity == GL_DEBUG_SEVERITY_HIGH)
    {
        printf("This is a high severity error!\n");
    }
}

int initSDL()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD))
    {
        log_error("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    return 0;
}

/*
 * Windows calls a window hung once its owning thread stops taking messages for
 * a few seconds, and replaces it with a grey ghost. Loading a course is exactly
 * that: one long stretch of work with no frame presented.
 *
 * So ghosting is turned off, and the message queue is drained from the loader's
 * file paths, which a load goes through constantly. Only the owning thread may
 * pump, and only every so often, so thousands of reads do not each pay for one.
 */
#define WINDOW_PUMP_INTERVAL_MS 100

static SDL_ThreadID g_windowThread = 0;
static Uint64 g_lastPumpTicks = 0;

static void disableWindowGhosting(void)
{
#if defined(_WIN32) || defined(__MINGW32__)
    /*
     * Loaded through SDL rather than windows.h, which does not sit well with
     * the GL headers this file already pulls in.  user32 is resident for the
     * window's sake anyway, so the handle is simply left open.
     */
    SDL_SharedObject *user32 = SDL_LoadObject("user32.dll");
    if (!user32)
        return;

    void (*disableGhosting)(void) =
        (void (*)(void))SDL_LoadFunction(user32, "DisableProcessWindowsGhosting");
    if (disableGhosting)
        disableGhosting();
#endif
}

void keepWindowResponsive(void)
{
    if (!g_windowThread)
        return;

    const Uint64 now = SDL_GetTicks();
    if (now - g_lastPumpTicks < WINDOW_PUMP_INTERVAL_MS)
        return;

    if (SDL_GetCurrentThreadID() != g_windowThread)
        return;

    g_lastPumpTicks = now;
    /*
     * Pump the native queue without consuming it.  SDL 1.2 games have their
     * own clInputDeviceKey::update() path; draining events here would make the
     * loader see TEST but discard the same key before the guest test menu can
     * see it.  The normal frame path still calls pollEvents() to update JVS
     * action states.
     */
    SDL_PumpEvents();
#if defined(_WIN32) || defined(__MINGW32__)
    if (platformIsES1())
        bridgeX11PumpInput();
#endif
}

void startSDL()
{
    creatingWindow = true;
    int numDisplays;
    SDL_DisplayID *sdlDisplayId = SDL_GetDisplays(&numDisplays);
    if (numDisplays > 1)
        log_warn("More than 1 display detected, will use the first one.\n");

    const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(sdlDisplayId[0]);
    SDL_free(sdlDisplayId);
    if (displayMode == NULL)
    {
        log_error("SDL_GetCurrentDisplayMode Error: %s\n", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    if (getConfig()->showDebugMessages)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); // Double buffering
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);  // 24-bit depth buffer
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);   // Set the alpha size to 8 bits
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    if (gGrp == GROUP_ABC || gGrp == GROUP_VF5 || gId == R_TUNED_SBQW || gId == GHOST_SQUAD_EVOLUTION_SBNJ || gId == SEGA_RACE_TV_SBPF ||
        gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA)
    {
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
    }

    // Use a pixel-dense backbuffer for custom resolutions.
    uint32_t windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;

    g_SdlWindow = SDL_CreateWindow(getGameName(), gWidth, gHeight, windowFlags);

    if (g_SdlWindow)
    {
        // Whoever created the window is the only thread allowed to pump for it.
        g_windowThread = SDL_GetCurrentThreadID();
        disableWindowGhosting();

#ifdef __linux__
        SDL_IOStream *io = SDL_IOFromConstMem(icon256x256, sizeof(icon256x256));
        if (io)
        {
            SDL_Surface *iconSurface = IMG_Load_IO(io, true);
            if (iconSurface)
            {
                SDL_SetWindowIcon(g_SdlWindow, iconSurface);
                SDL_DestroySurface(iconSurface);
            }
        }
#endif
    }

    // Hacky way to make AxA games render the characters properly
    if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
        SDL_SetWindowSize(g_SdlWindow, 1024, 768);

    if (!g_SdlWindow)
    {
        SDL_Quit();
        log_error("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ || gId == MJ4_EVO_SBTA || gId == MJ4_SBPN_REVG)
        SDL_SetWindowSize(g_SdlWindow, gWidth, gHeight + 1);

#ifdef __linux__
    x11Display = (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(g_SdlWindow), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    x11Window = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(g_SdlWindow), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!x11Display || !x11Window)
    {
        log_error("This program is not running on X11 or failed to get window/display.\n");
    }
#endif

    g_SdlContext = SDL_GL_CreateContext(g_SdlWindow);
    if (!g_SdlContext)
    {
        SDL_DestroyWindow(g_SdlWindow);
        SDL_Quit();
        log_error("OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    /* The QPC limiter below is the presentation clock when enabled. Do not
     * let SDL's swap interval add a second, refresh-driven wait. */
    if (getConfig()->fpsLimiter && !SDL_GL_SetSwapInterval(0))
        log_warn("Failed to disable OpenGL VSync while FPS limiter is enabled: %s", SDL_GetError());

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        log_error("Failed to initialize GLAD.\n");
        exit(EXIT_FAILURE);
    }

    if (getConfig()->showDebugMessages)
    {
        // Enable OpenGL debug output
        glad_glEnable(GL_DEBUG_OUTPUT);
        glad_glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glad_glDebugMessageCallback(openglDebugCallback, NULL);
    }

    // If games are any of the LGJ or Primeval Hunt, we prevent the window to resize.
    if ((gGrp == GROUP_LGJ || gId == PRIMEVAL_HUNT_SBPP) && !getConfig()->fullscreen)
        SDL_SetWindowMaximumSize(g_SdlWindow, gWidth, gHeight);
    else if (gGrp != GROUP_LGJ)
        SDL_SetWindowMaximumSize(g_SdlWindow, displayMode->w, displayMode->h);

    SDL_SetWindowMinimumSize(g_SdlWindow, gWidth, gHeight);

    // Hacky way to make AxA games render the characters properly
    if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
        SDL_SetWindowPosition(g_SdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    if (getConfig()->fullscreen)
    {
        SDL_SetWindowFullscreenMode(g_SdlWindow, NULL);
        SDL_SetWindowFullscreen(g_SdlWindow, true);
        isFullScreen = true;
    }

    initBlitting();

    if (gId != PRIMEVAL_HUNT_SBPP && gGrp != GROUP_LGJ)
        SDL_SetWindowResizable(g_SdlWindow, true);

    SDL_ShowWindow(g_SdlWindow);

    Uint64 startTime = SDL_GetTicks();
    int running = 1;

    // We clear the window background
    while (running)
    {
        if (SDL_GetTicks() - startTime >= 1000)
            running = 0;
        glad_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glad_glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(g_SdlWindow);
    }

    creatingWindow = false;

    printf("  RESOLUTION: %dx%d\n", gWidth, gHeight);

    if (getConfig()->hideCursor)
        SDL_HideCursor();
}

SDL_Window *getSDLWindow()
{
    return g_SdlWindow;
}

SDL_GLContext getSDLContext()
{
    return g_SdlContext;
}

bool makeSDLCurrent(SDL_Window *win, SDL_GLContext ctx)
{
    return SDL_GL_MakeCurrent(win, ctx);
}

bool setSDLSwapInterval(int interval)
{
    return SDL_GL_SetSwapInterval(interval);
}

bool runOnSDLMainThread(SDL_MainThreadCallback callback, void *userdata, bool waitComplete)
{
    if (!callback)
        return false;
    return SDL_RunOnMainThread(callback, userdata, waitComplete);
}

int presentSDLFrame(const SDLFramePresentOptions *options)
{
    if (!options)
        return 0;


    if (options->beforeEvents)
        options->beforeEvents(options->userdata);

    if (options->processEvents)
        pollEvents();

    if (options->beforeSwap)
        options->beforeSwap(options->userdata);

    SDL_Window *window = getSDLWindow();
    if (!window)
        return 0;

    // Present only completed WMMT frames.
    if (gGrp == GROUP_WMMT3 && n2WmmtShouldBlit())
        blitStretch();

    if (getConfig()->fpsLimiter)
        frameTiming();

    SDL_GL_SwapWindow(window);

    if (options->title)
        showFpsInWindowTitle(options->title);

    if (options->afterPresent)
        options->afterPresent(options->userdata);

    return 1;
}

void sdlQuit()
{
    setSwitch(SYSTEM, BUTTON_TEST, 1);
    usleep(50000);
    setSwitch(SYSTEM, BUTTON_TEST, 0);
    usleep(50000);

    if (g_SdlContext)
    {
        SDL_GL_DestroyContext(g_SdlContext);
        g_SdlContext = NULL;
    }

    if (g_SdlWindow)
    {
        SDL_DestroyWindow(g_SdlWindow);
        g_SdlWindow = NULL;
    }
    SDL_Quit();
    exit(0);
}

void pollEvents()
{
    SDL_Event event;
    g_lastPumpTicks = SDL_GetTicks();
    while (SDL_PollEvent(&event))
    {
#ifdef __linux__
        if (event.type == SDL_WIIMOTION_EVENT)
            processSdlEvent(&event);
#endif
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            {
#if defined(_WIN32) || defined(__MINGW32__)
                if (!event.key.repeat)
                    platformHandleHostKeyEvent((int)event.key.key,
                                               (uint32_t)event.key.mod, 1);
#endif
                if (gGrp != GROUP_LGJ && gId != PRIMEVAL_HUNT_SBPP)
                {
                    SDL_Keymod mod = SDL_GetModState();
                    if ((event.key.key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) || event.key.key == SDLK_F11)
                    {
                        if (isFullScreen)
                        {
                            if (!SDL_SetWindowFullscreen(g_SdlWindow, 0))
                            {
                                log_error("Error setting windowed mode: %s\n", SDL_GetError());
                            }
                            else
                            {
                                if ((long long)gWidth * 3 == (long long)gHeight * 4)
                                    SDL_SetWindowSize(g_SdlWindow, gWidth + 1, gHeight);
                                else
                                    SDL_SetWindowSize(g_SdlWindow, gWidth, gHeight + 1);
                                isFullScreen = false;
                                if (getConfig()->fullscreen)
                                    SDL_SetWindowBordered(g_SdlWindow, true);
                            }
                        }
                        else
                        {
                            if (!SDL_SetWindowFullscreen(g_SdlWindow, true))
                            {
                                log_error("Error setting fullscreen mode: %s\n", SDL_GetError());
                            }
                            else
                            {
                                isFullScreen = true;
                            }
                        }
                        break;
                    }
                }
            }
            case SDL_EVENT_KEY_UP:
#if defined(_WIN32) || defined(__MINGW32__)
                platformHandleHostKeyEvent((int)event.key.key,
                                           (uint32_t)event.key.mod, 0);
#endif
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                processSdlEvent(&event);
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                sdlQuit();
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            {
                if (((long long)gWidth * 3 == (long long)gHeight * 4) || gGrp == GROUP_HUMMER)
                    SDL_SetWindowSize(g_SdlWindow, gWidth + 1, gHeight);
                else
                    SDL_SetWindowSize(g_SdlWindow, gWidth, gHeight + 1);
            }
            break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                processSdlEvent(&event);
            default:
                break;
        }
    }
    if (sdlInputInitialized)
    {
        if (gGrp == GROUP_HOD4 || gGrp == GROUP_HOD4_TEST)
            updateGunShake();
        updateCombinedAxes();
        processChangedActions();
    }
}

void showFpsInWindowTitle(const char *name)
{
    static int framesSinceTitle = 0;
    char windowTitle[160];
    SDL_Window *window = getSDLWindow();

    /* Counts the frame it is called on, so it runs every present. */
    double fps = calculateFps();

    if (!window || ++framesSinceTitle < 30)
        return;

    framesSinceTitle = 0;
    if (gGrp == GROUP_WMMT3)
    {
#if defined(_WIN32) || defined(__MINGW32__)
        snprintf(windowTitle, sizeof(windowTitle), "%s - FPS: %.2f - Card: %s",
                 name && *name ? name : "linuxloader", fps,
                 cardControlGetConnectionText());
#else
        snprintf(windowTitle, sizeof(windowTitle), "%s - FPS: %.2f",
                 name && *name ? name : "linuxloader", fps);
#endif
    }
    else
        snprintf(windowTitle, sizeof(windowTitle), "%s - FPS: %.2f",
                 name && *name ? name : "linuxloader", fps);
    SDL_SetWindowTitle(window, windowTitle);
}
