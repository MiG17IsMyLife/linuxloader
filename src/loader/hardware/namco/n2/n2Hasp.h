#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reserves the virtual HASP entry points before a dependent ELF is relocated.
void n2RegisterHaspPreloadOverrides(void);

#ifdef __cplusplus
}
#endif
