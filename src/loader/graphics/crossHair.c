#define GL_GLEXT_PROTOTYPES
#include <glad/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3_image/SDL_image.h>

#include "crossHair.h"
#include "../config/config.h"
#include "../patching/patchResolution.h"
// #include "glInitFunctions.h"

#ifdef __linux__
#include <dlfcn.h>
#include <pthread.h>
static pthread_t pollingThreadId = 0;
#else
#include "loader/elfLoader/pthread/pthreadEmu.hpp"
static uint32_t pollingThreadId = 0;
#endif

#define MAX_PLAYERS 2
#define INACTIVITY_TIMEOUT 3

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;
extern int drawableW;
extern int drawableH;

extern int phX, phY, phW, phH;
extern int phX2, phY2, phW2, phH2;

static Crosshair crossHair[MAX_PLAYERS];
static GLuint gShaderProgram = 0;
static GLint gUProjectionLoc = -1;
static GLint gUTextureLoc = -1;

#ifndef _WIN32
#define __stdcall
#endif

void(__stdcall *real_glDrawArrays)(GLenum, GLint, GLsizei) = NULL;

bool p1CrossHairInitialized = false;
bool p2CrossHairInitialized = false;
bool testMode = false;

int textureIdIdxAdjust = 0;

static const char *vertex_shader_source = "#version 120\n"
                                          "attribute vec2 a_pos;\n"
                                          "attribute vec2 a_tex_coord;\n"
                                          "varying vec2 v_tex_coord;\n"
                                          "uniform mat4 u_projection;\n"
                                          "void main() {\n"
                                          "    gl_Position = u_projection * vec4(a_pos.x, a_pos.y, 0.0, 1.0);\n"
                                          "    v_tex_coord = a_tex_coord;\n"
                                          "}\n";
static const char *fragment_shader_source = "#version 120\n"
                                            "varying vec2 v_tex_coord;\n"
                                            "uniform sampler2D u_texture;\n"
                                            "void main() {\n"
                                            "    gl_FragColor = texture2D(u_texture, v_tex_coord);\n"
                                            "}\n";

GLuint compileShader(GLenum type, const char *source)
{
    GLuint shader = glad_glCreateShader(type);
    glad_glShaderSource(shader, 1, &source, NULL);
    glad_glCompileShader(shader);
    GLint success;
    glad_glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glad_glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "ERROR: Shader compilation failed\n%s\n", infoLog);
        glad_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram()
{
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertex_shader_source);
    if (vert == 0)
        return 0;
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragment_shader_source);
    if (frag == 0)
    {
        glad_glDeleteShader(vert);
        return 0;
    }
    GLuint program = glad_glCreateProgram();
    glad_glAttachShader(program, vert);
    glad_glAttachShader(program, frag);
    glad_glBindAttribLocation(program, 0, "a_pos");
    glad_glBindAttribLocation(program, 1, "a_tex_coord");
    glad_glLinkProgram(program);
    GLint success;
    glad_glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glad_glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "ERROR: Shader linking failed\n%s\n", infoLog);
    }
    glad_glDeleteShader(vert);
    glad_glDeleteShader(frag);
    return program;
}

