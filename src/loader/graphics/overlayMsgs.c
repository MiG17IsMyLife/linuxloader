#define GL_GLEXT_PROTOTYPES
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "overlayMsgs.h"
#include "../config/config.h"
#include "blitStretching.h"

extern const unsigned char LiberationMonoRegular_ttf[];
extern const unsigned int LiberationMonoRegular_ttf_length;

#define OVERLAY_MARGIN          16
#define OVERLAY_PAD_X           12
#define OVERLAY_PAD_Y           8
#define OVERLAY_FONT_SIZE       28
#define OVERLAY_MIN_WIDTH       200
#define OVERLAY_MIN_HEIGHT      50
#define OVERLAY_BG_R            50
#define OVERLAY_BG_G            50
#define OVERLAY_BG_B            50
#define OVERLAY_BG_A            128
#define OVERLAY_CORNER_R        4
#define TEXT_R                  57
#define TEXT_G                  255
#define TEXT_B                  20

#define FADE_IN_DURATION_MS     200
#define FADE_OUT_DURATION_MS    300

static const char *kVertSrc = "#version 120\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_tex_coord;\n"
    "varying vec2 v_tex_coord;\n"
    "uniform mat4 u_projection;\n"
    "uniform float u_fadeAlpha;\n"
    "void main() {\n"
    "    gl_Position = u_projection * vec4(a_pos.x, a_pos.y, 0.0, 1.0);\n"
    "    v_tex_coord = a_tex_coord;\n"
    "}\n";

static const char *kFragSrc = "#version 120\n"
    "varying vec2 v_tex_coord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_fadeAlpha;\n"
    "void main() {\n"
    "    vec4 texColor = texture2D(u_texture, v_tex_coord);\n"
    "    gl_FragColor = vec4(texColor.rgb, texColor.a * u_fadeAlpha);\n"
    "}\n";

typedef struct
{
    bool inUse;
    bool visible;
    char text[256];
    int durationMs;
    int remainingMs;
    OverlayPosition position;

    float fadeAlpha;
    bool fadingIn;
    bool fadingOut;

    bool textureDirty;

    GLuint texture;
    int texW, texH;
    GLuint vao;
    GLuint vbo;
} OverlaySlot;

typedef struct
{
    GLint lastProgram, lastVao, lastVbo, lastEbo, lastActiveTex, lastTexBind, lastDepthFunc;
    GLboolean lastBlendEnabled, lastDepthEnabled;
    GLint lastBlendSrc, lastBlendDst, lastBlendEq;
    GLint lastViewport[4];
    GLint lastDrawFbo, lastReadFbo;
    GLint lastUnpackAlign;
    GLboolean lastDepthMask;
    GLboolean lastScissorEnabled;
    GLboolean lastColorMask[4];
    GLfloat lastClearColor[4];
    GLint lastDrawBuffer, lastReadBuffer;
    GLint lastVertProgEnabled, lastFragProgEnabled, lastTex2dEnabled;
    GLint lastAttrib0Enabled, lastAttrib1Enabled;
    GLboolean lastClientVertex, lastClientTexCoord, lastClientNormal, lastClientColor;
    GLboolean lastStencilEnabled, lastAlphaTestEnabled, lastCullFaceEnabled;
} SavedOverlayState;

static bool gInitialized = false;
static bool gGlesMode = false;
static OverlaySlot gSlots[MAX_OVERLAY_SLOTS];

static bool gTtfOk = false;
static TTF_Font *gFont = NULL;

static GLuint gShaderProgram = 0;
static GLint gUProjectionLoc = -1;
static GLint gUTextureLoc = -1;
static GLint gUFadeAlphaLoc = -1;

static SDL_GLContext gLastRenderContext = NULL;

extern int renderWidth;
extern int renderHeight;
extern int drawableW;
extern int drawableH;
extern SDL_Window *g_SdlWindow;

