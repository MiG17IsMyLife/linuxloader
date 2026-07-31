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

#ifdef __cplusplus
}
#endif
