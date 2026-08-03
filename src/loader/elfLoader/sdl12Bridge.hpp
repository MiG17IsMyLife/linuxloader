#pragma once

#include <stdint.h>

#pragma pack(push, 4)

struct Sdl12Rect
{
    int16_t x, y;
    uint16_t w, h;
};

struct Sdl12Color
{
    uint8_t r, g, b, unused;
};

struct Sdl12Palette
{
    int ncolors;
    Sdl12Color *colors;
};

struct Sdl12PixelFormat
{
    Sdl12Palette *palette;
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint8_t Rloss, Gloss, Bloss, Aloss;
    uint8_t Rshift, Gshift, Bshift, Ashift;
    uint32_t Rmask, Gmask, Bmask, Amask;
    uint32_t colorkey;
    uint8_t alpha;
};

struct Sdl12Surface
{
    uint32_t flags;
    Sdl12PixelFormat *format;
    int w, h;
    uint16_t pitch;
    void *pixels;
    int offset;
    void *hwdata;
    Sdl12Rect clip_rect;
    uint32_t unused1;
    uint32_t locked;
    void *map;
    unsigned int format_version;
    int refcount;
};

struct Sdl12VideoInfo
{
    // A bitfield in SDL: hw_available:1, wm_available:1, then padding and
    // blit capability flags.  The guest only ever reads video_mem and the
    // format pointer, so the flags are carried as one word.
    uint32_t flags;
    uint32_t video_mem;
    Sdl12PixelFormat *vfmt;
    int current_w;
    int current_h;
};

struct Sdl12Keysym
{
    uint8_t scancode;
    int sym;
    int mod;
    uint16_t unicode;
};

struct Sdl12KeyboardEvent
{
    uint8_t type;
    uint8_t which;
    uint8_t state;
    Sdl12Keysym keysym;
};

struct Sdl12MouseMotionEvent
{
    uint8_t type;
    uint8_t which;
    uint8_t state;
    uint16_t x, y;
    int16_t xrel, yrel;
};

struct Sdl12MouseButtonEvent
{
    uint8_t type;
    uint8_t which;
    uint8_t button;
    uint8_t state;
    uint16_t x, y;
};

struct Sdl12ResizeEvent
{
    uint8_t type;
    int w, h;
};

/*
 * Exactly as wide as SDL 1.2's SDL_Event: twenty bytes, the size of its
 * keyboard event. The guest declares one on its stack and passes the address,
 * so anything wider is written straight past it - padding this to 64 smashed 44
 * bytes of the caller's frame.
 */
union Sdl12Event
{
    uint8_t type;
    Sdl12KeyboardEvent key;
    Sdl12MouseMotionEvent motion;
    Sdl12MouseButtonEvent button;
    Sdl12ResizeEvent resize;
    uint8_t padding[20];
};

struct Sdl12AudioSpec
{
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint16_t padding;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
};

struct Sdl12AudioCVT
{
    int needed;
    uint16_t src_format;
    uint16_t dst_format;
    double rate_incr;
    uint8_t *buf;
    int len;
    int len_cvt;
    int len_mult;
    double len_ratio;
    void (*filters[10])(struct Sdl12AudioCVT *cvt, uint16_t format);
    int filter_index;
};

struct Sdl12RWops
{
    int (*seek)(struct Sdl12RWops *context, int offset, int whence);
    int (*read)(struct Sdl12RWops *context, void *ptr, int size, int maxnum);
    int (*write)(struct Sdl12RWops *context, const void *ptr, int size, int num);
    int (*close)(struct Sdl12RWops *context);
    uint32_t type;
    union
    {
        struct
        {
            int autoclose;
            void *fp;
        } stdio;
        struct
        {
            uint8_t *base;
            uint8_t *here;
            uint8_t *stop;
        } mem;
        struct
        {
            void *data1;
        } unknown;
    } hidden;
};

struct Sdl12Overlay
{
    uint32_t format;
    int w, h;
    int planes;
    uint16_t *pitches;
    uint8_t **pixels;
    void *hwfuncs;
    void *hwdata;
    uint32_t hw_overlay_flag;
};

#pragma pack(pop)

// The guest supplies the storage for these, so their size is not ours to pick.
static_assert(sizeof(union Sdl12Event) == 20, "i386 SDL 1.2 SDL_Event is 20 bytes");
static_assert(sizeof(struct Sdl12KeyboardEvent) == 20, "i386 SDL 1.2 SDL_KeyboardEvent is 20 bytes");
static_assert(sizeof(struct Sdl12MouseMotionEvent) == 12, "i386 SDL 1.2 SDL_MouseMotionEvent is 12 bytes");
static_assert(sizeof(struct Sdl12MouseButtonEvent) == 8, "i386 SDL 1.2 SDL_MouseButtonEvent is 8 bytes");

namespace Sdl12Bridge
{
    void initBridges();
}
