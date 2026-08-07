#if defined(_WIN32) || defined(__MINGW32__)

#include "x11Bridge.hpp"

#include "../config/config.h"
#include "../graphics/sdlCalls.h"
#include "../input/sdlInput.h"
#include "../log/log.h"
#include "../platform/platformBackend.h"
#include "symbolResolver.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstring>
#include <deque>
#include <mutex>
#include <type_traits>

namespace
{
/*
 * These are not native C++ representations.  The guest is a 32-bit Xlib
 * client, while the loader is normally a 64-bit process.  Using ordinary
 * structs here lets the host compiler insert 64-bit pointer alignment gaps;
 * the guest then reads the Display/Screen fields at the wrong offsets.  Keep
 * the byte layout used by Xlib (and by KittenTools) explicitly instead.
 *
 * Important fields:
 *   Display.default_screen: 0x84
 *   Display.screens:        0x8c
 *   Screen.root:            0x00
 *   Screen.width:           0x0c
 *   Screen.height:          0x10
 */
std::array<unsigned char, 0x400> g_screen{};
std::array<unsigned char, 0x1000> g_display{};
unsigned char g_event[256]{};

template <typename T>
void writeX11Field(std::array<unsigned char, 0x400> &storage, size_t offset, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(storage.data() + offset, &value, sizeof(value));
}

template <typename T>
void writeX11DisplayField(size_t offset, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(g_display.data() + offset, &value, sizeof(value));
}

/*
 * Maximum Heat 3D does not use the SDL 1.2 event entry point for its cabinet
 * input.  Its clInputDeviceKey::update() calls the game's X11 PollEvent(),
 * which in turn calls XPending/XNextEvent and translates the keycode with
 * XKeycodeToKeysym().  The Windows X11 compatibility layer used to return no
 * events, so keyboard input could never reach the guest input bitfield.
 *
 * Keep a small X11-shaped queue fed from SDL.  The event layout below only
 * contains the fields consumed by the guest's PollEvent implementation:
 * offset 8 is the X event type and offset 0x3c is the keycode passed to
 * XKeycodeToKeysym().  The actual X11 ABI layout is otherwise irrelevant to
 * this compatibility path.
 */
struct PendingKeyEvent
{
    int type;
    unsigned long keysym;
};

std::deque<PendingKeyEvent> g_pendingKeyEvents;
std::mutex g_eventMutex;
unsigned long g_lastKeysym = 0;

unsigned long x11KeysymFromSdl(SDL_Keycode key)
{
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';

    if (key > 0 && key <= 0x7f)
        return static_cast<unsigned long>(key);

    switch (key)
    {
        case SDLK_LEFT:  return 0xff51;
        case SDLK_UP:    return 0xff52;
        case SDLK_RIGHT: return 0xff53;
        case SDLK_DOWN:  return 0xff54;
        case SDLK_F1:    return 0xffbe;
        case SDLK_F2:    return 0xffbf;
        case SDLK_F3:    return 0xffc0;
        case SDLK_F4:    return 0xffc1;
        case SDLK_F5:    return 0xffc2;
        case SDLK_F6:    return 0xffc3;
        case SDLK_F7:    return 0xffc4;
        case SDLK_F8:    return 0xffc5;
        case SDLK_F9:    return 0xffc6;
        case SDLK_F10:   return 0xffc7;
        case SDLK_F11:   return 0xffc8;
        case SDLK_F12:   return 0xffc9;
        default:         return 0;
    }
}

void updateLoaderInput(const SDL_Event &event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
        return;

    /* Keep the normal Pacloader/JVS mapping in sync even though the guest
     * consumes this event through X11 rather than SDL_PollEvent(). */
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        platformHandleHostKeyEvent(static_cast<int>(event.key.key),
                                   static_cast<uint32_t>(event.key.mod),
                                   event.type == SDL_EVENT_KEY_DOWN ? 1 : 0);

    processSdlEvent(&event);
}

void pumpX11KeyboardEvents()
{
    SDL_PumpEvents();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            updateLoaderInput(event);
            const unsigned long keysym = x11KeysymFromSdl(event.key.key);
            if (keysym != 0)
            {
                g_pendingKeyEvents.push_back({
                    event.type == SDL_EVENT_KEY_DOWN ? 2 : 3, keysym});
            }
        }
        break;

        /*
         * This pump drains the whole SDL queue, so anything it does not hand
         * to the input layer is discarded rather than left for another poll.
         * The wheel, pedals and panel buttons all arrive as these events, and
         * dropping them left a guest that only ever saw the keyboard.
         */
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
            updateLoaderInput(event);
            break;

        case SDL_EVENT_QUIT:
            /* Escape remains the guest's clean exit path; do not discard
             * unrelated window-close input while the X11 queue is active. */
            log_debug("ES1 X11: ignoring host quit event until guest exits");
            break;

        default:
            break;
        }
    }

    /* Combined half-axis pairs resolve once the whole queue has been read. */
    updateCombinedAxes();
    processChangedActions();
}

/* Also used by the long-running loader-side responsiveness pump. */
extern "C" void bridgeX11PumpInput(void)
{
    std::lock_guard<std::mutex> lock(g_eventMutex);
    pumpX11KeyboardEvents();
}

unsigned long dummyWindow()
{
    return 1;
}

extern "C" void *bridgeXOpenDisplay(const char *name)
{
    log_debug("ES1 X11: XOpenDisplay(\"%s\")", name ? name : "NULL");
    g_screen.fill(0);
    g_display.fill(0);

    const uint32_t screenRoot = 1;
    const int screenWidth = getConfig()->width;
    const int screenHeight = getConfig()->height;
    writeX11Field(g_screen, 0x00, screenRoot);
    writeX11Field(g_screen, 0x0c, screenWidth);
    writeX11Field(g_screen, 0x10, screenHeight);
    writeX11Field(g_screen, 0x14, 340);
    writeX11Field(g_screen, 0x18, 190);
    writeX11Field(g_screen, 0x1c, 24);

    const int defaultScreen = 0;
    writeX11DisplayField(0x84, defaultScreen);
    writeX11DisplayField(0x8c, g_screen.data());
    return g_display.data();
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
    return g_display.data();
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
    return g_lastKeysym;
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
    std::lock_guard<std::mutex> lock(g_eventMutex);
    if (g_pendingKeyEvents.empty())
        pumpX11KeyboardEvents();
    if (g_pendingKeyEvents.empty())
        return 0;

    const PendingKeyEvent pending = g_pendingKeyEvents.front();
    g_pendingKeyEvents.pop_front();
    g_lastKeysym = pending.keysym;

    if (event)
    {
        std::memset(event, 0, 256);
        static_cast<unsigned char *>(event)[8] = static_cast<unsigned char>(pending.type);
        std::memcpy(static_cast<unsigned char *>(event) + 0x3c,
                    &pending.keysym, sizeof(pending.keysym));
    }
    return 0;
}

extern "C" int bridgeXPending(void *display)
{
    (void)display;
    std::lock_guard<std::mutex> lock(g_eventMutex);
    if (g_pendingKeyEvents.empty())
        pumpX11KeyboardEvents();
    return static_cast<int>(g_pendingKeyEvents.size());
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
