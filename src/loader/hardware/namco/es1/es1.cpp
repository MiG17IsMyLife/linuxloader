#include "es1.h"

#include "../../../log/log.h"
#include "../../../graphics/sdlCalls.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
bool g_detected = false;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

bool has(const std::string &contents, const char *needle)
{
    return contents.find(needle) != std::string::npos;
}
}

extern "C" int es1PrepareLoad(const char *elfPath)
{
    (void)elfPath;
    return 0;
}

extern "C" int es1DetectGame(const char *elfPath)
{
    g_detected = false;
    if (!elfPath || !*elfPath)
        return 0;

    const std::filesystem::path elf(elfPath);
    const std::filesystem::path root = elf.parent_path().parent_path().parent_path();
    const std::string info = readFile(root / "info");
    const std::string csv = readFile(root / "data" / "csv" / "config.csv");

    /*
     * Maximum Heat 3D has no N2 gRomInfo marker.  Its package metadata and
     * JAMMA/camera/display configuration are stable across the known dump.
     * Require the package evidence instead of relying on the directory name.
     */
    const bool packageName = has(info, "US DRIVE") || has(info, "Maximum Heat 3D");
    const bool es1Config = has(csv, "USE_JAMMA_DEVICE") &&
                           has(csv, "USE_CAMERA_DEVICE") &&
                           has(csv, "VIDEO_XSIZE=1360") &&
                           has(csv, "VIDEO_YSIZE=768");
    if (!packageName && !es1Config)
        return 0;

    g_detected = true;
    log_info("Detected Namco System ES1 title: Maximum Heat 3D");
    return 1;
}

extern "C" int es1IsDetected(void)
{
    return g_detected ? 1 : 0;
}

extern "C" int es1InstallHooks(void)
{
    log_info("System ES1: no N2 code hooks installed; using common loader bridges");
    return 0;
}

extern "C" int es1InstallLateHooks(void)
{
    return 0;
}

extern "C" int es1InitializeGraphics(void)
{
    /* N2 creates the shared context in its own graphics backend. ES1 has no
     * N2 graphics hook, so it must explicitly bring up the common SDL/GL
     * window before pacloader reports GL capabilities. */
    startSDL();
    return 0;
}

extern "C" int es1HandleSystemCommand(const char *command)
{
    (void)command;
    return -1;
}

extern "C" int es1HandleHostKey(int key, uint32_t modifiers)
{
    (void)key;
    (void)modifiers;
    return 0;
}

extern "C" const char *es1GetGameTitle(void)
{
    return "Maximum Heat 3D";
}

extern "C" const char *es1GetGameShortTitle(void)
{
    return "Maximum Heat 3D";
}

extern "C" const char *es1GetGameId(void)
{
    return "MHEAT3D";
}

extern "C" const char *es1GetRevision(void)
{
    return "8807";
}
