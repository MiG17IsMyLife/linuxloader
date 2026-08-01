#pragma once

/*
 * Host-system queries behind the shell helpers the cabinet shipped in etc/.
 * They live apart from n2.cpp so the Windows networking headers never have to
 * share a translation unit with SDL.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The numbers etc/ifconfig.pl prints, taken from the first usable host
 * adapter: "$interface @address @mask @mac $link".  Returns 1 when a real
 * adapter was found, 0 when the caller should fall back to a loopback answer.
 */
int n2HostNetworkInterface(int *interfaceIndex, unsigned char address[4],
                           unsigned char mask[4], unsigned char mac[6], int *link);

/*
 * What etc/usbsize.pl reports: the total size of the volume holding the
 * working directory, in 1K blocks (the second column of "busybox df").
 * Returns 0 if the size could not be determined.
 */
unsigned long long n2HostWorkDiskKilobytes(void);

#ifdef __cplusplus
}
#endif
