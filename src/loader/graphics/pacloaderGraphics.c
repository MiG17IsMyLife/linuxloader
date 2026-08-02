#include "pacloaderGraphics.h"

#if defined(PACLOADER_BUILD)

#include <stddef.h>

#include "../config/config.h"
#include "../log/log.h"

/*
 * N2/ES1 games do not need the Lindbergh per-title shader rewriting.  Keep
 * the two Cg hooks as transparent calls because the ELF resolver uses them to
 * retain the address of the function supplied by the game's Cg runtime.
 */
void *real_cgCreateProgram = NULL;
void *real_cgGetProgramString = NULL;

char *bridgeCgCreateProgram(uint32_t context, int programType, const char *program,
                            int profile, const char *entry, const char **args)
{
    typedef char *(*CgCreateProgram)(uint32_t, int, const char *, int,
                                     const char *, const char **);
    CgCreateProgram createProgram = (CgCreateProgram)real_cgCreateProgram;
    return createProgram ? createProgram(context, programType, program, profile, entry, args) : NULL;
}

char *bridgeCgGetProgramString(char *program, int parameter)
{
    typedef char *(*CgGetProgramString)(char *, int);
    CgGetProgramString getProgramString = (CgGetProgramString)real_cgGetProgramString;
    return getProgramString ? getProgramString(program, parameter) : NULL;
}

void bridgeglEnable(GLenum capability)
{
    if (glad_glEnable)
        glad_glEnable(capability);
}

void bridgeglDisable(GLenum capability)
{
    if (glad_glDisable)
        glad_glDisable(capability);
}

void bridgeglBindTexture(GLenum target, GLuint texture)
{
    if (glad_glBindTexture)
        glad_glBindTexture(target, texture);
}

/* NV_fence is absent on many AMD and Intel drivers.  Present the NV API the
 * guest expects while backing it with core OpenGL sync objects. */
#define PACLOADER_MAX_FENCES 256

typedef struct
{
    GLuint id;
    GLsync sync;
} PacloaderFence;

static PacloaderFence fences[PACLOADER_MAX_FENCES];
static GLuint nextFenceId = 1;

void pacGraphicsReportCapabilities(void)
{
    const char *vendor = (const char *)glad_glGetString(GL_VENDOR);
    const char *renderer = (const char *)glad_glGetString(GL_RENDERER);
    const char *version = (const char *)glad_glGetString(GL_VERSION);
    log_info("OpenGL compatibility: vendor=%s renderer=%s version=%s",
             vendor ? vendor : "unknown", renderer ? renderer : "unknown",
             version ? version : "unknown");
    log_info("OpenGL compatibility: NV_fence=%s, core sync=%s, S3TC=%s",
             glad_glGenFencesNV ? "native" : "emulated",
             (glad_glFenceSync && glad_glClientWaitSync) ? "yes" : "no",
             GLAD_GL_EXT_texture_compression_s3tc ? "yes" : "no");

    if (!glad_glFenceSync && !glad_glGenFencesNV)
        log_warn("OpenGL compatibility: this driver has neither NV_fence nor core sync support");
}

static int useNativeNvFences(void)
{
    return getConfig()->GPUVendor == NVIDIA_GPU && glad_glGenFencesNV &&
           glad_glDeleteFencesNV && glad_glSetFenceNV && glad_glTestFenceNV &&
           glad_glFinishFenceNV && glad_glIsFenceNV;
}

static PacloaderFence *findFence(GLuint id)
{
    for (size_t i = 0; i < PACLOADER_MAX_FENCES; ++i)
    {
        if (fences[i].id == id)
            return &fences[i];
    }
    return NULL;
}

static PacloaderFence *allocateFence(GLuint id)
{
    for (size_t i = 0; i < PACLOADER_MAX_FENCES; ++i)
    {
        if (fences[i].id == 0)
        {
            fences[i].id = id;
            fences[i].sync = NULL;
            return &fences[i];
        }
    }
    return NULL;
}

void bridgeglGenFencesNV(GLsizei count, GLuint *output)
{
    if (useNativeNvFences())
    {
        glad_glGenFencesNV(count, output);
        return;
    }

    for (GLsizei i = 0; i < count; ++i)
    {
        GLuint id = nextFenceId++;
        if (id == 0)
            id = nextFenceId++;
        output[i] = allocateFence(id) ? id : 0;
    }
}

void bridgeglDeleteFencesNV(GLsizei count, const GLuint *ids)
{
    if (useNativeNvFences())
    {
        glad_glDeleteFencesNV(count, ids);
        return;
    }

    for (GLsizei i = 0; i < count; ++i)
    {
        PacloaderFence *fence = findFence(ids[i]);
        if (!fence)
            continue;
        if (fence->sync && glad_glDeleteSync)
            glad_glDeleteSync(fence->sync);
        fence->id = 0;
        fence->sync = NULL;
    }
}

void bridgeglSetFenceNV(GLuint id, GLenum condition)
{
    (void)condition;
    if (useNativeNvFences())
    {
        glad_glSetFenceNV(id, condition);
        return;
    }

    PacloaderFence *fence = findFence(id);
    if (!fence || !glad_glFenceSync)
        return;
    if (fence->sync && glad_glDeleteSync)
        glad_glDeleteSync(fence->sync);
    fence->sync = glad_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

GLboolean bridgeglTestFenceNV(GLuint id)
{
    if (useNativeNvFences())
        return glad_glTestFenceNV(id);

    PacloaderFence *fence = findFence(id);
    if (!fence || !fence->sync || !glad_glClientWaitSync)
        return GL_TRUE;
    GLenum status = glad_glClientWaitSync(fence->sync, 0, 0);
    return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
}

void bridgeglFinishFenceNV(GLuint id)
{
    if (useNativeNvFences())
    {
        glad_glFinishFenceNV(id);
        return;
    }

    PacloaderFence *fence = findFence(id);
    if (fence && fence->sync && glad_glClientWaitSync)
        glad_glClientWaitSync(fence->sync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
}

GLboolean bridgeglIsFenceNV(GLuint id)
{
    if (useNativeNvFences())
        return glad_glIsFenceNV(id);
    return findFence(id) ? GL_TRUE : GL_FALSE;
}

#endif
