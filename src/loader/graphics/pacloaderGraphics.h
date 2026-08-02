#pragma once

#include <glad/gl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void *real_cgCreateProgram;
extern void *real_cgGetProgramString;

char *bridgeCgCreateProgram(uint32_t context, int programType, const char *program,
                            int profile, const char *entry, const char **args);
char *bridgeCgGetProgramString(char *program, int parameter);

void bridgeglEnable(GLenum capability);
void bridgeglDisable(GLenum capability);
void bridgeglBindTexture(GLenum target, GLuint texture);

void bridgeglGenFencesNV(GLsizei count, GLuint *fences);
void bridgeglDeleteFencesNV(GLsizei count, const GLuint *fences);
void bridgeglSetFenceNV(GLuint fence, GLenum condition);
GLboolean bridgeglTestFenceNV(GLuint fence);
void bridgeglFinishFenceNV(GLuint fence);
GLboolean bridgeglIsFenceNV(GLuint fence);
void pacGraphicsReportCapabilities(void);

#ifdef __cplusplus
}
#endif