static GLuint compileShaderFn(GLenum type, const char *source)
{
    GLuint shader = glad_glCreateShader(type);
    glad_glShaderSource(shader, 1, &source, NULL);
    glad_glCompileShader(shader);
    GLint success;
    glad_glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char info[512];
        glad_glGetShaderInfoLog(shader, 512, NULL, info);
        fprintf(stderr, "overlayMsgs: shader compile error: %s\n", info);
        glad_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint createShaderProgramFn(void)
{
    GLuint vert = compileShaderFn(GL_VERTEX_SHADER, kVertSrc);
    if (vert == 0) return 0;
    GLuint frag = compileShaderFn(GL_FRAGMENT_SHADER, kFragSrc);
    if (frag == 0)
    {
        glad_glDeleteShader(vert);
        return 0;
    }
    GLuint prog = glad_glCreateProgram();
    glad_glAttachShader(prog, vert);
    glad_glAttachShader(prog, frag);
    glad_glBindAttribLocation(prog, 0, "a_pos");
    glad_glBindAttribLocation(prog, 1, "a_tex_coord");
    glad_glLinkProgram(prog);
    GLint success;
    glad_glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info[512];
        glad_glGetProgramInfoLog(prog, 512, NULL, info);
        fprintf(stderr, "overlayMsgs: program link error: %s\n", info);
    }
    glad_glDeleteShader(vert);
    glad_glDeleteShader(frag);
    return prog;
}

static void destroySlotGlResources(OverlaySlot *slot)
{
    if (slot->texture != 0)
    {
        glad_glDeleteTextures(1, &slot->texture);
        slot->texture = 0;
    }
    if (slot->vao != 0)
    {
        glad_glDeleteVertexArrays(1, &slot->vao);
        slot->vao = 0;
    }
    if (slot->vbo != 0)
    {
        glad_glDeleteBuffers(1, &slot->vbo);
        slot->vbo = 0;
    }
    slot->texW = 0;
    slot->texH = 0;
}

static void createOverlayGeometry(GLuint vao, GLuint vbo, int w, int h)
{
    float hf = (float)h;
    float wf = (float)w;
    float verts[] = {
        0.0f, hf, 0.0f, 1.0f,
        wf,  hf, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        wf,  hf, 1.0f, 1.0f,
        wf, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f
    };
    glad_glBindVertexArray(vao);
    glad_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glad_glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glad_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glad_glEnableVertexAttribArray(0);
    glad_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glad_glEnableVertexAttribArray(1);
    glad_glBindBuffer(GL_ARRAY_BUFFER, 0);
    glad_glBindVertexArray(0);
}

