#pragma once

#include <stdint.h>

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
/*
 * Runs the sequential shifter from the GearUp/GearDown bindings and reports
 * the selected gear, or 0 when the raw shifter switches are in use.
 * n2GearSwitchBits() turns that gear into the PLAYER_1 JVS switch bits the
 * cabinet's shifter is wired to.
 */
int n2UpdateShifter(void);
uint16_t n2GearSwitchBits(int gear);

/*
 * Raw count the cabinet's wheel or pedal potentiometer reports for a 0..1
 * position, as both the direct-write path and the JVS bridge have to publish
 * the same reading.
 */
enum
{
    N2_ANALOGUE_STEERING = 0,
    N2_ANALOGUE_ACCELERATOR = 1,
    N2_ANALOGUE_BRAKE = 2
};
uint16_t n2AnalogueCount(int channel, float normalized);

N2Game n2GetGame(void);
const char *n2GetGameTitle(void);
const char *n2GetGameShortTitle(void);
const char *n2GetGameId(void);
const char *n2GetRevision(void);

#ifdef __cplusplus
}
#endif
