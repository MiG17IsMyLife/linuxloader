#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CS Neo uses a stripped launcher, so it is identified from its installation
// layout before the ELF and engine module are loaded.
int n2CsNeoLooksLikeGame(const char *elfPath);
int n2CsNeoPrepareLoad(const char *elfPath);

// Installs the title-specific compatibility layer after game detection.
int n2CsNeoInstallHooks(void);

// Handles CS Neo host shortcuts. Returns non-zero when the key was consumed.
int n2CsNeoHandleHostKey(int key, uint32_t modifiers);

#ifdef __cplusplus
}
#endif
