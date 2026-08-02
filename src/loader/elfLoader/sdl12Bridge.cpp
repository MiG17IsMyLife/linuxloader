#if defined(_WIN32) || defined(__MINGW32__)

#include "sdl12Bridge.hpp"
#include "symbolResolver.hpp"
#include "../config/config.h"
#include "../graphics/sdlCalls.h"
#include "../hardware/namco/n2/n2.h"
#include "../log/log.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <deque>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace
{
// SDL 1.2 SDL_Init flags.
constexpr uint32_t sdl12InitAudio = 0x00000010;
constexpr uint32_t sdl12InitVideo = 0x00000020;

// SDL 1.2 event types the bridge can produce.
constexpr uint8_t sdl12KeyDown = 2;
constexpr uint8_t sdl12KeyUp = 3;
constexpr uint8_t sdl12MouseMotion = 4;
constexpr uint8_t sdl12MouseButtonDown = 5;
constexpr uint8_t sdl12MouseButtonUp = 6;
constexpr uint8_t sdl12Quit = 12;

constexpr uint8_t sdl12Released = 0;
constexpr uint8_t sdl12Pressed = 1;

// SDL 1.2's SDL_eventaction values, as SDL_EventState() takes them.
constexpr int sdl12Query = -1;
constexpr int sdl12Ignore = 0;
constexpr int sdl12Enable = 1;

/*
 * SDL 1.2 numbered its keys itself; SDL3 gives everything outside 7-bit ASCII a
 * scancode-derived value with bit 30 set.  The printable range agrees between
 * the two - both are just the character - so only the rest needs a table.
 */
struct Sdl12KeyMapping
{
    SDL_Keycode host;
    int guest;
};

const Sdl12KeyMapping g_keyMappings[] = {
    {SDLK_KP_0, 256},        {SDLK_KP_1, 257},         {SDLK_KP_2, 258},
    {SDLK_KP_3, 259},        {SDLK_KP_4, 260},         {SDLK_KP_5, 261},
    {SDLK_KP_6, 262},        {SDLK_KP_7, 263},         {SDLK_KP_8, 264},
    {SDLK_KP_9, 265},        {SDLK_KP_PERIOD, 266},    {SDLK_KP_DIVIDE, 267},
    {SDLK_KP_MULTIPLY, 268}, {SDLK_KP_MINUS, 269},     {SDLK_KP_PLUS, 270},
    {SDLK_KP_ENTER, 271},    {SDLK_KP_EQUALS, 272},

    {SDLK_UP, 273},          {SDLK_DOWN, 274},         {SDLK_RIGHT, 275},
    {SDLK_LEFT, 276},        {SDLK_INSERT, 277},       {SDLK_HOME, 278},
    {SDLK_END, 279},         {SDLK_PAGEUP, 280},       {SDLK_PAGEDOWN, 281},

    {SDLK_F1, 282},          {SDLK_F2, 283},           {SDLK_F3, 284},
    {SDLK_F4, 285},          {SDLK_F5, 286},           {SDLK_F6, 287},
    {SDLK_F7, 288},          {SDLK_F8, 289},           {SDLK_F9, 290},
    {SDLK_F10, 291},         {SDLK_F11, 292},          {SDLK_F12, 293},
    {SDLK_F13, 294},         {SDLK_F14, 295},          {SDLK_F15, 296},

    {SDLK_NUMLOCKCLEAR, 300}, {SDLK_CAPSLOCK, 301},    {SDLK_SCROLLLOCK, 302},
    {SDLK_RSHIFT, 303},      {SDLK_LSHIFT, 304},       {SDLK_RCTRL, 305},
    {SDLK_LCTRL, 306},       {SDLK_RALT, 307},         {SDLK_LALT, 308},
    {SDLK_RGUI, 309},        {SDLK_LGUI, 310},         {SDLK_MODE, 313},
    {SDLK_HELP, 315},        {SDLK_PRINTSCREEN, 316},  {SDLK_SYSREQ, 317},
    {SDLK_PAUSE, 318},       {SDLK_MENU, 319},         {SDLK_POWER, 320},
};

int sdl12KeyFromHost(SDL_Keycode key)
{
    // Everything a 1.2 program calls printable is its own character on both
    // sides, including backspace, tab, return, escape, space and delete.
    if (key > 0 && key <= 0x7F)
        return static_cast<int>(key);

    for (const Sdl12KeyMapping &mapping : g_keyMappings)
    {
        if (mapping.host == key)
            return mapping.guest;
    }
    return 0; // SDLK_UNKNOWN
}

int sdl12ModFromHost(SDL_Keymod mod)
{
    int result = 0;
    if (mod & SDL_KMOD_LSHIFT) result |= 0x0001;
    if (mod & SDL_KMOD_RSHIFT) result |= 0x0002;
    if (mod & SDL_KMOD_LCTRL)  result |= 0x0040;
    if (mod & SDL_KMOD_RCTRL)  result |= 0x0080;
    if (mod & SDL_KMOD_LALT)   result |= 0x0100;
    if (mod & SDL_KMOD_RALT)   result |= 0x0200;
    if (mod & SDL_KMOD_LGUI)   result |= 0x0400;
    if (mod & SDL_KMOD_RGUI)   result |= 0x0800;
    if (mod & SDL_KMOD_NUM)    result |= 0x1000;
    if (mod & SDL_KMOD_CAPS)   result |= 0x2000;
    if (mod & SDL_KMOD_MODE)   result |= 0x4000;
    return result;
}

/*
 * One host event can owe the guest more than one - a wheel notch is a press and
 * a release in SDL 1.2 - so translation fills a queue that PollEvent drains one
 * at a time, the way the guest expects to receive them.
 */
std::deque<Sdl12Event> g_eventQueue;
std::mutex g_eventMutex;

int (*g_eventFilter)(const Sdl12Event *) = nullptr;
bool g_unicodeEnabled = false;
bool g_keyRepeatEnabled = false;

// Every event type starts enabled, which is where SDL 1.2 starts too.  Only the
// types the bridge can produce need a slot; anything beyond it is let through.
bool g_eventEnabled[32] = {};
bool g_eventDefaultsApplied = false;

bool eventAllowed(uint8_t type)
{
    if (!g_eventDefaultsApplied)
    {
        for (bool &enabled : g_eventEnabled)
            enabled = true;
        g_eventDefaultsApplied = true;
    }
    return type < std::size(g_eventEnabled) ? g_eventEnabled[type] : true;
}

void queueEvent(const Sdl12Event &event)
{
    if (!eventAllowed(event.type))
        return;
    if (g_eventFilter && g_eventFilter(&event) == 0)
        return;
    g_eventQueue.push_back(event);
}

void queueKey(const SDL_KeyboardEvent &key, bool down)
{
    if (!g_keyRepeatEnabled && key.repeat)
        return;

    Sdl12Event event;
    std::memset(&event, 0, sizeof(event));
    event.key.type = down ? sdl12KeyDown : sdl12KeyUp;
    event.key.state = down ? sdl12Pressed : sdl12Released;
    event.key.keysym.scancode = static_cast<uint8_t>(key.scancode);
    event.key.keysym.sym = sdl12KeyFromHost(key.key);
    event.key.keysym.mod = sdl12ModFromHost(key.mod);
    if (g_unicodeEnabled && down && key.key > 0 && key.key <= 0x7F)
        event.key.keysym.unicode = static_cast<uint16_t>(key.key);
    queueEvent(event);
}

void queueMouseButton(uint8_t button, bool down, float x, float y)
{
    Sdl12Event event;
    std::memset(&event, 0, sizeof(event));
    event.button.type = down ? sdl12MouseButtonDown : sdl12MouseButtonUp;
    event.button.button = button;
    event.button.state = down ? sdl12Pressed : sdl12Released;
    event.button.x = static_cast<uint16_t>(x < 0 ? 0 : x);
    event.button.y = static_cast<uint16_t>(y < 0 ? 0 : y);
    queueEvent(event);
}

std::string g_lastError;
std::mutex g_errorMutex;

// The caption the guest last asked for, so the frame rate can be shown beside
// it without losing it.
std::string g_caption;
std::mutex g_captionMutex;

Sdl12PixelFormat g_screenFormat = {};
Sdl12Surface g_screenSurface = {};
Sdl12VideoInfo g_videoInfo = {};
bool g_videoReady = false;

void setError(const char *text)
{
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastError = text ? text : "";
}

/*
 * A 32-bit RGBA description, which is what the loader's GL context always is.
 * SDL 1.2 callers read the masks to decide how to poke at pixels.
 */
void describeScreenFormat(Sdl12PixelFormat &format)
{
    std::memset(&format, 0, sizeof(format));
    format.BitsPerPixel = 32;
    format.BytesPerPixel = 4;
    format.Rmask = 0x00FF0000;
    format.Gmask = 0x0000FF00;
    format.Bmask = 0x000000FF;
    format.Amask = 0xFF000000;
    format.Rshift = 16;
    format.Gshift = 8;
    format.Bshift = 0;
    format.Ashift = 24;
    format.alpha = 255;
}
} // namespace

