#include "n2Host.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
/*
 * etc/ifconfig.pl walks the "ifconfig" output, keeps the first ethN entry that
 * carries an inet address and skips lo entirely.  GetAdaptersInfo already
 * hands back that same set, so the filtering here mirrors the script: no
 * loopback, and an address that is actually configured.
 */
bool isUsableAdapter(const IP_ADAPTER_INFO &adapter)
{
    if (adapter.Type == MIB_IF_TYPE_LOOPBACK)
        return false;

    const char *address = adapter.IpAddressList.IpAddress.String;
    return address[0] != '\0' && std::strcmp(address, "0.0.0.0") != 0;
}

void parseDottedQuad(const char *text, unsigned char out[4])
{
    unsigned int parts[4] = {0, 0, 0, 0};
    if (std::sscanf(text, "%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]) != 4)
        return;
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<unsigned char>(parts[i] & 0xFF);
}
} // namespace

extern "C" int n2HostNetworkInterface(int *interfaceIndex, unsigned char address[4],
                                      unsigned char mask[4], unsigned char mac[6], int *link)
{
    ULONG size = 0;
    if (GetAdaptersInfo(nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0)
        return 0;

    std::vector<unsigned char> buffer(size);
    IP_ADAPTER_INFO *adapters = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());
    if (GetAdaptersInfo(adapters, &size) != NO_ERROR)
        return 0;

    for (IP_ADAPTER_INFO *adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (!isUsableAdapter(*adapter))
            continue;

        parseDottedQuad(adapter->IpAddressList.IpAddress.String, address);
        parseDottedQuad(adapter->IpAddressList.IpMask.String, mask);

        std::memset(mac, 0, 6);
        const UINT copied = adapter->AddressLength < 6 ? adapter->AddressLength : 6;
        std::memcpy(mac, adapter->Address, copied);

        /*
         * The script's link flag comes from "sudo ethtool ethN | Link detected".
         * An adapter that GetAdaptersInfo still reports with a configured
         * address is the closest equivalent Windows offers here.
         */
        *interfaceIndex = 0;
        *link = 1;
        return 1;
    }

    return 0;
}

extern "C" unsigned long long n2HostWorkDiskKilobytes(void)
{
    ULARGE_INTEGER freeToCaller = {};
    ULARGE_INTEGER total = {};
    ULARGE_INTEGER totalFree = {};
    if (!GetDiskFreeSpaceExA(".", &freeToCaller, &total, &totalFree))
        return 0;

    return total.QuadPart / 1024ULL;
}

#endif