static GLuint bakeTextToTexture(const char *text, TTF_Font *font, int padX, int padY,
                                 int minW, int minH, int bgR, int bgG, int bgB, int bgA,
                                 int cornerR, int textR, int textG, int textB,
                                 int *outW, int *outH)
{
    if (!font)
        return 0;

    SDL_Color fg = {(Uint8)textR, (Uint8)textG, (Uint8)textB, 255};
    size_t tlen = strlen(text);
    SDL_Surface *textSurf = TTF_RenderText_Blended(font, text, tlen, fg);
    if (!textSurf)
        return 0;

    SDL_Surface *textRGBA = SDL_ConvertSurface(textSurf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(textSurf);
    if (!textRGBA)
        return 0;

    int finalW = textRGBA->w + padX * 2;
    int finalH = textRGBA->h + padY * 2;
    if (finalW < minW) finalW = minW;
    if (finalH < minH) finalH = minH;

    int totalPixels = finalW * finalH;
    unsigned char *pixels = (unsigned char *)malloc(totalPixels * 4);
    if (!pixels)
    {
        SDL_DestroySurface(textRGBA);
        return 0;
    }

    int radius = cornerR;
    for (int row = 0; row < finalH; row++)
    {
        for (int col = 0; col < finalW; col++)
        {
            int dx = 0, dy = 0;
            if (col < radius && row < radius)
            {
                dx = radius - col - 1;
                dy = radius - row - 1;
            }
            else if (col >= finalW - radius && row < radius)
            {
                dx = col - (finalW - radius);
                dy = radius - row - 1;
            }
            else if (col < radius && row >= finalH - radius)
            {
                dx = radius - col - 1;
                dy = row - (finalH - radius);
            }
            else if (col >= finalW - radius && row >= finalH - radius)
            {
                dx = col - (finalW - radius);
                dy = row - (finalH - radius);
            }
            int distSq = dx * dx + dy * dy;
            unsigned char alpha = (distSq <= radius * radius) ? (unsigned char)bgA : 0;

            int i = (row * finalW + col) * 4;
            pixels[i + 0] = (unsigned char)bgR;
            pixels[i + 1] = (unsigned char)bgG;
            pixels[i + 2] = (unsigned char)bgB;
            pixels[i + 3] = alpha;
        }
    }

    unsigned char *textPx = (unsigned char *)textRGBA->pixels;
    int textPitch = textRGBA->pitch;
    for (int row = 0; row < textRGBA->h; row++)
    {
        for (int col = 0; col < textRGBA->w; col++)
        {
            int srcIdx = row * textPitch + col * 4;
            unsigned char sR = textPx[srcIdx + 0];
            unsigned char sG = textPx[srcIdx + 1];
            unsigned char sB = textPx[srcIdx + 2];
            unsigned char sA = textPx[srcIdx + 3];
            if (sA == 0)
                continue;

            int xOffset = (finalW - textRGBA->w) / 2;
            int dstRow = row + padY;
            int dstCol = col + xOffset;
            int dstIdx = (dstRow * finalW + dstCol) * 4;

            unsigned char dR = pixels[dstIdx + 0];
            unsigned char dG = pixels[dstIdx + 1];
            unsigned char dB = pixels[dstIdx + 2];
            unsigned char dA = pixels[dstIdx + 3];

            float sa = sA / 255.0f;
            float da = dA / 255.0f;
            float outA = sa + da * (1.0f - sa);
            if (outA > 0.0f)
            {
                pixels[dstIdx + 0] = (unsigned char)((sR * sa + dR * da * (1.0f - sa)) / outA);
                pixels[dstIdx + 1] = (unsigned char)((sG * sa + dG * da * (1.0f - sa)) / outA);
                pixels[dstIdx + 2] = (unsigned char)((sB * sa + dB * da * (1.0f - sa)) / outA);
                pixels[dstIdx + 3] = (unsigned char)(outA * 255.0f);
            }
        }
    }

    SDL_DestroySurface(textRGBA);

    GLuint tex;
    glad_glGenTextures(1, &tex);
    glad_glBindTexture(GL_TEXTURE_2D, tex);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalW, finalH, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    *outW = finalW;
    *outH = finalH;
    free(pixels);
    return tex;
}

static void ensureTtfReady(void)
{
    if (gTtfOk)
        return;

    if (!TTF_WasInit())
    {
        if (!TTF_Init())
            return;
    }

    SDL_IOStream *rw = SDL_IOFromConstMem(LiberationMonoRegular_ttf,
                                          LiberationMonoRegular_ttf_length);
    if (rw)
        gFont = TTF_OpenFontIO(rw, 1, (float)OVERLAY_FONT_SIZE);

    if (!gFont)
        return;

    TTF_SetFontStyle(gFont, TTF_STYLE_BOLD);
    gTtfOk = true;
}

static void clearGlErrors(void)
{
    while (glad_glGetError() != GL_NO_ERROR)
        ;
}

static int getScaledValue(int base, float scale)
{
    int val = (int)((float)base * scale);
    return val > 0 ? val : 1;
}

static void rebuildSlotTexture(OverlaySlot *slot, float scale)
{
    if (!gTtfOk || !gFont)
        return;

    if (slot->text[0] == '\0')
    {
        destroySlotGlResources(slot);
        return;
    }

    int fontSize = getScaledValue(OVERLAY_FONT_SIZE, scale);
    int padX = getScaledValue(OVERLAY_PAD_X, scale);
    int padY = getScaledValue(OVERLAY_PAD_Y, scale);
    int minW = getScaledValue(OVERLAY_MIN_WIDTH, scale);
    int minH = getScaledValue(OVERLAY_MIN_HEIGHT, scale);
    int cornerR = getScaledValue(OVERLAY_CORNER_R, scale);

    if (gFont)
        TTF_SetFontSize(gFont, (float)fontSize);

    destroySlotGlResources(slot);

    GLuint newTex = bakeTextToTexture(slot->text, gFont,
                                       padX, padY, minW, minH,
                                       OVERLAY_BG_R, OVERLAY_BG_G, OVERLAY_BG_B, OVERLAY_BG_A,
                                       cornerR,
                                       TEXT_R, TEXT_G, TEXT_B,
                                       &slot->texW, &slot->texH);
    if (newTex == 0)
        return;

    slot->texture = newTex;

    glad_glGenVertexArrays(1, &slot->vao);
    glad_glGenBuffers(1, &slot->vbo);
    createOverlayGeometry(slot->vao, slot->vbo, slot->texW, slot->texH);
}

static void overlaySaveState(SavedOverlayState *state)
{
    glad_glGetIntegerv(GL_CURRENT_PROGRAM, &state->lastProgram);
    glad_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->lastVao);
    glad_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->lastVbo);
    glad_glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state->lastEbo);
    glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &state->lastActiveTex);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->lastTexBind);
    glad_glGetIntegerv(GL_DEPTH_FUNC, &state->lastDepthFunc);
    glad_glGetIntegerv(GL_DRAW_BUFFER, &state->lastDrawBuffer);
    glad_glGetIntegerv(GL_READ_BUFFER, &state->lastReadBuffer);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state->lastDrawFbo);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->lastReadFbo);
    glad_glGetIntegerv(GL_VIEWPORT, state->lastViewport);
    glad_glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->lastUnpackAlign);
    glad_glGetBooleanv(GL_DEPTH_WRITEMASK, &state->lastDepthMask);
    glad_glGetBooleanv(GL_COLOR_WRITEMASK, state->lastColorMask);
    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, state->lastClearColor);
    state->lastBlendEnabled = glad_glIsEnabled(GL_BLEND);
    state->lastDepthEnabled = glad_glIsEnabled(GL_DEPTH_TEST);
    state->lastScissorEnabled = glad_glIsEnabled(GL_SCISSOR_TEST);
    state->lastStencilEnabled = glad_glIsEnabled(GL_STENCIL_TEST);
    state->lastAlphaTestEnabled = glad_glIsEnabled(GL_ALPHA_TEST);
    state->lastCullFaceEnabled = glad_glIsEnabled(GL_CULL_FACE);
    glad_glGetIntegerv(GL_BLEND_SRC_RGB, &state->lastBlendSrc);
    glad_glGetIntegerv(GL_BLEND_DST_RGB, &state->lastBlendDst);
    glad_glGetIntegerv(GL_BLEND_EQUATION_RGB, &state->lastBlendEq);
    state->lastVertProgEnabled = glad_glIsEnabled(GL_VERTEX_PROGRAM_ARB);
    state->lastFragProgEnabled = glad_glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    state->lastTex2dEnabled = glad_glIsEnabled(GL_TEXTURE_2D);
    glad_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state->lastAttrib0Enabled);
    glad_glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state->lastAttrib1Enabled);
    state->lastClientVertex = glad_glIsEnabled(GL_VERTEX_ARRAY);
    state->lastClientTexCoord = glad_glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    state->lastClientNormal = glad_glIsEnabled(GL_NORMAL_ARRAY);
    state->lastClientColor = glad_glIsEnabled(GL_COLOR_ARRAY);
}

