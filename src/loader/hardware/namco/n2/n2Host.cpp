#include "n2Host.h"

#include "../../../config/config.h"
#include "../../../log/log.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
/*
 * etc/ifconfig.pl walks the "ifconfig" output, keeps the first ethN entry that
 * carries an inet address and skips lo entirely.  GetAdaptersInfo already
 * hands back that same set, so the filtering here mirrors the script: no
 * loopback, and an address that is actually configured.
 */
const NamcoN2NetworkConfig *networkConfig()
{
    EmulatorConfig *config = getConfig();
    return config ? &config->namcoN2.network : nullptr;
}

bool isUsableAddress(const IP_ADDR_STRING &address)
{
    return address.IpAddress.String[0] != '\0' &&
           std::strcmp(address.IpAddress.String, "0.0.0.0") != 0;
}

bool isUsableAdapter(const IP_ADAPTER_INFO &adapter)
{
    return adapter.Type != MIB_IF_TYPE_LOOPBACK && isUsableAddress(adapter.IpAddressList);
}

bool matchesAdapter(const IP_ADAPTER_INFO &adapter, const IP_ADDR_STRING &address,
                    const NamcoN2NetworkConfig *config)
{
    if (!config)
        return true;

    if (config->bindAddress[0] != '\0' &&
        std::strcmp(config->bindAddress, address.IpAddress.String) != 0)
        return false;

    if (config->interfaceName[0] == '\0')
        return true;

    return std::strstr(adapter.AdapterName, config->interfaceName) != nullptr ||
           std::strstr(adapter.Description, config->interfaceName) != nullptr;
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

    const NamcoN2NetworkConfig *config = networkConfig();
    bool sawUsableAdapter = false;
    for (IP_ADAPTER_INFO *adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (!isUsableAdapter(*adapter))
            continue;

        for (IP_ADDR_STRING *ip = &adapter->IpAddressList; ip; ip = ip->Next)
        {
            if (!isUsableAddress(*ip))
                continue;
            sawUsableAdapter = true;
            if (!matchesAdapter(*adapter, *ip, config))
                continue;

            parseDottedQuad(ip->IpAddress.String, address);
            parseDottedQuad(ip->IpMask.String, mask);

            std::memset(mac, 0, 6);
            const UINT copied = adapter->AddressLength < 6 ? adapter->AddressLength : 6;
            std::memcpy(mac, adapter->Address, copied);

            *interfaceIndex = 0;
            *link = config && !config->enabled ? 0 : 1;
            return 1;
        }
    }

    if (config && (config->bindAddress[0] != '\0' || config->interfaceName[0] != '\0'))
    {
        log_warn("Namco N2: configured network adapter was not found: interface='%s' address='%s'",
                 config->interfaceName, config->bindAddress);
    }
    (void)sawUsableAdapter;
    return 0;
}

extern "C" int n2HostNetworkCommand(const char *command)
{
    if (!command)
        return -1;

    const NamcoN2NetworkConfig *config = networkConfig();
    if (config && !config->enabled)
    {
        log_info("Namco N2: network initialization disabled");
        return 0;
    }

    log_info("Namco N2: virtualized network setup accepted: %s", command);
    if (config && config->bindAddress[0] != '\0')
        log_info("Namco N2: sockets use configured address %s", config->bindAddress);
    if (config && config->broadcastAddress[0] != '\0')
        log_info("Namco N2: broadcast address %s", config->broadcastAddress);
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
