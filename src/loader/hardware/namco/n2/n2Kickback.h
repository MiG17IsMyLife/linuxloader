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
 * The self check does not arrive on this port. clKickback::sendJVS() copies
 * this->0x30 into the JVS general purpose output byte at 0x97ef768, and bit 7
 * of it is the self check request that clKickback::requestSelfCheck() raises.
 * While that bit is set clKickback::send() deliberately transmits nothing - it
 * only spins a retry counter - so no frame reaches this bridge, which is why
 * the port looks idle rather than broken during start up.
 *
 * The board is expected to answer anyway. clKickback::waitSelfCheck() polls
 * receive() and only advances once decordResultCode() has cleared this->0x2c
 * to zero; if that has not happened before its countdown expires it latches an
 * error and the cabinet reports E20 on the handle check. So the reply has to be
 * unsolicited, which is what n2KickbackReportSelfCheck() below exists for.
 * "E00" is one of the two codes that clear the field ("C01" is the other).
 *
 * Still not worked out:
 *
 *   - The layout of the ten byte request past the 0xFF 0xFF header. Bytes 2, 3,
 *     4, 6 and 8 are written from clKickback state (one of them through a float
 *     conversion, so at least one is a scaled magnitude); 5, 7 and 9 are left
 *     at zero by the initialiser.
 *   - Which reply each request expects. The bridge answers everything the same
 *     way, which is enough until the game starts transmitting in earnest.
 *
 * The other half of the board already exists: n2SteeringIo.cpp hooks
 * clKickback's setters and keeps the force feedback state in an
 * N2SteeringOutputState for a device backend to consume. That is where output
 * should be driven from; this file is only the wire the game talks to.
 */
int n2KickbackSerialEnabled(void);

// Where clKickback keeps its singleton, so the board can read the self check
// request line for itself instead of depending on the game to call something
// this loader has hooked.
void n2KickbackSetInstance(void *const *instance);

// Report the motor running or stopped, the answer clKickback::waitOnPower and
// waitOffPower each block on.
void n2KickbackReportMotorPower(int running);

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
