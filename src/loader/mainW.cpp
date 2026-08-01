#if defined(_WIN32) || defined(__MINGW32__)
#include <cstdlib>

#include <filesystem>
#include <winsock2.h>
#include <windows.h>
#include <libgen.h>

#include "elfLoader/alsa2sdlBridge.hpp"
#include "elfLoader/segaapiBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/ipcBridge.hpp"
#include "elfLoader/elfLoader.hpp"
#include "elfLoader/filesystemBridge.hpp"
#include "elfLoader/libcBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/graphicsBridge.hpp"
#include "elfLoader/pthreadBridge.hpp"
#include "elfLoader/regexBridge.hpp"
#include "elfLoader/termiosBridge.hpp"
#include "elfLoader/networkBridge.hpp"
#include "elfLoader/mathBridge.hpp"
#include "elfLoader/symbolResolver.hpp"
#include "elfLoader/pthread/pthreadEmu.hpp"
#include "log/log.h"
#include "graphics/gpuVendor.h"
#include "init.h"
#include "patching/patch.h"
#include "input/sdlInput.h"
#include "hardware/namco/n2/n2.h"
#include "config/config.h"
#include "mainShared.h"

std::string g_absoluteElfPath;
uint32_t partialElfCrc = 0;
static char g_n2ConfigPath[MAX_PATH_LENGTH] = "";
static char g_n2ControlsPath[MAX_PATH_LENGTH] = "";

extern SDLControllers sdlJoysticks;

extern int gGrp;

