#if defined(_WIN32) || defined(__MINGW32__)

#include "glxBridge.hpp"

#include "../graphics/sdlCalls.h"
#include "../log/log.h"
#include "symbolResolver.hpp"

#include <SDL3/SDL.h>

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
    return &g_visual;
}

extern "C" void *bridgeGlxCreateContext(void *display, void *visual, void *share,
                                         int direct)
{
    (void)display;
    (void)visual;
    (void)share;
    (void)direct;
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
    if (drawable == 0)
        return makeSDLCurrent(nullptr, nullptr) ? 1 : 0;
    return makeSDLCurrent(getSDLWindow(), static_cast<SDL_GLContext>(context)) ? 1 : 0;
}

extern "C" void bridgeGlxSwapBuffers(void *display, unsigned long drawable)
{
    (void)display;
    (void)drawable;
    if (getSDLWindow())
        SDL_GL_SwapWindow(getSDLWindow());
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
    log_info("Initialized GLX compatibility bridges");
}
}

#endif
