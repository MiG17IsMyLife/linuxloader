#include "n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <string>

#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../log/log.h"
#include "../../../../minhook/include/MinHook.h"

extern "C" void *n2ResolveSymbol(const char *name)
{
    std::string module;
    void *address = SymbolResolver::GetInstance().ResolveSymbol(name, &module);
    return module == "UNRESOLVED_STUB" ? nullptr : address;
}

extern "C" int n2HookSymbol(const char *name, void *replacement)
{
    void *target = n2ResolveSymbol(name);
    if (!target)
    {
        log_debug("Namco N2: optional symbol not found: %s", name);
        return 0;
    }

    // Two modules may legitimately ask for the same entry point.
    MH_STATUS status = MH_CreateHook(target, replacement, nullptr);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        log_error("Namco N2: failed to hook %s (MinHook status %d)", name, (int)status);
        return 0;
    }
    return 1;
}

extern "C" int n2ArmHooks(void)
{
    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK)
    {
        log_error("Namco N2: failed to arm hooks (MinHook status %d)", (int)status);
        return 0;
    }
    return 1;
}

extern "C" int n2HookSymbolWithOriginal(const char *name, void *replacement, void **original)
{
    void *target = n2ResolveSymbol(name);
    if (!target)
    {
        log_debug("Namco N2: optional symbol not found: %s", name);
        return 0;
    }

    MH_STATUS status = MH_CreateHook(target, replacement, original);
    if (status != MH_OK)
    {
        log_error("Namco N2: failed to hook %s with trampoline (MinHook status %d)",
                  name, (int)status);
        return 0;
    }
    return 1;
}

#endif
