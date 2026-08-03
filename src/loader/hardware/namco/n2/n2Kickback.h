#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The steering board - clKickback - on its own serial line, /dev/ttyM1 at
 * 9600 8N1. The game sends a fixed ten byte frame starting 0xFF 0xFF and reads
 * exactly three ASCII characters back.
 *
 * Everything turns on clKickback's this->0x2c (offsets from the 3DX+ JP binary):
 *
 *   0  idle. send() transmits here, and waitOnPower finishes here.
 *   1  motor stopped. waitOffPower finishes here; exec() clears the self check
 *      bit when it sees it.
 *   3  a request is out and a reply is expected.
 *   4  latched error, with this->0x70 set. Nothing recovers from it.
 *
 * Three rules follow:
 *
 *   - Answer every frame with "E00". A silent board is an error, not a slow
 *     one: send() writes "E20" after six hundred unanswered frames, and
 *     receive() has its own timeout onto the same error.
 *   - One reply per frame, replacing rather than queueing, or the game ends up
 *     reading answers to requests it made minutes ago.
 *   - A volunteered reply - a power report or the self check answer - waits
 *     until this->0x2c is not 0. decordResultCode() refuses a "C06" at 0 while
 *     the self check bit is up by writing "E20" itself. The wait is short:
 *     exec() puts the field back to 3 after a power ramp, and waitSelfCheck
 *     rewrites it to 3 while it polls.
 *
 * Output is driven from n2SteeringIo.cpp; this file is only the wire.
 */
int n2KickbackSerialEnabled(void);

// clKickback's singleton, which this bridge reads this->0x2c out of.
void n2KickbackSetInstance(void *const *instance);

// "C01" or "C06", what waitOnPower and waitOffPower each block on.
void n2KickbackReportMotorPower(int running);

/*
 * Stage "E00" then "C06", driven from clKickback::requestSelfCheck().
 *
 * The request is published as bit 7 of the JVS general purpose output, but it
 * is a pulse about one frame wide, so polling for it from the read path only
 * catches it when the game happens to pause for longer than the pulse - which
 * it does once during start up and never again. The call is not a coincidence.
 */
void n2KickbackReportSelfCheck(void);

int n2KickbackSerialOpen(const char *path, int flags);
int n2KickbackSerialIsDescriptor(int fd);

int n2KickbackSerialBytesAvailable(int fd);
int n2KickbackSerialRead(int fd, void *buffer, size_t count);
int n2KickbackSerialWrite(int fd, const void *buffer, size_t count);
int n2KickbackSerialClose(int fd);
int n2KickbackSerialIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
