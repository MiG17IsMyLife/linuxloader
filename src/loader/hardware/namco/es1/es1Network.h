#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual eth0 used by the ES1 cabinet bootstrap and network bridge. */
int es1HostNetworkInterface(int *interfaceIndex, unsigned char address[4],
                            unsigned char mask[4], unsigned char mac[6], int *link);
int es1HostAdapterAddress(unsigned char address[4]);
int es1HostGuestAddress(unsigned char address[4]);
int es1HostNetworkCommand(const char *command);

#ifdef __cplusplus
}
#endif
