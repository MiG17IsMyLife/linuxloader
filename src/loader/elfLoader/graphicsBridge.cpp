#include "graphicsBridge.hpp"

#include "../graphics/gluBridge.h"
#include "../graphics/pacloaderGraphics.h"
#include "../log/log.h"
#include "symbolResolver.hpp"
#include "glHooks.hpp"
#include "x11Bridge.hpp"
#include "glxBridge.hpp"

namespace Es1CompatBridge
{
void initBridges();
}
namespace AlsaCompatBridge
{
void initBridges();
}

#define MAP(name, function) \
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function))
#define MAP_WITH_ORIGINAL(name, function, original) \
    SymbolResolver::GetInstance().RegisterVTable( \
        name, reinterpret_cast<void *>(function), reinterpret_cast<void **>(original))

namespace GraphicsBridge
{
void initBridges()
{
    log_info("Initializing pacloader graphics bridges");
    MAP("gluPerspective", bridgeGluPerspective);
    MAP("gluLookAt", bridgeGluLookAt);
    MAP("gluOrtho2D", bridgeGluOrtho2D);
    MAP("gluErrorString", bridgeGluErrorString);
    MAP_WITH_ORIGINAL("cgCreateProgram", bridgeCgCreateProgram, &real_cgCreateProgram);
    MAP_WITH_ORIGINAL("cgGetProgramString", bridgeCgGetProgramString, &real_cgGetProgramString);
    X11Bridge::initBridges();
    GlxBridge::initBridges();
    Es1CompatBridge::initBridges();
    AlsaCompatBridge::initBridges();

    /* Register the target's GL imports through the existing GLAD-backed hook
     * table. Native opengl32 only exports the old 1.1 surface. */
    static const char *const glNames[] = {
        "glActiveTexture", "glAlphaFunc", "glAttachObjectARB", "glAttachShader",
        "glBegin", "glBeginQueryARB", "glBindBuffer", "glBindFramebufferEXT",
        "glBindRenderbufferEXT", "glBindTexture", "glBlendColor", "glBlendEquation",
        "glBlendFunc", "glBlendFuncSeparate", "glBlitFramebufferEXT", "glBufferData",
        "glBufferSubData", "glCheckFramebufferStatusEXT", "glClear", "glClearColor",
        "glClearDepth", "glClearStencil", "glClientActiveTexture", "glColor4f",
        "glColorMask", "glColorMaterial", "glColorPointer", "glCompileShader",
        "glCompileShaderARB", "glCompressedTexImage2D", "glCreateProgram",
        "glCreateProgramObjectARB", "glCreateShader", "glCreateShaderObjectARB",
        "glCullFace", "glDeleteBuffers", "glDeleteFramebuffersEXT", "glDeleteObjectARB",
        "glDeleteProgram", "glDeleteQueriesARB", "glDeleteRenderbuffersEXT",
        "glDeleteShader", "glDeleteTextures", "glDepthFunc", "glDepthMask", "glDisable",
        "glDisableClientState", "glDrawArrays", "glDrawBuffer", "glDrawBuffers",
        "glDrawElements", "glEnable", "glEnableClientState", "glEnd", "glEndQueryARB",
        "glFinish", "glFlush", "glFogf", "glFogfv", "glFramebufferRenderbufferEXT",
        "glFramebufferTexture2DEXT", "glGenBuffers", "glGenerateMipmapEXT",
        "glGenFramebuffersEXT", "glGenQueriesARB", "glGenRenderbuffersEXT", "glGenTextures",
        "glGetError", "glGetInfoLogARB", "glGetIntegerv", "glGetObjectParameterivARB",
        "glGetProgramInfoLog", "glGetProgramiv", "glGetQueryiv", "glGetQueryObjectivARB",
        "glGetQueryObjectuivARB", "glGetShaderInfoLog", "glGetShaderiv", "glGetUniformLocation",
        "glHint", "glLightf", "glLightfv", "glLineStipple", "glLinkProgram", "glLoadMatrixf",
        "glMaterialfv", "glMatrixMode", "glMultiDrawArrays", "glMultiDrawElements",
        "glMultiTexCoord2f", "glNormal3f", "glNormalPointer", "glPixelStorei",
        "glPointParameterf", "glPointParameterfv", "glPointSize", "glPopAttrib",
        "glPopMatrix", "glPushAttrib", "glPushMatrix", "glReadPixels", "glRenderbufferStorageMultisampleEXT",
        "glSecondaryColor3f", "glShadeModel", "glShaderSource", "glShaderSourceARB",
        "glStencilFunc", "glStencilFuncSeparate", "glStencilOpSeparate", "glTexCoord2f",
        "glTexCoord3f", "glTexCoordPointer", "glTexEnvf", "glTexEnvfv", "glTexEnvi",
        "glTexGeni", "glTexImage2D", "glTexParameterf", "glTexParameteri", "glTexSubImage2D",
        "glUniform1f", "glUniform1fv", "glUniform1i", "glUniform1iv", "glUniform2f",
        "glUniform2fv", "glUniform2i", "glUniform2iv", "glUniform3f", "glUniform3fv",
        "glUniform3i", "glUniform3iv", "glUniform4f", "glUniform4fv", "glUniform4i",
        "glUniform4iv", "glUniformMatrix2fv", "glUniformMatrix3fv", "glUniformMatrix4fv",
        "glUseProgram", "glUseProgramObjectARB", "glVertex2f", "glVertex3f", "glVertexPointer",
        "glViewport"};
    for (const char *name : glNames)
    {
        if (void *function = GLHooks_GetProcAddress(name))
            MAP(name, function);
    }
}
} // namespace GraphicsBridge