static void overlayRestoreState(SavedOverlayState *state)
{
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)state->lastTexBind);
    glad_glBindVertexArray((GLuint)state->lastVao);
    glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)state->lastEbo);
    glad_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->lastVbo);
    glad_glUseProgram((GLuint)state->lastProgram);
    glad_glActiveTexture((GLenum)state->lastActiveTex);
    glad_glPixelStorei(GL_UNPACK_ALIGNMENT, state->lastUnpackAlign);

    if (state->lastAttrib0Enabled)
        glad_glEnableVertexAttribArray(0);
    else
        glad_glDisableVertexAttribArray(0);
    if (state->lastAttrib1Enabled)
        glad_glEnableVertexAttribArray(1);
    else
        glad_glDisableVertexAttribArray(1);

    if (state->lastClientVertex)
        glad_glEnableClientState(GL_VERTEX_ARRAY);
    else
        glad_glDisableClientState(GL_VERTEX_ARRAY);
    if (state->lastClientTexCoord)
        glad_glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    else
        glad_glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (state->lastClientNormal)
        glad_glEnableClientState(GL_NORMAL_ARRAY);
    else
        glad_glDisableClientState(GL_NORMAL_ARRAY);
    if (state->lastClientColor)
        glad_glEnableClientState(GL_COLOR_ARRAY);
    else
        glad_glDisableClientState(GL_COLOR_ARRAY);

    glad_glBlendFunc((GLenum)state->lastBlendSrc, (GLenum)state->lastBlendDst);
    glad_glBlendEquation((GLenum)state->lastBlendEq);
    if (state->lastBlendEnabled)
        glad_glEnable(GL_BLEND);
    else
        glad_glDisable(GL_BLEND);
    if (state->lastDepthEnabled)
        glad_glEnable(GL_DEPTH_TEST);
    else
        glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc((GLenum)state->lastDepthFunc);
    glad_glDepthMask(state->lastDepthMask);
    if (state->lastScissorEnabled)
        glad_glEnable(GL_SCISSOR_TEST);
    else
        glad_glDisable(GL_SCISSOR_TEST);
    if (state->lastStencilEnabled)
        glad_glEnable(GL_STENCIL_TEST);
    else
        glad_glDisable(GL_STENCIL_TEST);
    if (state->lastAlphaTestEnabled)
        glad_glEnable(GL_ALPHA_TEST);
    else
        glad_glDisable(GL_ALPHA_TEST);
    if (state->lastCullFaceEnabled)
        glad_glEnable(GL_CULL_FACE);
    else
        glad_glDisable(GL_CULL_FACE);
    if (state->lastVertProgEnabled)
        glad_glEnable(GL_VERTEX_PROGRAM_ARB);
    else
        glad_glDisable(GL_VERTEX_PROGRAM_ARB);
    if (state->lastFragProgEnabled)
        glad_glEnable(GL_FRAGMENT_PROGRAM_ARB);
    else
        glad_glDisable(GL_FRAGMENT_PROGRAM_ARB);
    if (state->lastTex2dEnabled)
        glad_glEnable(GL_TEXTURE_2D);
    else
        glad_glDisable(GL_TEXTURE_2D);
    glad_glClearColor(state->lastClearColor[0], state->lastClearColor[1],
                      state->lastClearColor[2], state->lastClearColor[3]);
    glad_glColorMask(state->lastColorMask[0], state->lastColorMask[1],
                     state->lastColorMask[2], state->lastColorMask[3]);
    glad_glDrawBuffer((GLenum)state->lastDrawBuffer);
    glad_glReadBuffer((GLenum)state->lastReadBuffer);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->lastReadFbo);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state->lastDrawFbo);
    glad_glViewport(state->lastViewport[0], state->lastViewport[1],
                    state->lastViewport[2], state->lastViewport[3]);
}