void createCrosshairGeometry(Crosshair *ch)
{
    float w = (float)ch->width;
    float h = (float)ch->height;
    float vertices[] = {0.0f, h, 0.0f, 1.0f, w, h,    1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        w,    h, 1.0f, 1.0f, w, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    glad_glGenVertexArrays(1, &ch->vao);
    glad_glGenBuffers(1, &ch->vbo);
    glad_glBindVertexArray(ch->vao);
    glad_glBindBuffer(GL_ARRAY_BUFFER, ch->vbo);
    glad_glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glad_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glad_glEnableVertexAttribArray(0);
    glad_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glad_glEnableVertexAttribArray(1);
    glad_glBindBuffer(GL_ARRAY_BUFFER, 0);
    glad_glBindVertexArray(0);
}

void initCrossHairs()
{
    if (gShaderProgram == 0)
    {
        gShaderProgram = createShaderProgram();
        gUProjectionLoc = glad_glGetUniformLocation(gShaderProgram, "u_projection");
        gUTextureLoc = glad_glGetUniformLocation(gShaderProgram, "u_texture");
    }

    if (gShaderProgram == 0)
        return;

    if (loadCrosshairImage(0, getConfig()->p1CrossHairPath))
        p1CrossHairInitialized = true;

    if (loadCrosshairImage(1, getConfig()->p2CrossHairPath))
        p2CrossHairInitialized = true;

    if (isTestMode() || gGrp == GROUP_HOD4_TEST || gGrp == GROUP_HOD4_SP_TEST)
        testMode = true;

    if (!testMode && gId != PRIMEVAL_HUNT_SBPP)
        startPollingThread();

    if (!real_glDrawArrays)
#ifdef __linux__
        real_glDrawArrays = dlsym(RTLD_NEXT, "glDrawArrays");
#else
        real_glDrawArrays = (__stdcall void (*)(GLenum, GLint, GLsizei))SDL_GL_GetProcAddress("glDrawArrays");
#endif

    textureIdIdxAdjust = p1CrossHairInitialized + p2CrossHairInitialized;
}

int loadCrosshairImage(int player, const char *filepath)
{
    if (player < 0 || player >= MAX_PLAYERS)
        return 0;

    if (crossHair[player].surface)
    {
        SDL_DestroySurface(crossHair[player].surface);
        crossHair[player].surface = NULL;
    }
    if (crossHair[player].texture)
    {
        glad_glDeleteTextures(1, &crossHair[player].texture);
        crossHair[player].texture = 0;
    }
    if (crossHair[player].vao)
    {
        glad_glDeleteVertexArrays(1, &crossHair[player].vao);
        crossHair[player].vao = 0;
    }
    if (crossHair[player].vbo)
    {
        glad_glDeleteBuffers(1, &crossHair[player].vbo);
        crossHair[player].vbo = 0;
    }

    SDL_Surface *surface = IMG_Load(filepath);
    if (!surface)
    {
        fprintf(stderr, "Failed to load PNG for player %d: %s\n", player + 1, SDL_GetError());
        return 0;
    }
    crossHair[player].surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);

    crossHair[player].x = (int)(drawableW / 2.0f);
    crossHair[player].y = (int)(drawableH / 2.0f);
    crossHair[player].visible = false;
    // crossHair[player].lastMovementTime = time(NULL);
    crossHair[player].texture = 0;
    crossHair[player].vao = 0;
    crossHair[player].vbo = 0;

    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
    {
        GLuint tex;
        glad_glGenTextures(1, &tex);
        glad_glBindTexture(GL_TEXTURE_2D, tex);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, crossHair[player].surface->w, crossHair[player].surface->h, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, crossHair[player].surface->pixels);

        SDL_DestroySurface(crossHair[player].surface);

        crossHair[player].width = getConfig()->customCrossHairWidth;
        crossHair[player].height = getConfig()->customCrossHairHeight;
        crossHair[player].texture = tex;
    }

    return 1;
}

void updateCrosshairPosition(int player, float normX, float normY)
{
    if (player < 0 || player >= MAX_PLAYERS)
        return;
    crossHair[player].x = normX * drawableW;
    crossHair[player].y = normY * drawableH;
    // crossHair[player].lastMovementTime = time(NULL);
    // crossHair[player].visible = true;
    if (testMode || gId == PRIMEVAL_HUNT_SBPP)
        crossHair[player].visible = true;
}

