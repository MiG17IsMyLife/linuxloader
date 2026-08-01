#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    N2_GAME_NONE = 0,
    N2_GAME_WMMT3,
    N2_GAME_WMMT3DX,
    N2_GAME_WMMT3DX_PLUS,
    N2_GAME_WMMT3_FAMILY,
    N2_GAME_CSNEO
} N2Game;

// True for the Wangan Midnight titles, which share an engine and a cabinet
int n2IsWanganTitle(void);

// The path is needed because not every N2 title can be recognised from its
// symbols: CSNeo executable is a stripped launcher.
int n2DetectGame(const char *elfPath);
int n2IsDetected(void);
int n2InstallHooks(void);
int n2InstallAdmHooks(void);
int n2InitializeGraphics(void);
int n2HandleSystemCommand(const char *command);
N2Game n2GetGame(void);
const char *n2GetGameTitle(void);
const char *n2GetGameShortTitle(void);
const char *n2GetGameId(void);
const char *n2GetRevision(void);

#ifdef __cplusplus
}
#endif