extern "C"
{
    // ---------------------------------------------------------------- errors

    void bridgeSdl12SetError(const char *format, ...)
    {
        if (!format)
            return;
        char text[512];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
        setError(text);
    }

    const char *bridgeSdl12GetError()
    {
        std::lock_guard<std::mutex> lock(g_errorMutex);
        return g_lastError.c_str();
    }

    void bridgeSdl12ClearError() { setError(""); }
    void bridgeSdl12Error(int code) { bridgeSdl12SetError("SDL error %d", code); }

    // ----------------------------------------------------------- lifecycle

    int bridgeSdl12Init(uint32_t flags)
    {
        log_info("SDL 1.2: init requested (flags=0x%08X)", flags);

        // The loader owns the window and the GL context; SDL3 video is brought
        // up with it, so an SDL_Init asking for video only has to succeed.
        if ((flags & sdl12InitAudio) && !SDL_WasInit(SDL_INIT_AUDIO))
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                log_warn("SDL 1.2: audio subsystem unavailable: %s", SDL_GetError());
        }
        return 0;
    }

    int bridgeSdl12InitSubSystem(uint32_t flags) { return bridgeSdl12Init(flags); }
    void bridgeSdl12Quit() { log_info("SDL 1.2: quit requested"); }

    // --------------------------------------------------------------- video

    /*
     * The same policy the display manager path applies: when the loader is
     * pacing frames itself, vsync is suppressed so the two do not fight, and
     * [Graphics] FPS_TARGET is the single thing deciding the rate.
     */
    bool applySwapInterval(int interval)
    {
        if (getConfig()->fpsLimiter && interval != 0)
        {
            log_info("SDL 1.2: vsync suppressed; [Graphics] FPS_TARGET drives the frame rate");
            interval = 0;
        }
        return setSDLSwapInterval(interval);
    }

    int bridgeSdl12GLSetAttribute(int attribute, int value)
    {
        /*
         * The pixel-format attributes arrive after the loader has already made
         * the window, so nothing can be done with them and they are noted and
         * dropped.  Swap control is the exception: it is not a format at all
         * but the presentation rate, it still takes effect on a live context,
         * and SDL3 spells it as a call rather than an attribute.  Dropping it
         * with the rest left Counter-Strike Neo presenting as fast as the
         * driver would go.
         */
        constexpr int sdl12SwapControl = 16;

        if (attribute == sdl12SwapControl)
            return applySwapInterval(value) ? 0 : -1;

        log_debug("SDL 1.2: GL attribute %d = %d", attribute, value);
        return 0;
    }

    Sdl12Surface *bridgeSdl12SetVideoMode(int width, int height, int bpp, uint32_t flags)
    {
        log_info("SDL 1.2: video mode %dx%d %dbpp (flags=0x%08X)", width, height, bpp, flags);

        if (!getSDLWindow())
            startSDL();

        SDL_Window *window = getSDLWindow();
        if (!window)
        {
            bridgeSdl12SetError("no window");
            return nullptr;
        }

        int actualWidth = width;
        int actualHeight = height;
        SDL_GetWindowSize(window, &actualWidth, &actualHeight);

        describeScreenFormat(g_screenFormat);
        std::memset(&g_screenSurface, 0, sizeof(g_screenSurface));
        g_screenSurface.flags = flags;
        g_screenSurface.format = &g_screenFormat;
        g_screenSurface.w = actualWidth;
        g_screenSurface.h = actualHeight;
        g_screenSurface.pitch = static_cast<uint16_t>(actualWidth * 4);
        g_screenSurface.clip_rect.w = static_cast<uint16_t>(actualWidth);
        g_screenSurface.clip_rect.h = static_cast<uint16_t>(actualHeight);
        g_screenSurface.refcount = 1;
        g_videoReady = true;

        return &g_screenSurface;
    }

    const Sdl12VideoInfo *bridgeSdl12GetVideoInfo()
    {
        describeScreenFormat(g_screenFormat);
        std::memset(&g_videoInfo, 0, sizeof(g_videoInfo));
        g_videoInfo.flags = 0x3; // hardware surfaces and a window manager
        g_videoInfo.vfmt = &g_screenFormat;

        /*
         * video_mem is in kilobytes.  Callers size their texture budget from
         * it, and reporting zero - which is what an unanswered query gives -
         * makes them behave as if there is no card at all.
         */
        g_videoInfo.video_mem = 256 * 1024;

        SDL_Window *window = getSDLWindow();
        if (window)
            SDL_GetWindowSize(window, &g_videoInfo.current_w, &g_videoInfo.current_h);

        return &g_videoInfo;
    }

    /*
     * Presenting is also where the frame is paced, exactly as the display
     * manager path does it for the Wangan titles.  An SDL 1.2 program that
     * never asks for swap control gets no pacing from the driver either, so
     * without this the game runs as fast as the card will draw and everything
     * timed in frames - Counter-Strike Neo's logo and warning screens among
     * them - plays at several times its intended speed.
     */
    void bridgeSdl12GLSwapBuffers()
    {
        // The guest's own caption stays in front of the reading; one that never
        // names its window keeps the name the loader gave it.
        std::string caption;
        {
            std::lock_guard<std::mutex> lock(g_captionMutex);
            caption = g_caption;
        }
        if (caption.empty())
            caption = getConfig()->gameTitle ? getConfig()->gameTitle : "";

        const SDLFramePresentOptions present = {
            caption.c_str(), false, true, nullptr, nullptr, nullptr, nullptr};
        (void)presentSDLFrame(&present);
    }

    /*
     * Kept as well as applied, because the frame rate is appended to it on
     * every present and the guest's own caption should survive that.
     */
    void bridgeSdl12WMSetCaption(const char *title, const char *)
    {
        if (!title)
            return;

        {
            std::lock_guard<std::mutex> lock(g_captionMutex);
            g_caption = title;
        }

        SDL_Window *window = getSDLWindow();
        if (window)
            SDL_SetWindowTitle(window, title);
    }

    Sdl12Surface *bridgeSdl12CreateRGBSurface(uint32_t flags, int width, int height, int depth,
                                              uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask)
    {
        const int bytesPerPixel = depth / 8 > 0 ? depth / 8 : 4;

        Sdl12Surface *surface = static_cast<Sdl12Surface *>(std::calloc(1, sizeof(Sdl12Surface)));
        Sdl12PixelFormat *format = static_cast<Sdl12PixelFormat *>(std::calloc(1, sizeof(Sdl12PixelFormat)));
        if (!surface || !format)
        {
            std::free(surface);
            std::free(format);
            bridgeSdl12SetError("out of memory");
            return nullptr;
        }

        format->BitsPerPixel = static_cast<uint8_t>(depth);
        format->BytesPerPixel = static_cast<uint8_t>(bytesPerPixel);
        format->Rmask = rmask;
        format->Gmask = gmask;
        format->Bmask = bmask;
        format->Amask = amask;
        format->alpha = 255;

        surface->flags = flags;
        surface->format = format;
        surface->w = width;
        surface->h = height;
        surface->pitch = static_cast<uint16_t>(width * bytesPerPixel);
        surface->pixels = std::calloc(1, static_cast<size_t>(surface->pitch) * height);
        surface->clip_rect.w = static_cast<uint16_t>(width);
        surface->clip_rect.h = static_cast<uint16_t>(height);
        surface->refcount = 1;

        if (!surface->pixels)
        {
            std::free(format);
            std::free(surface);
            bridgeSdl12SetError("out of memory");
            return nullptr;
        }
        return surface;
    }

    void bridgeSdl12FreeSurface(Sdl12Surface *surface)
    {
        if (!surface || surface == &g_screenSurface)
            return;
        std::free(surface->pixels);
        std::free(surface->format);
        std::free(surface);
    }

    uint32_t bridgeSdl12MapRGB(const Sdl12PixelFormat *format, uint8_t r, uint8_t g, uint8_t b)
    {
        if (!format)
            return 0;
        return (static_cast<uint32_t>(r) << format->Rshift) | (static_cast<uint32_t>(g) << format->Gshift) |
               (static_cast<uint32_t>(b) << format->Bshift);
    }

    int bridgeSdl12SetColorKey(Sdl12Surface *surface, uint32_t flag, uint32_t key)
    {
        if (surface && surface->format)
        {
            surface->format->colorkey = key;
            surface->flags = flag ? (surface->flags | 0x1000) : (surface->flags & ~0x1000u);
        }
        return 0;
    }

    int bridgeSdl12SetAlpha(Sdl12Surface *surface, uint32_t flag, uint8_t alpha)
    {
        if (surface && surface->format)
        {
            surface->format->alpha = alpha;
            surface->flags = flag ? (surface->flags | 0x10000) : (surface->flags & ~0x10000u);
        }
        return 0;
    }

    /*
     * SDL_ttf and SDL_image compose glyphs and decoded images through this.
     * Only same-depth copies are handled, which is what they ask for once the
     * surfaces they made came from this bridge in the first place.
     */
    int bridgeSdl12UpperBlit(Sdl12Surface *source, Sdl12Rect *sourceRect, Sdl12Surface *destination, Sdl12Rect *destinationRect)
    {
        if (!source || !destination || !source->pixels || !destination->pixels ||
            !source->format || !destination->format)
            return -1;
        if (source->format->BytesPerPixel != destination->format->BytesPerPixel)
        {
            bridgeSdl12SetError("blit between differing pixel formats is not provided");
            return -1;
        }

        int sourceX = sourceRect ? sourceRect->x : 0;
        int sourceY = sourceRect ? sourceRect->y : 0;
        int width = sourceRect && sourceRect->w ? sourceRect->w : source->w;
        int height = sourceRect && sourceRect->h ? sourceRect->h : source->h;
        const int destinationX = destinationRect ? destinationRect->x : 0;
        const int destinationY = destinationRect ? destinationRect->y : 0;

        if (width > source->w - sourceX)
            width = source->w - sourceX;
        if (height > source->h - sourceY)
            height = source->h - sourceY;
        if (width > destination->w - destinationX)
            width = destination->w - destinationX;
        if (height > destination->h - destinationY)
            height = destination->h - destinationY;
        if (width <= 0 || height <= 0)
            return 0;

        const int bytes = source->format->BytesPerPixel;
        for (int row = 0; row < height; row++)
        {
            const uint8_t *from = static_cast<const uint8_t *>(source->pixels) +
                                  static_cast<size_t>(sourceY + row) * source->pitch + static_cast<size_t>(sourceX) * bytes;
            uint8_t *to = static_cast<uint8_t *>(destination->pixels) +
                          static_cast<size_t>(destinationY + row) * destination->pitch +
                          static_cast<size_t>(destinationX) * bytes;
            std::memcpy(to, from, static_cast<size_t>(width) * bytes);
        }

        if (destinationRect)
        {
            destinationRect->w = static_cast<uint16_t>(width);
            destinationRect->h = static_cast<uint16_t>(height);
        }
        return 0;
    }

    Sdl12Surface *bridgeSdl12ConvertSurface(Sdl12Surface *source, Sdl12PixelFormat *format, uint32_t flags)
    {
        if (!source || !format)
            return nullptr;

        Sdl12Surface *converted = bridgeSdl12CreateRGBSurface(flags, source->w, source->h, format->BitsPerPixel,
                                                             format->Rmask, format->Gmask, format->Bmask, format->Amask);
        if (!converted)
            return nullptr;

        if (source->format->BytesPerPixel == converted->format->BytesPerPixel)
        {
            bridgeSdl12UpperBlit(source, nullptr, converted, nullptr);
        }
        else
        {
            log_debug("SDL 1.2: convert between %d and %d bytes per pixel left the surface blank",
                      source->format->BytesPerPixel, converted->format->BytesPerPixel);
        }
        return converted;
    }

    // -------------------------------------------------------------- events

    /*
     * Counter-Strike Neo is played on a keyboard and mouse and reads both from
     * here, so this is the whole of its input: the host's events translated
     * into the shapes and key numbers SDL 1.2 used.  Nothing else drains the
     * host queue while such a game runs - it presents through SDL_GL_SwapBuffers
     * rather than the display manager - so this is also the only place they can
     * be collected.
     */
    int bridgeSdl12PollEvent(Sdl12Event *event)
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);

        SDL_Event hostEvent;
        while (g_eventQueue.empty() && SDL_PollEvent(&hostEvent))
        {
            switch (hostEvent.type)
            {
                case SDL_EVENT_KEY_DOWN:
                    if (!hostEvent.key.repeat)
                        n2HandleHostKey(static_cast<int>(hostEvent.key.key),
                                       static_cast<uint32_t>(hostEvent.key.mod));
                    queueKey(hostEvent.key, true);
                    break;

                case SDL_EVENT_KEY_UP:
                    queueKey(hostEvent.key, false);
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                {
                    Sdl12Event motion;
                    std::memset(&motion, 0, sizeof(motion));
                    motion.motion.type = sdl12MouseMotion;
                    motion.motion.state = static_cast<uint8_t>(hostEvent.motion.state);
                    motion.motion.x = static_cast<uint16_t>(hostEvent.motion.x < 0 ? 0 : hostEvent.motion.x);
                    motion.motion.y = static_cast<uint16_t>(hostEvent.motion.y < 0 ? 0 : hostEvent.motion.y);
                    motion.motion.xrel = static_cast<int16_t>(hostEvent.motion.xrel);
                    motion.motion.yrel = static_cast<int16_t>(hostEvent.motion.yrel);
                    queueEvent(motion);
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    queueMouseButton(hostEvent.button.button, true, hostEvent.button.x, hostEvent.button.y);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    queueMouseButton(hostEvent.button.button, false, hostEvent.button.x, hostEvent.button.y);
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                {
                    // SDL 1.2 had no wheel event; a notch was a press and a
                    // release of button 4 (up) or 5 (down).
                    if (hostEvent.wheel.y == 0)
                        break;
                    const uint8_t button = hostEvent.wheel.y > 0 ? 4 : 5;
                    float x = 0.0f, y = 0.0f;
                    SDL_GetMouseState(&x, &y);
                    queueMouseButton(button, true, x, y);
                    queueMouseButton(button, false, x, y);
                    break;
                }

                case SDL_EVENT_QUIT:
                {
                    Sdl12Event quit;
                    std::memset(&quit, 0, sizeof(quit));
                    quit.type = sdl12Quit;
                    queueEvent(quit);
                    break;
                }

                default:
                    break;
            }
        }

        if (g_eventQueue.empty())
            return 0;

        if (event)
            *event = g_eventQueue.front();
        g_eventQueue.pop_front();
        return 1;
    }

    /*
     * SDL 1.2 let a program silence event types it did not want; a bridge that
     * always answered "ignored" would tell a caller checking the current state
     * that its own input was switched off.
     */
    uint8_t bridgeSdl12EventState(uint8_t type, int state)
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);

        const uint8_t previous = eventAllowed(type) ? sdl12Enable : sdl12Ignore;
        if (state == sdl12Enable || state == sdl12Ignore)
        {
            if (type < std::size(g_eventEnabled))
                g_eventEnabled[type] = state == sdl12Enable;
        }
        else if (state != sdl12Query)
        {
            log_debug("SDL 1.2: unknown event state %d for type %u", state, type);
        }
        return previous;
    }

    void bridgeSdl12SetEventFilter(void *filter)
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        g_eventFilter = reinterpret_cast<int (*)(const Sdl12Event *)>(filter);
    }

    // SDL 1.2 repeats only when asked, and a zero delay is how it is turned off.
    int bridgeSdl12EnableKeyRepeat(int delay, int)
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        g_keyRepeatEnabled = delay != 0;
        return 0;
    }

    int bridgeSdl12EnableUNICODE(int enable)
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);

        const int previous = g_unicodeEnabled ? 1 : 0;
        if (enable >= 0)
            g_unicodeEnabled = enable != 0;
        return previous;
    }

    // --------------------------------------------------------------- timing

    uint32_t bridgeSdl12GetTicks() { return static_cast<uint32_t>(SDL_GetTicks()); }
    void bridgeSdl12Delay(uint32_t milliseconds) { SDL_Delay(milliseconds); }

    // -------------------------------------------------- threads and locking

    void *bridgeSdl12CreateThread(int (*entry)(void *), void *argument)
    {
        return SDL_CreateThread(entry, "sdl12", argument);
    }

    void bridgeSdl12WaitThread(void *thread, int *status)
    {
        SDL_WaitThread(static_cast<SDL_Thread *>(thread), status);
    }

    void *bridgeSdl12CreateMutex() { return SDL_CreateMutex(); }
    void bridgeSdl12DestroyMutex(void *mutex) { SDL_DestroyMutex(static_cast<SDL_Mutex *>(mutex)); }

    int bridgeSdl12MutexP(void *mutex)
    {
        SDL_LockMutex(static_cast<SDL_Mutex *>(mutex));
        return 0;
    }

    int bridgeSdl12MutexV(void *mutex)
    {
        SDL_UnlockMutex(static_cast<SDL_Mutex *>(mutex));
        return 0;
    }

    void *bridgeSdl12CreateSemaphore(uint32_t initial) { return SDL_CreateSemaphore(initial); }
    void bridgeSdl12DestroySemaphore(void *semaphore) { SDL_DestroySemaphore(static_cast<SDL_Semaphore *>(semaphore)); }

    int bridgeSdl12SemWait(void *semaphore)
    {
        SDL_WaitSemaphore(static_cast<SDL_Semaphore *>(semaphore));
        return 0;
    }

    int bridgeSdl12SemPost(void *semaphore)
    {
        SDL_SignalSemaphore(static_cast<SDL_Semaphore *>(semaphore));
        return 0;
    }

    uint32_t bridgeSdl12SemValue(void *semaphore)
    {
        return SDL_GetSemaphoreValue(static_cast<SDL_Semaphore *>(semaphore));
    }

    // --------------------------------------------------------------- audio

    int bridgeSdl12OpenAudio(Sdl12AudioSpec *desired, Sdl12AudioSpec *obtained)
    {
        if (!desired)
            return -1;
        if (obtained)
            *obtained = *desired;
        log_info("SDL 1.2: audio open %d Hz, %d channels (not yet routed)", desired->freq, desired->channels);
        return 0;
    }

    void bridgeSdl12CloseAudio() {}
    void bridgeSdl12PauseAudio(int) {}
    void bridgeSdl12LockAudio() {}
    void bridgeSdl12UnlockAudio() {}
    void bridgeSdl12MixAudio(uint8_t *, const uint8_t *, uint32_t, int) {}

    int bridgeSdl12BuildAudioCVT(Sdl12AudioCVT *cvt, uint16_t srcFormat, uint8_t, int,
                                 uint16_t dstFormat, uint8_t, int)
    {
        if (!cvt)
            return -1;
        std::memset(cvt, 0, sizeof(*cvt));
        cvt->src_format = srcFormat;
        cvt->dst_format = dstFormat;
        cvt->len_mult = 1;
        cvt->len_ratio = 1.0;
        cvt->needed = 0;
        return 0;
    }

    int bridgeSdl12ConvertAudio(Sdl12AudioCVT *cvt)
    {
        if (cvt)
            cvt->len_cvt = cvt->len;
        return 0;
    }

    Sdl12Surface *bridgeSdl12LoadWAV_RW(Sdl12RWops *, int, Sdl12AudioSpec *, uint8_t **buffer, uint32_t *length)
    {
        if (buffer)
            *buffer = nullptr;
        if (length)
            *length = 0;
        bridgeSdl12SetError("WAV loading is not provided by the SDL 1.2 bridge");
        return nullptr;
    }

    void bridgeSdl12FreeWAV(uint8_t *buffer) { std::free(buffer); }

    // --------------------------------------------------------------- RWops

    int rwSeekStdio(Sdl12RWops *context, int offset, int whence)
    {
        FILE *file = static_cast<FILE *>(context->hidden.stdio.fp);
        if (std::fseek(file, offset, whence) != 0)
            return -1;
        return static_cast<int>(std::ftell(file));
    }

    int rwReadStdio(Sdl12RWops *context, void *pointer, int size, int maximum)
    {
        FILE *file = static_cast<FILE *>(context->hidden.stdio.fp);
        return static_cast<int>(std::fread(pointer, size, maximum, file));
    }

    int rwWriteStdio(Sdl12RWops *context, const void *pointer, int size, int number)
    {
        FILE *file = static_cast<FILE *>(context->hidden.stdio.fp);
        return static_cast<int>(std::fwrite(pointer, size, number, file));
    }

    int rwCloseStdio(Sdl12RWops *context)
    {
        if (context->hidden.stdio.autoclose)
            std::fclose(static_cast<FILE *>(context->hidden.stdio.fp));
        std::free(context);
        return 0;
    }

    int rwSeekMem(Sdl12RWops *context, int offset, int whence)
    {
        uint8_t *target = nullptr;
        switch (whence)
        {
            case 0: target = context->hidden.mem.base + offset; break;
            case 1: target = context->hidden.mem.here + offset; break;
            case 2: target = context->hidden.mem.stop + offset; break;
            default: return -1;
        }
        if (target < context->hidden.mem.base || target > context->hidden.mem.stop)
            return -1;
        context->hidden.mem.here = target;
        return static_cast<int>(target - context->hidden.mem.base);
    }

    int rwReadMem(Sdl12RWops *context, void *pointer, int size, int maximum)
    {
        if (size <= 0 || maximum <= 0)
            return 0;
        int available = static_cast<int>((context->hidden.mem.stop - context->hidden.mem.here) / size);
        if (available > maximum)
            available = maximum;
        if (available <= 0)
            return 0;
        std::memcpy(pointer, context->hidden.mem.here, static_cast<size_t>(available) * size);
        context->hidden.mem.here += static_cast<size_t>(available) * size;
        return available;
    }

    int rwWriteMem(Sdl12RWops *context, const void *pointer, int size, int number)
    {
        const int room = static_cast<int>((context->hidden.mem.stop - context->hidden.mem.here) / size);
        const int writable = room < number ? room : number;
        if (writable <= 0)
            return 0;
        std::memcpy(context->hidden.mem.here, pointer, static_cast<size_t>(writable) * size);
        context->hidden.mem.here += static_cast<size_t>(writable) * size;
        return writable;
    }

    int rwCloseMem(Sdl12RWops *context)
    {
        std::free(context);
        return 0;
    }

    Sdl12RWops *bridgeSdl12RWFromFP(void *fp, int autoclose)
    {
        if (!fp)
            return nullptr;
        Sdl12RWops *ops = static_cast<Sdl12RWops *>(std::calloc(1, sizeof(Sdl12RWops)));
        if (!ops)
            return nullptr;
        ops->seek = rwSeekStdio;
        ops->read = rwReadStdio;
        ops->write = rwWriteStdio;
        ops->close = rwCloseStdio;
        ops->type = 2;
        ops->hidden.stdio.autoclose = autoclose;
        ops->hidden.stdio.fp = fp;
        return ops;
    }

    Sdl12RWops *bridgeSdl12RWFromFile(const char *file, const char *mode)
    {
        if (!file || !mode)
            return nullptr;
        FILE *handle = std::fopen(file, mode);
        if (!handle)
        {
            bridgeSdl12SetError("could not open %s", file);
            return nullptr;
        }
        return bridgeSdl12RWFromFP(handle, 1);
    }

    Sdl12RWops *bridgeSdl12RWFromMem(void *memory, int size)
    {
        if (!memory || size < 0)
            return nullptr;
        Sdl12RWops *ops = static_cast<Sdl12RWops *>(std::calloc(1, sizeof(Sdl12RWops)));
        if (!ops)
            return nullptr;
        ops->seek = rwSeekMem;
        ops->read = rwReadMem;
        ops->write = rwWriteMem;
        ops->close = rwCloseMem;
        ops->type = 4;
        ops->hidden.mem.base = static_cast<uint8_t *>(memory);
        ops->hidden.mem.here = ops->hidden.mem.base;
        ops->hidden.mem.stop = ops->hidden.mem.base + size;
        return ops;
    }

    uint16_t bridgeSdl12ReadLE16(Sdl12RWops *context)
    {
        uint16_t value = 0;
        context->read(context, &value, sizeof(value), 1);
        return value;
    }

    uint32_t bridgeSdl12ReadLE32(Sdl12RWops *context)
    {
        uint32_t value = 0;
        context->read(context, &value, sizeof(value), 1);
        return value;
    }

    uint16_t bridgeSdl12ReadBE16(Sdl12RWops *context)
    {
        const uint16_t value = bridgeSdl12ReadLE16(context);
        return static_cast<uint16_t>((value >> 8) | (value << 8));
    }

    uint32_t bridgeSdl12ReadBE32(Sdl12RWops *context)
    {
        const uint32_t value = bridgeSdl12ReadLE32(context);
        return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) | ((value & 0x00FF0000u) >> 8) |
               ((value & 0xFF000000u) >> 24);
    }

    // --------------------------------------------------------- YUV overlay

    Sdl12Overlay *bridgeSdl12CreateYUVOverlay(int width, int height, uint32_t format, Sdl12Surface *)
    {
        /*
         * The movie player draws through these.  The planes are real memory so
         * the decoder can write them; nothing presents them yet, so a video
         * plays silently behind whatever else is on screen rather than
         * crashing the decoder with a null plane.
         */
        Sdl12Overlay *overlay = static_cast<Sdl12Overlay *>(std::calloc(1, sizeof(Sdl12Overlay)));
        if (!overlay)
            return nullptr;

        const int planes = 3;
        overlay->format = format;
        overlay->w = width;
        overlay->h = height;
        overlay->planes = planes;
        overlay->pitches = static_cast<uint16_t *>(std::calloc(planes, sizeof(uint16_t)));
        overlay->pixels = static_cast<uint8_t **>(std::calloc(planes, sizeof(uint8_t *)));
        if (!overlay->pitches || !overlay->pixels)
        {
            std::free(overlay->pitches);
            std::free(overlay->pixels);
            std::free(overlay);
            return nullptr;
        }

        for (int plane = 0; plane < planes; plane++)
        {
            const int planeWidth = plane == 0 ? width : width / 2;
            const int planeHeight = plane == 0 ? height : height / 2;
            overlay->pitches[plane] = static_cast<uint16_t>(planeWidth);
            overlay->pixels[plane] = static_cast<uint8_t *>(std::calloc(1, static_cast<size_t>(planeWidth) * planeHeight + 16));
        }

        log_debug("SDL 1.2: created a %dx%d YUV overlay", width, height);
        return overlay;
    }

    int bridgeSdl12LockYUVOverlay(Sdl12Overlay *) { return 0; }
    void bridgeSdl12UnlockYUVOverlay(Sdl12Overlay *) {}
    int bridgeSdl12DisplayYUVOverlay(Sdl12Overlay *, Sdl12Rect *) { return 0; }

    void bridgeSdl12FreeYUVOverlay(Sdl12Overlay *overlay)
    {
        if (!overlay)
            return;
        if (overlay->pixels)
            for (int plane = 0; plane < overlay->planes; plane++)
                std::free(overlay->pixels[plane]);
        std::free(overlay->pixels);
        std::free(overlay->pitches);
        std::free(overlay);
    }

    /*
     * Not an SDL entry point: Namco added it so the game could reach the display
     * manager the arcade board's driver exposed.  Answering "no device" is not
     * open to the bridge - none of Counter-Strike Neo's four call sites checks
     * the result before reading the handle out of it - so a record has to come
     * back, shaped the way those call sites read it: a handle at +4 that the
     * adm* entry points accept, and a screen at +8 that one of them walks.
     *
     * The loader drives a single window, so there is a single device, and the
     * adm* hooks answer for it whatever handle they are given.
     */
    struct Sdl12AdmScreen
    {
        uint32_t reserved[16];
    };

    struct Sdl12AdmDevice
    {
        int32_t index;
        int32_t handle;
        Sdl12AdmScreen *screen;
    };

    void *bridgeSdl12ADMGetDevice(int32_t index)
    {
        static Sdl12AdmScreen screen = {};
        static Sdl12AdmDevice device = {0, 0, &screen};

        device.index = index;
        return &device;
    }
}