void renderCrosshairs(void)
{
    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
        return;

    if (gId == PRIMEVAL_HUNT_SBPP)
        glad_glViewport(phX, phY, phW, phH);
    else if (gGrp == GROUP_HOD4_SP)
        glad_glViewport(0, 0, gWidth, gHeight);

    GLint texFormat = 0;
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &texFormat);

    GLint last_program, last_vao, last_vbo, last_active_texture, last_depth_func;
    GLboolean last_blend_enabled, last_depth_enabled; //, last_srgb_enabled;
    GLint last_blend_src, last_blend_dst;
    glad_glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    glad_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vao);
    glad_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_vbo);
    glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active_texture);
    last_blend_enabled = glad_glIsEnabled(GL_BLEND);
    last_depth_enabled = glad_glIsEnabled(GL_DEPTH_TEST);
    // last_srgb_enabled = _glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glad_glGetIntegerv(GL_BLEND_SRC_RGB, &last_blend_src);
    glad_glGetIntegerv(GL_BLEND_DST_RGB, &last_blend_dst);
    glad_glGetIntegerv(GL_DEPTH_FUNC, &last_depth_func);

    // if (last_srgb_enabled)
    //     _glDisable(GL_FRAMEBUFFER_SRGB);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc(GL_ALWAYS);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glUseProgram(gShaderProgram);
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glUniform1i(gUTextureLoc, 0);

    // time_t currentTime = time(NULL);
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        // if (difftime(currentTime, crossHair[i].lastMovementTime) > INACTIVITY_TIMEOUT)
        // {
        //     crossHair[i].visible = false;
        // }
        if (!crossHair[i].visible)
            continue;

        if (crossHair[i].texture == 0 && crossHair[i].surface != NULL)
        {
            glad_glGenTextures(1, &crossHair[i].texture);
            glad_glBindTexture(GL_TEXTURE_2D, crossHair[i].texture);
            glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, crossHair[i].surface->w, crossHair[i].surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              crossHair[i].surface->pixels);

            crossHair[i].width = getConfig()->customCrossHairWidth;
            crossHair[i].height = getConfig()->customCrossHairHeight;
            createCrosshairGeometry(&crossHair[i]);

            SDL_DestroySurface(crossHair[i].surface);
            crossHair[i].surface = NULL;
        }

        if (crossHair[i].texture != 0 && crossHair[i].vao != 0)
        {
            float x = crossHair[i].x - (crossHair[i].width / 2.0f);
            float y = crossHair[i].y - (crossHair[i].height / 2.0f);
            float projection[16] = {0.0f};
            projection[0] = 2.0f / drawableW;
            projection[5] = -2.0f / drawableH;
            projection[10] = -1.0f;
            projection[12] = -1.0f + (x * projection[0]);
            projection[13] = 1.0f + (y * projection[5]);
            projection[15] = 1.0f;
            glad_glUniformMatrix4fv(gUProjectionLoc, 1, GL_FALSE, projection);

            glad_glBindTexture(GL_TEXTURE_2D, crossHair[i].texture);
            glad_glBindVertexArray(crossHair[i].vao);
            real_glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    glad_glBindVertexArray(last_vao);
    glad_glBindBuffer(GL_ARRAY_BUFFER, last_vbo);
    glad_glUseProgram(last_program);
    glad_glActiveTexture(last_active_texture);
    glad_glBlendFunc(last_blend_src, last_blend_dst);
    if (last_blend_enabled)
        glad_glEnable(GL_BLEND);
    else
        glad_glDisable(GL_BLEND);
    if (last_depth_enabled)
        glad_glEnable(GL_DEPTH_TEST);
    else
        glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc(last_depth_func);
    // if (last_srgb_enabled)
    //     _glEnable(GL_FRAMEBUFFER_SRGB);
}

void bindAndPosition(Crosshair *c)
{
    if (!c->texture)
        return;
    ;

    float x = c->x - c->width / 2.0f;
    float y = c->y - c->height / 2.0f;

    glad_glBindTexture(GL_TEXTURE_2D, c->texture);
    glad_glColor4f(1, 1, 1, 1);
    glad_glBegin(GL_QUADS);
    glad_glTexCoord2f(0, 0);
    glad_glVertex2f(x, y);
    glad_glTexCoord2f(1, 0);
    glad_glVertex2f(x + c->width, y);
    glad_glTexCoord2f(1, 1);
    glad_glVertex2f(x + c->width, y + c->height);
    glad_glTexCoord2f(0, 1);
    glad_glVertex2f(x, y + c->height);
    glad_glEnd();
}

void renderGsEvoCrosshairs(void)
{
    GLint texFormat = 0;
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &texFormat);

    if (texFormat == 0x1908 || texFormat == 0x1 || texFormat == 0x8051)
        return;

    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    // glad_glDisable(GL_DEPTH_TEST); // for HOD4
    glad_glDepthMask(GL_FALSE);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glOrtho(0, drawableW, drawableH, 0, -1, 1);
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();

    if (p1CrossHairInitialized && crossHair[0].visible)
        bindAndPosition(&crossHair[0]);

    if (p2CrossHairInitialized && crossHair[1].visible)
        bindAndPosition(&crossHair[1]);

    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glDisable(GL_BLEND);
    glad_glDisable(GL_TEXTURE_2D);
    glad_glDepthMask(GL_TRUE);
    glad_glEnable(GL_DEPTH_TEST);
    glad_glPopAttrib();
}

void destroyCrosshairs(void)
{
    stopPollingThread();
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (crossHair[i].texture)
            glad_glDeleteTextures(1, &crossHair[i].texture);
        if (crossHair[i].vao)
            glad_glDeleteVertexArrays(1, &crossHair[i].vao);
        if (crossHair[i].vbo)
            glad_glDeleteBuffers(1, &crossHair[i].vbo);
        if (crossHair[i].surface)
            SDL_DestroySurface(crossHair[i].surface);
    }
    if (gShaderProgram)
        glad_glDeleteProgram(gShaderProgram);
}

#ifdef __linux__
#undef glDrawArrays
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
#else
void bridgeglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
#endif

    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
    {
        GLint currentFBO = 0;
        glad_glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
        renderGsEvoCrosshairs();
    }
    if (!real_glDrawArrays)
