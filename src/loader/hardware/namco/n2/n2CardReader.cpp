#include "n2CardReader.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#include "../../../config/config.h"
#include "../../../log/log.h"

namespace
{
constexpr int cardDescriptor = 0x7202;
constexpr char cardDevicePath[] = "/dev/ttyM2";
// YaCardEmu may be started after the game, so keep retrying - but not on every
// status request, which the boot check issues continuously.
constexpr DWORD reconnectIntervalMs = 1000;
std::mutex cardMutex;
HANDLE cardPipe = INVALID_HANDLE_VALUE;
bool launchAttempted = false;
bool connectFailureReported = false;
DWORD lastConnectAttempt = 0;

bool launchYaCardEmu()
{
    EmulatorConfig *config = getConfig();
    if (!config->n2YaCardEmuAutoStart || !config->n2YaCardEmuPath[0] || launchAttempted)
        return false;

    launchAttempted = true;
    std::filesystem::path executable(config->n2YaCardEmuPath);
    std::string commandLine = "\"" + executable.string() + "\"";
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    std::string workingDirectory = executable.parent_path().string();

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const BOOL started = CreateProcessA(
        executable.string().c_str(), mutableCommand.data(), nullptr, nullptr,
        FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo, &processInfo);
    if (!started)
    {
        log_error("Namco N2 card: failed to launch external YaCardEmu (%s, error=%lu)",
                  executable.string().c_str(), GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    log_info("Namco N2 card: launched external YaCardEmu process");
    return true;
}

bool connectCardPipe()
{
    EmulatorConfig *config = getConfig();
    const char *pipeName = config->n2YaCardEmuPipe;
    if (!pipeName[0])
        return false;

    auto openPipe = [pipeName]() {
        return CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    };

    cardPipe = openPipe();
    const bool launched = cardPipe == INVALID_HANDLE_VALUE && launchYaCardEmu();
    if (cardPipe == INVALID_HANDLE_VALUE && launched)
    {
        const DWORD deadline = GetTickCount() + 10000;
        do
        {
            cardPipe = openPipe();
            if (cardPipe != INVALID_HANDLE_VALUE)
                break;
            if (GetLastError() == ERROR_PIPE_BUSY)
                WaitNamedPipeA(pipeName, 100);
            else
                Sleep(100);
        } while (static_cast<LONG>(deadline - GetTickCount()) > 0);
    }
    else if (cardPipe == INVALID_HANDLE_VALUE &&
             GetLastError() == ERROR_PIPE_BUSY &&
             WaitNamedPipeA(pipeName, 3000))
    {
        cardPipe = openPipe();
    }

    if (cardPipe == INVALID_HANDLE_VALUE)
    {
        // Reported once per disconnected period; the retry runs every second.
        if (!connectFailureReported)
        {
            connectFailureReported = true;
            log_warn("Namco N2 card: YaCardEmu pipe is unavailable: %s (error=%lu). "
                     "The cabinet will report E51 until it is running.",
                     pipeName, GetLastError());
        }
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(cardPipe, &mode, nullptr, nullptr);
    connectFailureReported = false;
    log_info("Namco N2 card: connected /dev/ttyM2 to external YaCardEmu at %s",
             pipeName);
    return true;
}

// Callers must hold cardMutex.
bool cardPipeIsLive()
{
    if (cardPipe != INVALID_HANDLE_VALUE)
    {
        DWORD available = 0;
        if (PeekNamedPipe(cardPipe, nullptr, 0, nullptr, &available, nullptr))
            return true;

        log_warn("Namco N2 card: YaCardEmu closed the pipe; the cabinet will report E51");
        CloseHandle(cardPipe);
        cardPipe = INVALID_HANDLE_VALUE;
        connectFailureReported = true;
    }

    if (!getConfig()->n2YaCardEmuEnabled)
        return false;

    const DWORD now = GetTickCount();
    if (lastConnectAttempt != 0 &&
        static_cast<LONG>(now - lastConnectAttempt) < static_cast<LONG>(reconnectIntervalMs))
        return false;

    lastConnectAttempt = now;
    return connectCardPipe();
}
}

extern "C" int n2CardReaderOpen(const char *path, int)
{
    if (!path || std::strcmp(path, cardDevicePath) != 0)
        return -1;

    std::lock_guard<std::mutex> lock(cardMutex);
    if (!cardPipeIsLive())
    {
        errno = ENODEV;
        return -1;
    }
    return cardDescriptor;
}

extern "C" int n2CardReaderIsConnected(void)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    return cardPipeIsLive() ? 1 : 0;
}

extern "C" int n2CardReaderIsDescriptor(int fd)
{
    return fd == cardDescriptor;
}

extern "C" int n2CardReaderRead(int fd, void *buffer, size_t count)
{
    if (fd != cardDescriptor || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(cardMutex);
    if (cardPipe == INVALID_HANDLE_VALUE)
    {
        errno = ENODEV;
        return -1;
    }

    DWORD available = 0;
    if (!PeekNamedPipe(cardPipe, nullptr, 0, nullptr, &available, nullptr))
    {
        CloseHandle(cardPipe);
        cardPipe = INVALID_HANDLE_VALUE;
        errno = EIO;
        return -1;
    }
    if (!available)
    {
        errno = EAGAIN;
        return -1;
    }

    DWORD bytesRead = 0;
    const DWORD requested = static_cast<DWORD>(
        (std::min)(count, static_cast<size_t>(available)));
    if (!ReadFile(cardPipe, buffer, requested, &bytesRead, nullptr))
    {
        CloseHandle(cardPipe);
        cardPipe = INVALID_HANDLE_VALUE;
        errno = EIO;
        return -1;
    }
    return static_cast<int>(bytesRead);
}

extern "C" int n2CardReaderWrite(int fd, const void *buffer, size_t count)
{
    if (fd != cardDescriptor || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(cardMutex);
    if (cardPipe == INVALID_HANDLE_VALUE)
    {
        errno = ENODEV;
        return -1;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(cardPipe, buffer, static_cast<DWORD>(count), &bytesWritten, nullptr))
    {
        CloseHandle(cardPipe);
        cardPipe = INVALID_HANDLE_VALUE;
        errno = EIO;
        return -1;
    }
    return static_cast<int>(bytesWritten);
}

extern "C" int n2CardReaderClose(int fd)
{
    if (fd != cardDescriptor)
        return -1;

    std::lock_guard<std::mutex> lock(cardMutex);
    if (cardPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(cardPipe);
        cardPipe = INVALID_HANDLE_VALUE;
    }
    return 0;
}

extern "C" int n2CardReaderIoctl(int fd, unsigned long, void *)
{
    if (fd != cardDescriptor)
        return -1;
    // clSerialN2 uses Linux serial-specific RS485 ioctls. The named pipe is
    // already full duplex, so no host-side operation is required.
    return 0;
}

#endif
