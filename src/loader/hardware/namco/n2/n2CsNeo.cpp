#include "n2CsNeo.h"

#include "n2.h"
#include "n2Hasp.h"
#include "n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <filesystem>

#include <SDL3/SDL.h>

#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../log/log.h"

namespace
{
constexpr char launcherPrefix[] = "hlds";
constexpr char engineModule[] = "engine_amd.so";
constexpr char defaultLoginId[] = "12";
constexpr char localLoginCommandText[] = "login 12\n";
constexpr char contentsDirectory[] = "freespace/contents2";
constexpr char saveDirectory[] = "platform/SAVE";

int returnNoError()
{
    return 0;
}

void suppressCabinetError()
{
    // Offline CS Neo has no retired Namco store/account server. Its watchdog
    // may still request the cabinet error overlay while a local map is active.
}

void localLoginCommand(void *, int argc, char **argv)
{
    const char *loginId = argc > 1 && argv && argv[1] ? argv[1] : defaultLoginId;
    using MsLogin = void (*)(const char *);
    MsLogin localLogin = reinterpret_cast<MsLogin>(n2ResolveSymbol("_Z7msLoginPKc"));
    if (!localLogin)
    {
        log_warn("Namco N2 CS Neo: could not resolve msLogin for offline login");
        return;
    }

    localLogin(loginId);
    log_info("Namco N2 CS Neo: local login %s accepted without store DB", loginId);
}

void createDirectory(const char *path)
{
    std::error_code failure;
    std::filesystem::create_directories(path, failure);
    if (failure)
        log_warn("Namco N2 CS Neo: could not create %s (%s)",
                 path, failure.message().c_str());
}
} // namespace

extern "C" int n2CsNeoLooksLikeGame(const char *elfPath)
{
    if (!elfPath || !*elfPath)
        return 0;

    std::error_code failure;
    const std::filesystem::path executable(elfPath);
    if (executable.stem().string().rfind(launcherPrefix, 0) != 0)
        return 0;

    return std::filesystem::exists(executable.parent_path() / engineModule, failure) ? 1 : 0;
}

extern "C" int n2CsNeoPrepareLoad(const char *elfPath)
{
    if (!n2CsNeoLooksLikeGame(elfPath))
        return 0;

    /*
     * engine_amd.so is a DT_NEEDED dependency of the stripped launcher, so
     * its PLT is relocated before game detection can inspect the mapped ELF.
     * Reserve these path-identifiable overrides before loading either file.
     */
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z10IsPCBErrorv", reinterpret_cast<void *>(returnNoError));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z10IsTestModev", reinterpret_cast<void *>(returnNoError));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z9makeErrorv", reinterpret_cast<void *>(suppressCabinetError));
    SymbolResolver::GetInstance().RegisterVTable(
        "_ZN12CommandLogin7ExecuteEiPPc", reinterpret_cast<void *>(localLoginCommand));
    n2RegisterHaspPreloadOverrides();
    return 1;
}

extern "C" int n2CsNeoInstallHooks(void)
{
    createDirectory(contentsDirectory);
    createDirectory(saveDirectory);

    n2InstallAdmHooks();
    log_info("Namco N2 CS Neo compatibility hooks installed");
    return 0;
}

extern "C" int n2CsNeoHandleHostKey(int key, uint32_t modifiers)
{
    if ((modifiers & SDL_KMOD_ALT) == 0 || key != SDLK_F7)
        return 0;

    using CbufAddText = void (*)(const char *);
    CbufAddText addText = reinterpret_cast<CbufAddText>(n2ResolveSymbol("Cbuf_AddText"));
    if (!addText)
    {
        log_warn("Namco N2 CS Neo: could not resolve Cbuf_AddText for local login");
        return 1;
    }

    // Keep the established offline `login 12` procedure available for dumps
    // carrying the matching local database patches.
    addText(localLoginCommandText);
    log_info("Namco N2 CS Neo: requested local login 12 (Alt+F7)");
    return 1;
}

#endif
