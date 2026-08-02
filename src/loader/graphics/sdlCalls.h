#pragma once

#include <GL/gl.h>
#include <stdbool.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#ifdef __cplusplus
extern "C" {
#endif

int initSDL();
void startSDL();
SDL_Window* getSDLWindow();
SDL_GLContext getSDLContext();
bool makeSDLCurrent(SDL_Window *win, SDL_GLContext ctx);
bool setSDLSwapInterval(int interval);
bool runOnSDLMainThread(SDL_MainThreadCallback callback, void *userdata, bool waitComplete);
void sdlQuit();
void pollEvents();

typedef void (*SDLFrameCallback)(void *userdata);

typedef struct SDLFramePresentOptions
{
    const char *title;
    bool processEvents;
    bool profileFrame;
    SDLFrameCallback beforeEvents;
    SDLFrameCallback beforeSwap;
    SDLFrameCallback afterPresent;
    void *userdata;
} SDLFramePresentOptions;

/*
 * Shared frame boundary for ADM, GLX, GLUT, and SDL 1.2 guests. Platform
 * bridges keep their ABI and platform-specific drawing in the callbacks.
 */
int presentSDLFrame(const SDLFramePresentOptions *options);

/*
 * Drains the window's message queue from a long stretch of work that does not
 * present a frame, so Windows does not mark the game as not responding while a
 * course loads. Cheap to call often; does nothing off the window's thread.
 */
void keepWindowResponsive(void);
void showFpsInWindowTitle(const char *name);

#ifdef __cplusplus
}
#endif