namespace Sdl12Bridge
{
    void initBridges()
    {
        log_info("Initializing SDL 1.2 Bridges...");

        MAP("SDL_Init", bridgeSdl12Init);
        MAP("SDL_InitSubSystem", bridgeSdl12InitSubSystem);
        MAP("SDL_Quit", bridgeSdl12Quit);
        MAP("SDL_GetError", bridgeSdl12GetError);
        MAP("SDL_SetError", bridgeSdl12SetError);
        MAP("SDL_ClearError", bridgeSdl12ClearError);
        MAP("SDL_Error", bridgeSdl12Error);

        MAP("SDL_SetVideoMode", bridgeSdl12SetVideoMode);
        MAP("SDL_GetVideoInfo", bridgeSdl12GetVideoInfo);
        MAP("SDL_GL_SetAttribute", bridgeSdl12GLSetAttribute);
        MAP("SDL_GL_SwapBuffers", bridgeSdl12GLSwapBuffers);
        MAP("SDL_WM_SetCaption", bridgeSdl12WMSetCaption);
        MAP("SDL_CreateRGBSurface", bridgeSdl12CreateRGBSurface);
        MAP("SDL_FreeSurface", bridgeSdl12FreeSurface);
        MAP("SDL_MapRGB", bridgeSdl12MapRGB);
        MAP("SDL_SetColorKey", bridgeSdl12SetColorKey);
        MAP("SDL_SetAlpha", bridgeSdl12SetAlpha);
        MAP("SDL_UpperBlit", bridgeSdl12UpperBlit);
        MAP("SDL_BlitSurface", bridgeSdl12UpperBlit);
        MAP("SDL_ConvertSurface", bridgeSdl12ConvertSurface);

        MAP("SDL_PollEvent", bridgeSdl12PollEvent);
        MAP("SDL_EventState", bridgeSdl12EventState);
        MAP("SDL_SetEventFilter", bridgeSdl12SetEventFilter);
        MAP("SDL_EnableKeyRepeat", bridgeSdl12EnableKeyRepeat);
        MAP("SDL_EnableUNICODE", bridgeSdl12EnableUNICODE);

        MAP("SDL_GetTicks", bridgeSdl12GetTicks);
        MAP("SDL_Delay", bridgeSdl12Delay);

        MAP("SDL_CreateThread", bridgeSdl12CreateThread);
        MAP("SDL_WaitThread", bridgeSdl12WaitThread);
        MAP("SDL_CreateMutex", bridgeSdl12CreateMutex);
        MAP("SDL_DestroyMutex", bridgeSdl12DestroyMutex);
        MAP("SDL_mutexP", bridgeSdl12MutexP);
        MAP("SDL_mutexV", bridgeSdl12MutexV);
        MAP("SDL_CreateSemaphore", bridgeSdl12CreateSemaphore);
        MAP("SDL_DestroySemaphore", bridgeSdl12DestroySemaphore);
        MAP("SDL_SemWait", bridgeSdl12SemWait);
        MAP("SDL_SemPost", bridgeSdl12SemPost);
        MAP("SDL_SemValue", bridgeSdl12SemValue);

        MAP("SDL_OpenAudio", bridgeSdl12OpenAudio);
        MAP("SDL_CloseAudio", bridgeSdl12CloseAudio);
        MAP("SDL_PauseAudio", bridgeSdl12PauseAudio);
        MAP("SDL_LockAudio", bridgeSdl12LockAudio);
        MAP("SDL_UnlockAudio", bridgeSdl12UnlockAudio);
        MAP("SDL_MixAudio", bridgeSdl12MixAudio);
        MAP("SDL_BuildAudioCVT", bridgeSdl12BuildAudioCVT);
        MAP("SDL_ConvertAudio", bridgeSdl12ConvertAudio);
        MAP("SDL_LoadWAV_RW", bridgeSdl12LoadWAV_RW);
        MAP("SDL_FreeWAV", bridgeSdl12FreeWAV);

        MAP("SDL_RWFromFile", bridgeSdl12RWFromFile);
        MAP("SDL_RWFromFP", bridgeSdl12RWFromFP);
        MAP("SDL_RWFromMem", bridgeSdl12RWFromMem);
        MAP("SDL_ReadLE16", bridgeSdl12ReadLE16);
        MAP("SDL_ReadLE32", bridgeSdl12ReadLE32);
        MAP("SDL_ReadBE16", bridgeSdl12ReadBE16);
        MAP("SDL_ReadBE32", bridgeSdl12ReadBE32);

        MAP("SDL_CreateYUVOverlay", bridgeSdl12CreateYUVOverlay);
        MAP("SDL_LockYUVOverlay", bridgeSdl12LockYUVOverlay);
        MAP("SDL_UnlockYUVOverlay", bridgeSdl12UnlockYUVOverlay);
        MAP("SDL_DisplayYUVOverlay", bridgeSdl12DisplayYUVOverlay);
        MAP("SDL_FreeYUVOverlay", bridgeSdl12FreeYUVOverlay);

        MAP("SDL_ADM_GetDevice", bridgeSdl12ADMGetDevice);
    }
} // namespace Sdl12Bridge

#endif