LONG CALLBACK myVectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    DWORD exceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    PCONTEXT ctx = ExceptionInfo->ContextRecord;


    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION || exceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        uint8_t* pint = (uint8_t*)ctx->Eip;
        if (pint && !IsBadReadPtr(pint, 2) && pint[0] == 0xCD && pint[1] == 0x80) {        
            if (ctx->Eax == 0xE0) { 
                ctx->Eax = (DWORD)PthreadEmu::pthreadSelf();
                ctx->Eip += 2;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}   


void initBridges()
{
    FileSystemBridge::initBridges();
    GraphicsBridge::initBridges();
    LibcBridge::initBridges();
    PthreadBridge::initBridges();
    TermiosBridge::initBridges();
    GccBridge::initBridges();
    NetworkBridge::initBridges();
    IpcBridge::initBridges();
    SegaApiBridge::initBridges();
    Alsa2SdlBridge::initBridges();
    RegexBridge::initBridges();
    MathBridge::initBridges();
}

int main(int argc, char *argv[], char *envp[])
{
    /*
     * Warnings and above by default, but a crash usually needs the lines under
     * that - which shared object landed at which address, which symbol was
     * resolved where - and rebuilding to get them is a poor trade when the
     * failure is hard to reproduce.  LL_LOG_LEVEL opens it up in place.
     */
    logSetMinLevel(LOG_WARN);
    if (const char *requested = std::getenv("LL_LOG_LEVEL"))
    {
        const struct { const char *name; int level; } levels[] = {
            {"trace", LOG_TRACE}, {"debug", LOG_DEBUG}, {"game", LOG_GAME},
            {"info", LOG_INFO}, {"warn", LOG_WARN}, {"error", LOG_ERROR},
            {"fatal", LOG_FATAL},
        };
        for (const auto &entry : levels)
        {
            if (_stricmp(requested, entry.name) == 0)
            {
                logSetMinLevel(entry.level);
                break;
            }
        }
    }

    char command[MAX_PATH_LENGTH] = {0};
    char originalDir[MAX_PATH_LENGTH] = {0};
    char gameELF[MAX_PATH_LENGTH] = {0};
    char libraryPath[MAX_PATH_LENGTH] = {0};
    char gamePath[MAX_PATH_LENGTH] = {0};

    log_info("Parsing arguments...\n");
    if (parseArgs(argc, argv, command, originalDir, gameELF, libraryPath) != PARSE_ARGS_SUCCESS)
        return EXIT_SUCCESS;

    if (configPath)
        strncpy(g_n2ConfigPath, configPath, sizeof(g_n2ConfigPath) - 1);
    if (controlsPath)
        strncpy(g_n2ControlsPath, controlsPath, sizeof(g_n2ControlsPath) - 1);
    const bool hasConfigPath = configPath && configPath[0] != '\0';
    const bool hasControlsPath = controlsPath && controlsPath[0] != '\0';

    char elfPath[MAX_PATH_LENGTH];
    char elfArgs[MAX_PATH_LENGTH] = "";

    char *spacePos = strchr(gameELF, ' ');
    if (spacePos != NULL)
    {
        size_t elfPathLen = spacePos - gameELF;
        strncpy(elfPath, gameELF, elfPathLen);
        elfPath[elfPathLen] = '\0';
        strcpy(elfArgs, spacePos + 1);
    }
    else
    {
        strcpy(elfPath, gameELF);
    }

    ElfLoader::PreReserveAddressSpace(elfPath);

    log_info("Initializing library search paths...\n");
    GetCurrentDirectoryA(MAX_PATH_LENGTH, gamePath);
    SymbolResolver::GetInstance().InitSearchPaths(libraryPath, gamePath);

    // Install Exception Handler (BEFORE any C++ code runs)
    if (!AddVectoredExceptionHandler(1, myVectoredHandler))
        log_error("Failed to register MyVectoredHandler");

    log_info("Initializing bridges...\n");
    initBridges();

    if (!std::filesystem::exists("tmp"))
        std::filesystem::create_directory("tmp");
    if (!std::filesystem::exists("tmp\\segaboot"))
        std::filesystem::create_directory("tmp\\segaboot");

    log_info("Loading ELF file: %s\n", elfPath);
    ElfLoader loader;
    bool initializedBeforeElfConstructors = false;

    if (!loader.Load(elfPath, [&]() {
            if (!n2DetectGame())
                return true;

            uint8_t *baseAddr = (uint8_t *)loader.GetBaseAddress();
            if (baseAddr)
                partialElfCrc = getCrc32Mem(baseAddr + 10, 0x4000);
            initMain(hasConfigPath ? g_n2ConfigPath : (char *)"",
                     hasControlsPath ? g_n2ControlsPath : (char *)"");
            initializedBeforeElfConstructors = true;
            return true;
        }))
    {
        log_fatal("Failed to load ELF file: %s", elfPath);
        return 1;
    }

    uint8_t *baseAddr = (uint8_t *)loader.GetBaseAddress();
    if (baseAddr && !initializedBeforeElfConstructors)
        partialElfCrc = getCrc32Mem(baseAddr + 10, 0x4000);

    if (!initializedBeforeElfConstructors)
    {
        log_debug("Initializing main...");
        initMain(hasConfigPath ? g_n2ConfigPath : (char *)"",
                 hasControlsPath ? g_n2ControlsPath : (char *)"");
    }

    int final_argc = 0;
    char *final_argv_arr[10];

    final_argv_arr[final_argc++] = elfPath;

    if (strlen(elfArgs) > 0)
    {
        char *token = strtok(elfArgs, " ");
        while (token != NULL && final_argc < 9)
        {
            final_argv_arr[final_argc++] = token;
            token = strtok(NULL, " ");
        }
    }
    /*
     * WMMT3 derives its development flag straight from the argument count:
     *
     *     main():  gDebug = (argc == 1);  seqMain(gDebug)
     *
     * A cabinet starts the game from .execrc as "./main 1", so argc is 2 and
     * the flag stays clear.  Launching the ELF bare puts it in development
     * mode, which swaps the test menu for the one carrying デバッグ and
     * テストプレイ and makes main() shell out to "perl prepend-n2.pl".
     * Reproduce the cabinet's command line unless the user asked for the
     * development mode through [NamcoN2] DEBUG_MODE, or passed their own
     * arguments.
     */
    if (getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 && final_argc == 1)
    {
        if (getConfig()->n2DebugMode)
        {
            log_warn("Namco N2: [NamcoN2] DEBUG_MODE is on; starting the ELF without arguments "
                     "so the game runs in its development mode");
        }
        else
        {
            static char n2CabinetArg[] = "1";
            final_argv_arr[final_argc++] = n2CabinetArg;
            log_info("Namco N2: starting the ELF as \"main 1\" to match the cabinet's .execrc");
        }
    }

    final_argv_arr[final_argc] = NULL;

    if (!loader.Execute(final_argc, final_argv_arr, envp))
    {
        log_error("Failed to execute ELF file.");
        return 1;
    }

    log_info("ELF Execution Finished");
    return 0;
}

#endif