#ifdef __linux__
       real_glDrawArrays = dlsym(RTLD_NEXT, "glDrawArrays");
#else
       real_glDrawArrays = (__stdcall void (*)(GLenum, GLint, GLsizei))SDL_GL_GetProcAddress("glDrawArrays");
#endif
    real_glDrawArrays(mode, first, count);
}

typedef struct
{
    bool keepRunning;
} PollingArgs;

PollingArgs gPollingArgs;

static void *gsevoPollingThreadFunc(void *arg)
{
    PollingArgs *args = (PollingArgs *)arg;

    while (args->keepRunning)
    {
        uint8_t p1Mode = *(uint8_t *)0x086617E8;
        uint8_t p2Mode = *(uint8_t *)0x08661994;
        if (p1Mode == 0x2)
            crossHair[0].visible = true;
        else
            crossHair[0].visible = false;

        if (p2Mode == 0x2)
            crossHair[1].visible = true;
        else
            crossHair[1].visible = false;

        SDL_Delay(10);
    }
    return NULL;
}

static void *pollingThreadFunc(void *arg)
{
    PollingArgs *args = (PollingArgs *)arg;
    uint32_t *pGunMgr = NULL;
    uint32_t *pPlayerMgr = NULL;

    if (gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA)
    {
        pGunMgr = (uint32_t *)0x0a711758;
        pPlayerMgr = (uint32_t *)0x0a7117a8;
    }
    else if (gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB || gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC)
    {
        pGunMgr = (uint32_t *)0x0a6f27a8;
        pPlayerMgr = (uint32_t *)0x0a6f27f8;
    }
    else if (gId == RAMBO_SBQL)
        pPlayerMgr = (uint32_t *)0x0842fe9c;
    else if (gId == RAMBO_SBSS_CHINA)
        pPlayerMgr = (uint32_t *)0x084304fc;
    else if (gId == THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB)
        pPlayerMgr = (uint32_t *)0x0A69F92C;
    else
        args->keepRunning = false;

    while (args->keepRunning)
    {
        if (gGrp != GROUP_HOD4 || *pGunMgr != 0x0)
        {
            uint8_t *gameMode;
            if (gGrp == GROUP_HOD4)
            {
                uint32_t *gameModeAddress = *(void **)pGunMgr + 0x2c;
                gameMode = *(uint8_t **)gameModeAddress + 0x38;
            }
            if (gGrp != GROUP_HOD4 || *gameMode == 8)
            {
                if (*pPlayerMgr != 0x0)
                {
                    uint32_t *p1ModeAddress = *(void **)pPlayerMgr + 0x34;
                    uint32_t *p2ModeAddress = *(void **)pPlayerMgr + 0x38;

                    if (*p1ModeAddress != 0x0)
                    {
                        uint8_t *p1Mode = *(uint8_t **)p1ModeAddress + 0x38;
                        if (*p1Mode == 3 || *p1Mode == 5)
                            crossHair[0].visible = true;
                        else
                            crossHair[0].visible = false;
                    }

                    if (*p2ModeAddress != 0x0)
                    {
                        uint8_t *p2Mode = *(uint8_t **)p2ModeAddress + 0x38;
                        if (*p2Mode == 3 || *p2Mode == 5)
                            crossHair[1].visible = true;
                        else
                            crossHair[1].visible = false;
                    }
                }
            }
        }
        SDL_Delay(10);
    }
    return NULL;
}

void startPollingThread()
{
    if (gPollingArgs.keepRunning)
        return;

    gPollingArgs.keepRunning = true;
    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
    {
#ifdef __linux__
        pthread_create(&pollingThreadId, NULL, gsevoPollingThreadFunc, &gPollingArgs);
#else
        emuPthreadCreate(&pollingThreadId, NULL, gsevoPollingThreadFunc, &gPollingArgs);
#endif
    }
    else
    {
#ifdef __linux__
        pthread_create(&pollingThreadId, NULL, pollingThreadFunc, &gPollingArgs);
#else
        emuPthreadCreate(&pollingThreadId, NULL, pollingThreadFunc, &gPollingArgs);
#endif
    }
}

void stopPollingThread()
{
    if (gPollingArgs.keepRunning)
    {
        gPollingArgs.keepRunning = false;
        if (pollingThreadId)
        {
#ifdef __linux__
            pthread_join(pollingThreadId, NULL);
#else
            emuPthreadJoin(pollingThreadId, NULL);
#endif
            pollingThreadId = 0;
        }
    }
}
