#pragma once

#include <GL/gl.h>
#include <SDL3/SDL_video.h>

#ifdef __cplusplus
extern "C" {
#endif

int initSDL();
void startSDL();
SDL_Window* getSDLWindow();
SDL_GLContext getSDLContext();
int makeSDLCurrent(SDL_Window *win, SDL_GLContext ctx);
void sdlQuit();
void pollEvents();

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
