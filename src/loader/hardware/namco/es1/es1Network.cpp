#include "es1Network.h"

#include "../../../log/log.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Es1InterfaceState
{
    bool initialized = false;
    unsigned char adapterAddress[4] = {127, 0, 0, 1};
    unsigned char guestAddress[4] = {127, 0, 0, 1};
    unsigned char mask[4] = {255, 0, 0, 0};
    unsigned char mac[6] = {};
    int interfaceIndex = 0;
    int link = 1;
};

Es1InterfaceState state;
std::mutex stateMutex;

bool usableAddress(const IP_ADDR_STRING &address)
{
    return address.IpAddress.String[0] != '\0' &&
           std::strcmp(address.IpAddress.String, "0.0.0.0") != 0;
}

void parseDottedQuad(const char *text, unsigned char out[4])
{
    unsigned int parts[4] = {0, 0, 0, 0};
    if (!text || std::sscanf(text, "%u.%u.%u.%u",
                             &parts[0], &parts[1], &parts[2], &parts[3]) != 4)
        return;
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<unsigned char>(parts[i] & 0xff);
}

void initializeStateLocked()
{
    if (state.initialized)
        return;
    state.initialized = true;

    ULONG size = 0;
    if (GetAdaptersInfo(nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0)
        return;

    std::vector<unsigned char> buffer(size);
    IP_ADAPTER_INFO *adapters = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());
    if (GetAdaptersInfo(adapters, &size) != NO_ERROR)
        return;

    for (IP_ADAPTER_INFO *adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (adapter->Type == MIB_IF_TYPE_LOOPBACK ||
            !usableAddress(adapter->IpAddressList))
            continue;

        const IP_ADDR_STRING &ip = adapter->IpAddressList;
        parseDottedQuad(ip.IpAddress.String, state.adapterAddress);
        std::memcpy(state.guestAddress, state.adapterAddress, sizeof(state.guestAddress));
        parseDottedQuad(ip.IpMask.String, state.mask);
        const UINT copied = std::min<UINT>(adapter->AddressLength, 6);
        std::memcpy(state.mac, adapter->Address, copied);
        state.link = 1;
        log_info("System ES1: virtual eth0 uses host adapter %u.%u.%u.%u",
                 state.adapterAddress[0], state.adapterAddress[1],
                 state.adapterAddress[2], state.adapterAddress[3]);
        return;
    }

    log_warn("System ES1: no usable host adapter; using loopback eth0");
}

std::string redirectPath(const char *command)
{
    if (!command)
        return {};
    const char *redirect = std::strchr(command, '>');
    if (!redirect)
        return {};
    ++redirect;
    while (*redirect && std::isspace(static_cast<unsigned char>(*redirect)))
        ++redirect;

    std::string path(redirect);
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back())))
        path.pop_back();
    if (path.size() >= 2 && path.front() == '\'' && path.back() == '\'')
        path = path.substr(1, path.size() - 2);
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    if (path.rfind("/tmp/", 0) == 0)
        path.erase(0, 1);
    else if (!path.empty() && path.front() == '/')
        path.erase(0, 1);
    return path;
}

bool parseIPv4Token(const std::string &token, unsigned char out[4])
{
    const unsigned long value = inet_addr(token.c_str());
    if (value == INADDR_NONE && token != "255.255.255.255")
        return false;
    std::memcpy(out, &value, 4);
    return true;
}

void applyIfconfigLocked(const char *command)
{
    std::istringstream stream(command ? command : "");
    std::string token;
    bool afterEth0 = false;
    bool haveAddress = false;
    while (stream >> token)
    {
        if (token == "eth0")
        {
            afterEth0 = true;
            haveAddress = false;
            continue;
        }
        if (!afterEth0)
            continue;

        unsigned char parsed[4] = {};
        if (!haveAddress && parseIPv4Token(token, parsed))
        {
            std::memcpy(state.guestAddress, parsed, sizeof(parsed));
            haveAddress = true;
            continue;
        }
        if (token == "netmask" && (stream >> token) && parseIPv4Token(token, parsed))
            std::memcpy(state.mask, parsed, sizeof(parsed));
    }
}

int writeInterfaceReport(const char *command)
{
    const std::string path = redirectPath(command);
    if (path.empty())
        return 1;

    std::lock_guard<std::mutex> lock(stateMutex);
    initializeStateLocked();
    std::error_code error;
    const std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty())
        std::filesystem::create_directories(outputPath.parent_path(), error);
    if (error)
        return 1;

    std::ofstream output(outputPath, std::ios::trunc | std::ios::binary);
    if (!output)
        return 1;
    output << state.interfaceIndex;
    for (unsigned char value : state.guestAddress)
        output << " " << static_cast<int>(value);
    for (unsigned char value : state.mask)
        output << " " << static_cast<int>(value);
    for (unsigned char value : state.mac)
        output << " " << static_cast<int>(value);
    output << " " << state.link << "\n";
    log_debug("System ES1: wrote virtual eth0 report to %s", path.c_str());
    return output ? 0 : 1;
}

