#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* GLHooks_GetProcAddress(const char* procName);
uint32_t GLHooks_ConsumeCompressedImageSize();

#ifdef __cplusplus
}
#endif
