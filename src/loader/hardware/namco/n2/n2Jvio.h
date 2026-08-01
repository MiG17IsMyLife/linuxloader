#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The cabinet's JVIO board hangs off /dev/ttyM3 at 115200 8N1, and the game
 * drives it as a plain JVS master: n2JvioTx() escapes 0xE0/0xD0 and appends a
 * byte-sum checksum, n2JvioRx() reassembles the reply, and the command set is
 * the one in jvs.h.  This bridge answers that master with the loader's JVS
 * slave instead of a serial port.
 *
 * True for the Wangan titles only; Counter-Strike Neo has no JVIO board of its
 * own and never opens the port.
 */
int n2JvioSerialEnabled(void);

int n2JvioSerialOpen(const char *path, int flags);
int n2JvioSerialIsDescriptor(int fd);

/* Bytes the slave has ready, so FIONREAD and poll() can answer truthfully. */
int n2JvioSerialBytesAvailable(int fd);
int n2JvioSerialRead(int fd, void *buffer, size_t count);
int n2JvioSerialWrite(int fd, const void *buffer, size_t count);
int n2JvioSerialClose(int fd);
int n2JvioSerialIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
