#if defined(_WIN32) || defined(__MINGW32__)

#include "x11Bridge.hpp"

#include "../config/config.h"
#include "../graphics/sdlCalls.h"
#include "../log/log.h"
#include "symbolResolver.hpp"

#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>

namespace
{
struct DisplayStub
{
    unsigned char reserved[0x84];
    int defaultScreen;
    unsigned char reserved2[4];
    void *screens;
};

struct ScreenStub
{
    unsigned long root;
    unsigned long display;
    int width;
    int height;
    int widthMm;
    int heightMm;
    int rootDepth;
    void *rootVisual;
};

ScreenStub g_screen{1, 0, 1360, 768, 340, 190, 24, reinterpret_cast<void *>(1)};
DisplayStub g_display{{}, 0, {}, &g_screen};
unsigned char g_event[256]{};

unsigned long dummyWindow()
{
    return 1;
}

extern "C" void *bridgeXOpenDisplay(const char *name)
{
    log_debug("XOpenDisplay(\"%s\")", name ? name : "NULL");
    g_screen.width = getConfig()->width;
    g_screen.height = getConfig()->height;
    g_screen.display = reinterpret_cast<unsigned long>(&g_display);
    g_display.defaultScreen = 0;
    g_display.screens = &g_screen;
    return &g_display;
}

extern "C" int bridgeXCloseDisplay(void *display)
{
    (void)display;
    return 0;
}

extern "C" unsigned long bridgeXCreateWindow(void *display, ...)
{
    (void)display;
    if (!getSDLWindow())
        startSDL();
    return dummyWindow();
}

extern "C" int bridgeXDestroyWindow(void *display, unsigned long window)
{
    (void)display;
    (void)window;
    return 0;
}

extern "C" int bridgeXCreateColormap(void *display, ...)
{
    (void)display;
    return 1;
}

extern "C" void *bridgeXCreateGC(void *display, ...)
{
    (void)display;
    return &g_display;
}

extern "C" void *bridgeXCreateImage(void *display, ...)
{
    (void)display;
    return g_event;
}

extern "C" unsigned long bridgeXCreatePixmap(void *display, ...)
{
    (void)display;
    return 1;
}

extern "C" unsigned long bridgeXCreatePixmapCursor(void *display, ...)
{
    (void)display;
    return 1;
}

extern "C" unsigned long bridgeXDefineCursor(void *display, ...)
{
    (void)display;
    return 1;
}

extern "C" int bridgeXFree(void *value)
{
    (void)value;
    return 0;
}

extern "C" int bridgeXFreeCursor(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXFreeGC(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXFreePixmap(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXGetScreenSaver(void *display, int *timeout, int *interval,
                                      int *preferBlanking, int *allowExposures)
{
    (void)display;
    if (timeout) *timeout = 0;
    if (interval) *interval = 0;
    if (preferBlanking) *preferBlanking = 0;
    if (allowExposures) *allowExposures = 1;
    return 1;
}

extern "C" int bridgeXGrabKeyboard(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" unsigned long bridgeXKeycodeToKeysym(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXMapWindow(void *display, unsigned long window)
{
    (void)display;
    (void)window;
    return 0;
}

extern "C" int bridgeXMoveResizeWindow(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXNextEvent(void *display, void *event)
{
    (void)display;
    if (event)
        std::memset(event, 0, 256);
    return 0;
}

extern "C" int bridgeXPending(void *display)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXPutImage(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXRaiseWindow(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXReparentWindow(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXSetScreenSaver(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXSetWindowBackground(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXSetWMNormalHints(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" unsigned long bridgeXStringToKeysym(const char *name)
{
    (void)name;
    return 0;
}

extern "C" int bridgeXSync(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXUndefineCursor(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXUnmapWindow(void *display, ...)
{
    (void)display;
    return 0;
}

extern "C" int bridgeXFlush(void *display)
{
    (void)display;
    return 0;
}

extern "C" int bridgeDPMSQueryExtension(void *display, int *eventBase, int *errorBase)
{
    (void)display;
    if (eventBase) *eventBase = 0;
    if (errorBase) *errorBase = 0;
    return 1;
}

extern "C" int bridgeDPMSDisable(void *display)
{
    (void)display;
    return 1;
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace X11Bridge
{
void initBridges()
{
    map("DPMSDisable", bridgeDPMSDisable);
    map("DPMSQueryExtension", bridgeDPMSQueryExtension);
    map("XAllocSizeHints", +[]() -> void * { return std::calloc(1, 256); });
    map("XClearWindow", bridgeXFlush);
    map("XCloseDisplay", bridgeXCloseDisplay);
    map("XCreateColormap", bridgeXCreateColormap);
    map("XCreateGC", bridgeXCreateGC);
    map("XCreateImage", bridgeXCreateImage);
    map("XCreatePixmap", bridgeXCreatePixmap);
    map("XCreatePixmapCursor", bridgeXCreatePixmapCursor);
    map("XCreateWindow", bridgeXCreateWindow);
    map("XDefineCursor", bridgeXDefineCursor);
    map("XDestroyWindow", bridgeXDestroyWindow);
    map("XFlush", bridgeXFlush);
    map("XFree", bridgeXFree);
    map("XFreeCursor", bridgeXFreeCursor);
    map("XFreeGC", bridgeXFreeGC);
    map("XFreePixmap", bridgeXFreePixmap);
    map("XGetScreenSaver", bridgeXGetScreenSaver);
    map("XGrabKeyboard", bridgeXGrabKeyboard);
    map("XKeycodeToKeysym", bridgeXKeycodeToKeysym);
    map("XMapWindow", bridgeXMapWindow);
    map("XMoveResizeWindow", bridgeXMoveResizeWindow);
    map("XNextEvent", bridgeXNextEvent);
    map("XOpenDisplay", bridgeXOpenDisplay);
    map("XPending", bridgeXPending);
    map("XPutImage", bridgeXPutImage);
    map("XRaiseWindow", bridgeXRaiseWindow);
    map("XReparentWindow", bridgeXReparentWindow);
    map("XSetScreenSaver", bridgeXSetScreenSaver);
    map("XSetWindowBackground", bridgeXSetWindowBackground);
    map("XSetWMNormalHints", bridgeXSetWMNormalHints);
    map("XStringToKeysym", bridgeXStringToKeysym);
    map("XSync", bridgeXSync);
    map("XUndefineCursor", bridgeXUndefineCursor);
    map("XUnmapWindow", bridgeXUnmapWindow);
    log_info("Initialized X11 compatibility bridges");
}
}

#endif
