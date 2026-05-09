#if defined(_WIN32) || defined(__MINGW32__)
#include "termiosBridge.hpp"
#include "symbolResolver.hpp"
#include "libcBridge.hpp"
#include "../log/log.h"
#include <map>
#include <cstring>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace TermiosBridge
{
    void initBridges()
    {
        log_info("Initializing Termios Bridges...");

        MAP("tcgetattr", bridgeTcgetattr);
        MAP("tcsetattr", bridgeTcsetattr);
        MAP("tcflush", bridgeTcflush);
        MAP("tcdrain", bridgeTcdrain);
        MAP("cfgetispeed", LibcBridge::bridgeStubSuccess);
        MAP("cfgetospeed", LibcBridge::bridgeStubSuccess);
        MAP("cfsetispeed", bridgeCfsetispeed);
        MAP("cfsetospeed", bridgeCfsetospeed);
        MAP("cfsetspeed", bridgeCfsetospeed);
        MAP("cfmakeraw", LibcBridge::bridgeStubSuccess);

        MAP("isatty", LibcBridge::bridgeStubSuccess);
    }

    static std::map<int, linux_termios> g_termios_state;

    int bridgeTcgetattr(int fd, struct linux_termios *termios_p)
    {
        if (!termios_p)
            return -1;

        // If state exists, return it; otherwise return default "raw-ish" mode
        if (g_termios_state.find(fd) != g_termios_state.end())
        {
            *termios_p = g_termios_state[fd];
        }
        else
        {
            memset(termios_p, 0, sizeof(linux_termios));
            // Default flags typical for JVS communication
            termios_p->c_cflag = 0x1000 | 0x0800 | 0x0030; // CLOCAL | CREAD | CS8
            termios_p->c_ispeed = 115200;
            termios_p->c_ospeed = 115200;
        }
        return 0;
    }

    int bridgeTcsetattr(int fd, int optional_actions, const struct linux_termios *termios_p)
    {
        if (!termios_p)
            return -1;
        // Store state to appease the game
        g_termios_state[fd] = *termios_p;
        log_debug("tcsetattr: fd=%d, speed=%d, flags=0x%x", fd, termios_p->c_ospeed, termios_p->c_cflag);
        return 0;
    }

    int bridgeTcflush(int fd, int queue_selector)
    {
        // Ideally map to PurgeComm, but returning 0 (success) is sufficient for now
        return 0;
    }

    int bridgeTcdrain(int fd)
    {
        // Wait for transmission. Sleep briefly to simulate IO delay.
        Sleep(1);
        return 0;
    }

    int bridgeCfsetispeed(struct linux_termios *termios_p, unsigned int speed)
    {
        if (termios_p)
            termios_p->c_ispeed = speed;
        return 0;
    }

    int bridgeCfsetospeed(struct linux_termios *termios_p, unsigned int speed)
    {
        if (termios_p)
            termios_p->c_ospeed = speed;
        return 0;
    }

} // namespace TermiosBridge

#endif