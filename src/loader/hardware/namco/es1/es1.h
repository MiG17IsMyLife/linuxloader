#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int es1PrepareLoad(const char *elfPath);
int es1DetectGame(const char *elfPath);
int es1IsDetected(void);
int es1InstallHooks(void);
int es1InstallLateHooks(void);
int es1InitializeGraphics(void);
int es1HandleSystemCommand(const char *command);
const char *es1GetGameTitle(void);
const char *es1GetGameShortTitle(void);
const char *es1GetGameId(void);
const char *es1GetRevision(void);

#ifdef __cplusplus
}
#endif