void overlayInit(void)
{
    if (gInitialized)
        return;
    gInitialized = true;

    for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
    {
        gSlots[i].inUse = false;
        gSlots[i].visible = false;
        gSlots[i].text[0] = '\0';
        gSlots[i].texture = 0;
        gSlots[i].vao = 0;
        gSlots[i].vbo = 0;
    }
}

void overlayDestroy(void)
{
    for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
        destroySlotGlResources(&gSlots[i]);

    if (gShaderProgram != 0)
    {
        glad_glDeleteProgram(gShaderProgram);
        gShaderProgram = 0;
    }
    if (gFont != 0)
    {
        TTF_CloseFont(gFont);
        gFont = 0;
    }
    if (gTtfOk)
    {
        TTF_Quit();
        gTtfOk = false;
    }
    gInitialized = false;
}

int overlayShowMessage(const char *text, int durationMs, OverlayPosition pos)
{
    if (!gInitialized)
        overlayInit();

    int slotIdx = -1;
    for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
    {
        if (!gSlots[i].inUse)
        {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx < 0)
        return -1;

    OverlaySlot *slot = &gSlots[slotIdx];
    memset(slot, 0, sizeof(OverlaySlot));
    strncpy(slot->text, text, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    slot->inUse = true;
    slot->visible = true;
    slot->durationMs = durationMs;
    slot->remainingMs = durationMs;
    slot->position = pos;
    slot->fadeAlpha = 0.0f;
    slot->fadingIn = true;
    slot->fadingOut = false;
    slot->textureDirty = true;

    return slotIdx;
}

void overlayHideMessage(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= MAX_OVERLAY_SLOTS)
        return;
    if (!gSlots[slotIndex].inUse)
        return;

    OverlaySlot *slot = &gSlots[slotIndex];
    if (slot->durationMs > 0)
    {
        slot->fadingOut = true;
        slot->fadingIn = false;
    }
    else
    {
        slot->visible = false;
    }
}

void overlayUpdateMessageText(int slotIndex, const char *text)
{
    if (slotIndex < 0 || slotIndex >= MAX_OVERLAY_SLOTS)
        return;
    if (!gSlots[slotIndex].inUse)
        return;

    OverlaySlot *slot = &gSlots[slotIndex];

    if (strcmp(slot->text, text) == 0)
        return;

    strncpy(slot->text, text, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    slot->textureDirty = true;
}

void overlaySetMessageVisible(int slotIndex, bool visible)
{
    if (slotIndex < 0 || slotIndex >= MAX_OVERLAY_SLOTS)
        return;
    OverlaySlot *slot = &gSlots[slotIndex];
    if (!slot->inUse)
        return;

    if (visible)
    {
        slot->fadingIn = true;
        slot->fadingOut = false;
        slot->fadeAlpha = 0.0f;
    }
    else
    {
        slot->fadingOut = true;
        slot->fadingIn = false;
    }
}

bool overlayIsMessageVisible(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= MAX_OVERLAY_SLOTS)
        return false;
    return gSlots[slotIndex].inUse && gSlots[slotIndex].visible;
}

void overlayRender(void)
{
    SDL_GLContext currentCtx = SDL_GL_GetCurrentContext();
    if (!currentCtx)
        return;

    if (currentCtx != gLastRenderContext)
    {
        for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
        {
            gSlots[i].texture = 0;
            gSlots[i].texW = 0;
            gSlots[i].texH = 0;
            gSlots[i].vao = 0;
            gSlots[i].vbo = 0;
        }
        gShaderProgram = 0;
        gUProjectionLoc = -1;
        gUTextureLoc = -1;
        gUFadeAlphaLoc = -1;
        gLastRenderContext = currentCtx;
    }

    if (!gInitialized)
        overlayInit();

    ensureTtfReady();
    clearGlErrors();

    static uint64_t sLastTime = 0;
    uint64_t now = SDL_GetTicks();
    if (sLastTime == 0)
        sLastTime = now;
    int deltaMs = (int)(now - sLastTime);
    sLastTime = now;

    if (deltaMs > 0)
    {
        for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
        {
            OverlaySlot *slot = &gSlots[i];
            if (!slot->inUse)
                continue;

            if (slot->fadingIn)
            {
                slot->fadeAlpha += (float)deltaMs / (float)FADE_IN_DURATION_MS;
                if (slot->fadeAlpha >= 1.0f)
                {
                    slot->fadeAlpha = 1.0f;
                    slot->fadingIn = false;
                }

                if (!slot->visible && slot->fadeAlpha > 0.0f)
                    slot->visible = true;
            }

            if (slot->durationMs > 0)
            {
                slot->remainingMs -= deltaMs;
                if (slot->remainingMs <= 0 && !slot->fadingOut)
                {
                    slot->fadingOut = true;
                    slot->fadingIn = false;
                }
            }

            if (slot->fadingOut)
            {
                slot->fadeAlpha -= (float)deltaMs / (float)FADE_OUT_DURATION_MS;
                if (slot->fadeAlpha <= 0.0f)
                {
                    slot->fadeAlpha = 0.0f;
                    slot->fadingOut = false;
                    slot->visible = false;

                    if (slot->durationMs > 0 && slot->remainingMs <= 0)
                    {
                        destroySlotGlResources(slot);
                        memset(slot, 0, sizeof(OverlaySlot));
                    }
                }
            }
        }
    }

    int vpW = drawableW;
    int vpH = drawableH;
    if (vpW <= 1 || vpH <= 1)
    {
        SDL_GetWindowSizeInPixels(g_SdlWindow, &vpW, &vpH);
        if (vpW <= 0 || vpH <= 0)
            return;
    }

    if (gShaderProgram == 0)
    {
        gShaderProgram = createShaderProgramFn();
        if (gShaderProgram != 0)
        {
            gUProjectionLoc = glad_glGetUniformLocation(gShaderProgram, "u_projection");
            gUTextureLoc = glad_glGetUniformLocation(gShaderProgram, "u_texture");
            gUFadeAlphaLoc = glad_glGetUniformLocation(gShaderProgram, "u_fadeAlpha");
        }
    }
    if (gShaderProgram == 0)
        return;

    SavedOverlayState savedState;
    overlaySaveState(&savedState);

    glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glad_glViewport(0, 0, vpW, vpH);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc(GL_ALWAYS);
    glad_glDepthMask(GL_FALSE);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glBlendEquation(GL_FUNC_ADD);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_VERTEX_PROGRAM_ARB);
    glad_glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glad_glDisable(GL_TEXTURE_2D);
    glad_glDrawBuffer(GL_BACK);
    glad_glReadBuffer(GL_BACK);
    glad_glUseProgram(gShaderProgram);
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glUniform1i(gUTextureLoc, 0);

    float scale = 1.0f;
    float margin = (float)getScaledValue(OVERLAY_MARGIN, scale);

    int areaX, areaY, areaW, areaH;
   
    if (getConfig()->crc32 == PRIMEVAL_HUNT_SBPP && getConfig()->phScreenMode != 0)
    {
        extern int phX, phY2, phW, phH;
        areaX = phX;
        switch (getConfig()->phScreenMode)
        {
            case 1:
                areaY = phY2;
                break;
            case 2:
                areaY = phY2;
                break;
            case 3:
                areaY = 0;
                break;
            case 4:
                areaY = 0;
                break;;
        }
        areaW = phW;
        areaH = phH;
    }
    else
    {
        areaX = dest.X;
        areaY = dest.Y;
        areaW = dest.W > 0 ? dest.W : vpW;
        areaH = dest.H > 0 ? dest.H : vpH;
        if (areaW <= 0 || areaH <= 0)
        {
            areaW = vpW;
            areaH = vpH;
        }
    }

    for (int i = 0; i < MAX_OVERLAY_SLOTS; i++)
    {
        OverlaySlot *slot = &gSlots[i];
        if (!slot->inUse || !slot->visible)
            continue;

        if (slot->textureDirty || (slot->texture == 0 && slot->text[0] != '\0'))
        {
            slot->textureDirty = false;

            GLint prevTexBind, prevUnpackAlign, prevActiveTex;
            glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexBind);
            glad_glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlign);
            glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
            glad_glActiveTexture(GL_TEXTURE0);

            rebuildSlotTexture(slot, scale);

            glad_glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexBind);
            glad_glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlign);
            glad_glActiveTexture((GLenum)prevActiveTex);
        }

        if (slot->texture == 0 || slot->vao == 0)
            continue;

        int overlayW = slot->texW;
        int overlayH = slot->texH;
        float rectX, rectY;
        switch (slot->position)
        {
            case OVERLAY_TOP_LEFT:
                rectX = (float)areaX + margin;
                rectY = (float)areaY + margin;
                break;
            case OVERLAY_TOP_RIGHT:
                rectX = (float)(areaX + areaW) - (float)overlayW - margin;
                rectY = (float)areaY + margin;
                break;
            case OVERLAY_BOTTOM_LEFT:
                rectX = (float)areaX + margin;
                rectY = (float)(areaY + areaH) - (float)overlayH - margin;
                break;
            case OVERLAY_BOTTOM_RIGHT:
                rectX = (float)(areaX + areaW) - (float)overlayW - margin;
                rectY = (float)(areaY + areaH) - (float)overlayH - margin;
                break;
            default:
                rectX = (float)areaX + margin;
                rectY = (float)areaY + margin;
                break;
        }

        float projection[16] = {0.0f};
        projection[0] = 2.0f / vpW;
        projection[5] = -2.0f / vpH;
        projection[10] = -1.0f;
        projection[12] = -1.0f + (rectX * projection[0]);
        projection[13] = 1.0f + (rectY * projection[5]);
        projection[15] = 1.0f;
        glad_glUniformMatrix4fv(gUProjectionLoc, 1, GL_FALSE, projection);

        glad_glUniform1f(gUFadeAlphaLoc, slot->fadeAlpha);
        glad_glActiveTexture(GL_TEXTURE0);
        glad_glBindTexture(GL_TEXTURE_2D, slot->texture);
        glad_glBindVertexArray(slot->vao);
        glad_glEnableVertexAttribArray(0);
        glad_glEnableVertexAttribArray(1);
        glad_glDrawArrays(GL_TRIANGLES, 0, 6);
        glad_glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    clearGlErrors();
    overlayRestoreState(&savedState);
}
