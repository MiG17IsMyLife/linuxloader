#if defined(_WIN32) || defined(__MINGW32__)

#include "n2VirtualDevices.h"

#include "n2CardReader.h"
#include "n2CsNeoPcb.h"
#include "n2Jvio.h"
#include "n2Kickback.h"
#include "../../../config/config.h"
#include "../../../elfLoader/virtualDeviceRegistry.hpp"

#include <cstring>

namespace
{
    bool claimsCardReader(const char *path)
    {
        return getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 &&
               path && std::strcmp(path, "/dev/ttyM2") == 0;
    }

    bool claimsCsNeoPcb(const char *path)
    {
        return n2CsNeoPcbEnabled() && path && std::strcmp(path, "/dev/ttyM0") == 0;
    }

    bool claimsJvio(const char *path)
    {
        return n2JvioSerialEnabled() && path && std::strcmp(path, "/dev/ttyM3") == 0;
    }

    bool claimsKickback(const char *path)
    {
        return n2KickbackSerialEnabled() && path && std::strcmp(path, "/dev/ttyM1") == 0;
    }
}

extern "C" void n2RegisterVirtualDevices(void)
{
    using VirtualDeviceRegistry::Device;
    using VirtualDeviceRegistry::registerDevice;

    registerDevice(Device{
        "Namco N2 CS Neo cabinet PCB (/dev/ttyM0)", claimsCsNeoPcb,
        n2CsNeoPcbOpen, n2CsNeoPcbIsDescriptor, n2CsNeoPcbBytesAvailable,
        n2CsNeoPcbRead, n2CsNeoPcbWrite, n2CsNeoPcbClose, n2CsNeoPcbIoctl, nullptr});

    registerDevice(Device{
        "Namco N2 card reader (/dev/ttyM2)", claimsCardReader,
        n2CardReaderOpen, n2CardReaderIsDescriptor, n2CardReaderBytesAvailable,
        n2CardReaderRead, n2CardReaderWrite, n2CardReaderClose, n2CardReaderIoctl, nullptr});
    registerDevice(Device{
        "Namco N2 JVIO (/dev/ttyM3)", claimsJvio,
        n2JvioSerialOpen, n2JvioSerialIsDescriptor, n2JvioSerialBytesAvailable,
        n2JvioSerialRead, n2JvioSerialWrite, n2JvioSerialClose, n2JvioSerialIoctl, nullptr});
    registerDevice(Device{
        "Namco N2 steering board (/dev/ttyM1)", claimsKickback,
        n2KickbackSerialOpen, n2KickbackSerialIsDescriptor, n2KickbackSerialBytesAvailable,
        n2KickbackSerialRead, n2KickbackSerialWrite, n2KickbackSerialClose, n2KickbackSerialIoctl, nullptr});
}

#endif
