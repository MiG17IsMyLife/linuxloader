#include "platformBackend.h"

#include "../config/config.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/n2/n2.h"
#include "../hardware/namco/n2/n2CardReader.h"
#include "../hardware/namco/es1/es1VirtualDevices.h"
#include "../hardware/namco/n2/n2VirtualDevices.h"
#include "../log/log.h"

namespace
{
bool g_detected = false;
}

extern "C" int platformPrepareLoad(const char *elfPath)
{
    /* ES1 is checked first by design; N2 and ES1 are separate boards. */
    if (es1DetectGame(elfPath))
    {
        es1PrepareLoad(elfPath);
        return 0;
    }
    n2PrepareLoad(elfPath);
    return 0;
}

extern "C" int platformDetectGame(const char *elfPath)
{
    g_detected = false;

    if (es1DetectGame(elfPath))
    {
        g_detected = true;
        return 1;
    }
    if (n2DetectGame(elfPath))
    {
        g_detected = true;
        return 1;
    }
    return 0;
}

extern "C" int platformIsDetected(void)
{
    return g_detected ? 1 : 0;
}

extern "C" int platformIsN2(void)
{
    return g_detected && n2IsDetected();
}

extern "C" int platformIsES1(void)
{
    return g_detected && es1IsDetected();
}

extern "C" int platformInstallHooks(void)
{
    if (platformIsES1())
        return es1InstallHooks();
    if (platformIsN2())
        return n2InstallHooks();
    return -1;
}

extern "C" int platformInstallAdmHooks(void)
{
    if (platformIsN2())
        return n2InstallAdmHooks();
    return 0;
}

extern "C" int platformInstallLateHooks(void)
{
    if (platformIsES1())
        return es1InstallLateHooks();
    if (platformIsN2())
        return n2InstallLateTextureHooks();
    return -1;
}

extern "C" int platformInitializeGraphics(void)
{
    if (platformIsES1())
        return es1InitializeGraphics();
    if (platformIsN2())
        return n2InitializeGraphics();
    return -1;
}

extern "C" int platformHandleSystemCommand(const char *command)
{
    if (platformIsES1())
        return es1HandleSystemCommand(command);
    if (platformIsN2())
        return n2HandleSystemCommand(command);
    return -1;
}

extern "C" int platformHandleHostKey(int key, uint32_t modifiers)
{
    return platformHandleHostKeyEvent(key, modifiers, 1);
}

extern "C" int platformHandleHostKeyEvent(int key, uint32_t modifiers, int pressed)
{
    if (platformIsN2() && pressed)
        return n2HandleHostKey(key, modifiers);
    return 0;
}

extern "C" int platformWantsCabinetArgument(void)
{
    return platformIsN2() ? 1 : 0;
}

extern "C" const char *platformName(void)
{
    if (platformIsES1())
        return "Namco System ES1";
    if (platformIsN2())
        return "Namco System N2";
    return "Unknown";
}

extern "C" void platformRegisterVirtualDevices(void)
{
    /* Registration is done before detection; each provider gates its claims. */
    n2RegisterVirtualDevices();
    es1RegisterVirtualDevices();
}

extern "C" void platformRegisterCardControl(void)
{
    if (platformIsN2())
        n2CardReaderRegisterCardControl();
}