void clearConflictReports()
{
    /*
     * The ES1 bootstrap normally fills these files with replies from the
     * cabinet LAN.  An isolated virtual cabinet has no ARP/pinger replies;
     * leaving a report from an earlier run in place makes the game's
     * clIPConflictChecker compare our own address twice and raise the real
     * PCB-ID duplicate error.
     */
    std::error_code error;
    /* Keep the parser's input valid while pointing it at a non-local address.
     * The following arping operation is virtualized and produces no reply;
     * unlike the stale host address, this cannot be mistaken for our PCB. */
    constexpr const char *virtualPeer = "192.168.3.254\n";
    for (const char *name : {"save0/tmp/arp_ip.txt", "save1/tmp/arp_ip.txt"})
    {
        const std::filesystem::path path(name);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc | std::ios::binary);
        if (!output)
        {
            log_warn("System ES1: could not clear virtual conflict report %s", name);
            continue;
        }
        output << virtualPeer;
        log_debug("System ES1: virtual cabinet LAN has no conflict reply (%s)", name);
    }

    /* clIPConflictChecker reads the command output, not arp_ip.txt.  An
     * isolated real cabinet produces an empty arping result, so create the
     * same empty reports and remove stale replies left by a previous run. */
    for (const char *name : {"save0/tmp/arping.txt", "save1/tmp/arping.txt"})
    {
        const std::filesystem::path path(name);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc | std::ios::binary);
        if (!output)
            log_warn("System ES1: could not clear virtual arping output %s", name);
    }

    /* The real pinger creates this file even when every configured target
     * replies. clNet::updateClient uses its existence as the completion
     * signal before it starts the per-PCB LAN sessions. */
    for (const char *name : {"save0/ping-reports.txt", "save1/ping-reports.txt"})
    {
        const std::filesystem::path path(name);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc | std::ios::binary);
        if (!output)
            log_warn("System ES1: could not write virtual ping report %s", name);
    }
}
} // namespace

extern "C" int es1HostNetworkInterface(int *interfaceIndex, unsigned char address[4],
                                         unsigned char mask[4], unsigned char mac[6], int *link)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    initializeStateLocked();
    if (interfaceIndex)
        *interfaceIndex = state.interfaceIndex;
    if (address)
        std::memcpy(address, state.guestAddress, 4);
    if (mask)
        std::memcpy(mask, state.mask, 4);
    if (mac)
        std::memcpy(mac, state.mac, 6);
    if (link)
        *link = state.link;
    return 1;
}

extern "C" int es1HostAdapterAddress(unsigned char address[4])
{
    if (!address)
        return 0;
    std::lock_guard<std::mutex> lock(stateMutex);
    initializeStateLocked();
    std::memcpy(address, state.adapterAddress, 4);
    return 1;
}

extern "C" int es1HostGuestAddress(unsigned char address[4])
{
    if (!address)
        return 0;
    std::lock_guard<std::mutex> lock(stateMutex);
    initializeStateLocked();
    std::memcpy(address, state.guestAddress, 4);
    return 1;
}

extern "C" int es1HostNetworkCommand(const char *command)
{
    if (!command)
        return -1;

    if (std::strstr(command, "ifconfig.pl") && std::strchr(command, '>'))
        return writeInterfaceReport(command);

    if (std::strstr(command, "ifconfig eth0"))
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        initializeStateLocked();
        applyIfconfigLocked(command);
        log_debug("System ES1: accepted virtual eth0 configuration command");
        return 0;
    }

    if (std::strstr(command, "arping") || std::strstr(command, "pinger.pl"))
    {
        clearConflictReports();
        return 0;
    }

    if (std::strstr(command, "route ") || std::strstr(command, "killall perl") ||
        std::strstr(command, "/proc/sys/net/"))
        return 0;

    return -1;
}

#else

extern "C" int es1HostNetworkInterface(int *, unsigned char[4], unsigned char[4],
                                         unsigned char[6], int *)
{
    return 0;
}

extern "C" int es1HostAdapterAddress(unsigned char[4])
{
    return 0;
}

extern "C" int es1HostGuestAddress(unsigned char[4])
{
    return 0;
}

extern "C" int es1HostNetworkCommand(const char *)
{
    return -1;
}

#endif
