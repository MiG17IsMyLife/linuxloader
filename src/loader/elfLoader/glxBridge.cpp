#if defined(_WIN32) || defined(__MINGW32__)

#include "glxBridge.hpp"

#include "../config/config.h"
#include "../graphics/fpsLimiter.h"
#include "../graphics/sdlCalls.h"
#include "../log/log.h"
#include "symbolResolver.hpp"
#include "glHooks.hpp"

#include <SDL3/SDL.h>
#include <cstring>

namespace
{
struct VisualInfo
{
    void *visual;
    unsigned long visualid;
    int screen;
    int depth;
    int classType;
    unsigned long redMask;
    unsigned long greenMask;
    unsigned long blueMask;
    int colormapSize;
    int bitsPerRgb;
};

VisualInfo g_visual{reinterpret_cast<void *>(1), 1, 0, 24, 4,
                    0x00ff0000, 0x0000ff00, 0x000000ff, 256, 8};

extern "C" void *bridgeGlxChooseVisual(void *display, int screen, int *attributes)
{
    (void)display;
    (void)screen;
    (void)attributes;
    log_debug("ES1 GLX: glXChooseVisual");
    return &g_visual;
}

extern "C" void *bridgeGlxCreateContext(void *display, void *visual, void *share,
                                         int direct)
{
    (void)display;
    (void)visual;
    (void)share;
    (void)direct;
    log_debug("ES1 GLX: glXCreateContext");
    if (!getSDLWindow())
        startSDL();
    return getSDLContext();
}

extern "C" void bridgeGlxDestroyContext(void *display, void *context)
{
    (void)display;
    (void)context;
    /* SDL owns the process-wide context; the guest must not destroy it. */
}

extern "C" int bridgeGlxMakeCurrent(void *display, unsigned long drawable,
                                     void *context)
{
    (void)display;
    log_debug("ES1 GLX: glXMakeCurrent drawable=%lu context=%p", drawable, context);
    bool success = false;
    if (drawable == 0)
        success = makeSDLCurrent(nullptr, nullptr);
    else
        success = makeSDLCurrent(getSDLWindow(), static_cast<SDL_GLContext>(context));
    return success ? 1 : 0;
}

extern "C" void bridgeGlxSwapBuffers(void *display, unsigned long drawable)
{
    (void)display;
    log_debug("ES1 GLX: glXSwapBuffers drawable=%lu", drawable);
    if (getSDLWindow())
    {
        pollEvents();
        if (getConfig()->fpsLimiter)
            frameTiming();
        SDL_GL_SwapWindow(getSDLWindow());
    }
}

extern "C" int bridgeGlxSwapIntervalSGI(int interval)
{
    /* System ES1 requests this legacy GLX entry point during X-system
     * initialization. When the loader limiter is enabled, disable vsync so
     * [Graphics] FPS_TARGET remains the single presentation-rate control. */
    if (getConfig()->fpsLimiter && interval != 0)
        interval = 0;

    return setSDLSwapInterval(interval) ? 0 : 1;
}

extern "C" void *bridgeGlxGetProcAddress(const char *name)
{
    if (!name)
        return nullptr;
    if (std::strcmp(name, "glXSwapIntervalSGI") == 0)
        return reinterpret_cast<void *>(bridgeGlxSwapIntervalSGI);
    return GLHooks_GetProcAddress(name);
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace GlxBridge
{
void initBridges()
{
    map("glXChooseVisual", bridgeGlxChooseVisual);
    map("glXCreateContext", bridgeGlxCreateContext);
    map("glXDestroyContext", bridgeGlxDestroyContext);
    map("glXMakeCurrent", bridgeGlxMakeCurrent);
    map("glXSwapBuffers", bridgeGlxSwapBuffers);
    map("glXSwapIntervalSGI", bridgeGlxSwapIntervalSGI);
    map("glXGetProcAddress", bridgeGlxGetProcAddress);
    map("glXGetProcAddressARB", bridgeGlxGetProcAddress);
    log_info("Initialized GLX compatibility bridges");
}
}

#endif
