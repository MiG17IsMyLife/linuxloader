#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The steering board - clKickback, the one that drives the wheel's force
 * feedback - is not on the JVS bus. It has its own line, and this bridge stands
 * in for it the way n2CardReader does for the reader and n2Jvio does for the
 * JVIO board.
 *
 * What is known about it, from WMMT3's own code:
 *
 *   Port      /dev/ttyM1, 9600 8N1. clKickback builds a clSerialN2 with a
 *             clSerialParam whose first field is the port index, and passes 1.
 *   Request   A fixed ten byte frame from clKickback::send(), starting 0xFF
 *             0xFF and carrying the force feedback state. Sent only when
 *             clKickback::requestTrans() has set its "something changed" flag,
 *             which the setSpring/setViosity/setReflect/setVibrate setters do -
 *             so a cabinet with force feedback switched off never sends at all.
 *   Reply     Exactly three ASCII characters, read back with receive(buf, 3).
 *
 * clKickback::decordResultCode() reads the reply as <class><a><b>:
 *
 *   'C'  a command acknowledgement. "C0?" is accepted; within that, "C01" and
 *        "C06" carry state - "C06" sets the field that stops further sends -
 *        and any other third character is only logged.
 *   'E'  a status report, formatted through "strpcb(e) result code is %c%c%c".
 *        "E00" is the no-error form that clKickback::getPCBError() checks byte
 *        for byte; other codes latch an error and put the cabinet into PCB
 *        ERROR on the test menu's I/F INITIALIZE screen.
 *
 * Not yet worked out, and what a force feedback implementation needs next:
 *
 *   - The layout of the ten byte request past the 0xFF 0xFF header. Bytes 2, 3,
 *     4, 6 and 8 are written from clKickback state (one of them through a float
 *     conversion, so at least one is a scaled magnitude); 5, 7 and 9 are left
 *     at zero by the initialiser.
 *   - Which reply each request expects. The bridge answers everything the same
 *     way, which is enough for a cabinet that never transmits.
 *   - The self check. clKickback::requestSelfCheck() does not go through this
 *     port at all - it calls slot 0x60 on the object at 0x9bc54dc - so with
 *     force feedback enabled the cabinet reports PCB ERROR before a single
 *     frame reaches this bridge. That path has to be understood before force
 *     feedback can be turned on, and it is the reason the game is currently
 *     run with its own force feedback setting off.
 *
 * The other half of the board already exists: n2SteeringIo.cpp hooks
 * clKickback's setters and keeps the force feedback state in an
 * N2SteeringOutputState for a device backend to consume. That is where output
 * should be driven from; this file is only the wire the game talks to.
 */
int n2KickbackSerialEnabled(void);

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
