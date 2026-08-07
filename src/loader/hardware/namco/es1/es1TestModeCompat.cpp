#include "es1TestModeCompat.h"

#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../log/log.h"
#include "../../../../minhook/include/MinHook.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
constexpr size_t ClockDifferenceFileSize = 76;
constexpr size_t ClockDifferenceDateOffset = 2 + 28 + 28;
constexpr uint64_t MinimumValidDateTicks = 190000000000000000ULL;
constexpr uint64_t MaximumValidDateTicks = 500000000000000000ULL;
constexpr size_t ClockAdjustmentOffset = 0x4d8;
constexpr size_t ClockAdjustmentSize = 0x1c;

using TestModeConstructor = void (*)(void *);
TestModeConstructor g_originalConstructor = nullptr;

bool isClockDataValid(const std::filesystem::path &path)
{
    std::error_code error;
    if (std::filesystem::file_size(path, error) != ClockDifferenceFileSize || error)
        return false;

    std::array<unsigned char, ClockDifferenceFileSize> contents{};
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char *>(contents.data()), contents.size()))
        return false;

    if (contents[0] != contents[ClockDifferenceFileSize - 2] ||
        contents[1] != contents[ClockDifferenceFileSize - 1])
        return false;

    uint64_t dateTicks = 0;
    std::memcpy(&dateTicks, contents.data() + ClockDifferenceDateOffset,
                sizeof(dateTicks));
    return dateTicks >= MinimumValidDateTicks && dateTicks <= MaximumValidDateTicks;
}

std::filesystem::path recoveryPathFor(const std::filesystem::path &path,
                                      std::error_code &error)
{
    const std::filesystem::path slotDirectory = path.parent_path().parent_path();
    const std::filesystem::path recoveryDirectory = slotDirectory.parent_path() /
                                                    "pacloader-recovery";
    std::filesystem::create_directory(recoveryDirectory, error);
    if (error)
        return {};

    const std::string baseName = slotDirectory.filename().string() +
                                 "-clock_difference.bin.corrupt";
    std::filesystem::path recovery = recoveryDirectory / baseName;
    for (unsigned int suffix = 1; std::filesystem::exists(recovery, error) && !error;
         ++suffix)
    {
        recovery = recoveryDirectory / (baseName + "." + std::to_string(suffix));
    }
    return error ? std::filesystem::path{} : recovery;
}

void quarantineInvalidClockData(const std::filesystem::path &path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error || isClockDataValid(path))
        return;

    const std::filesystem::path recovery = recoveryPathFor(path, error);
    if (error || recovery.empty())
    {
        log_warn("System ES1: could not prepare clock-data recovery for %s: %s",
                 path.string().c_str(), error.message().c_str());
        return;
    }

    std::filesystem::rename(path, recovery, error);
    if (error)
    {
        log_warn("System ES1: could not quarantine invalid clock data %s: %s",
                 path.string().c_str(), error.message().c_str());
        return;
    }

    log_warn("System ES1: quarantined invalid clock data as %s",
             recovery.string().c_str());
}

void testModeConstructor(void *self)
{
    if (self)
    {
        /* The guest publishes the singleton before these clock adjustments
         * are initialized, so its clDateTime members can consume heap data. */
        std::memset(static_cast<unsigned char *>(self) + ClockAdjustmentOffset,
                    0, ClockAdjustmentSize);
    }
    g_originalConstructor(self);
}
}

int es1PrepareTestModeCompat(const char *elfPath)
{
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(
        elfPath && *elfPath ? std::filesystem::path(elfPath)
                            : std::filesystem::path("a.elf"),
        error);
    if (error)
    {
        log_warn("System ES1: could not resolve the game directory for clock-data validation");
        return -1;
    }

    const std::filesystem::path directory = executable.parent_path();
    quarantineInvalidClockData(directory / "save0" / "testmode" /
                               "clock_difference.bin");
    quarantineInvalidClockData(directory / "save1" / "testmode" /
                               "clock_difference.bin");
    return 0;
}

int es1InstallTestModeCompatHook()
{
    static constexpr char ConstructorName[] = "_ZN10clTestModeC1Ev";
    std::string module;
    void *target = SymbolResolver::GetInstance().ResolveSymbol(ConstructorName, &module);
    if (!target || module == "UNRESOLVED_STUB")
    {
        log_error("System ES1: required clTestMode constructor target was not found");
        return -1;
    }

    const MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void *>(testModeConstructor),
        reinterpret_cast<void **>(&g_originalConstructor));
    if (status != MH_OK || !g_originalConstructor)
    {
        log_error("System ES1: failed to install clTestMode initialization fix (MinHook status %d)",
                  static_cast<int>(status));
        return -1;
    }

    log_info("System ES1: installed clTestMode initialization-order fix at %p", target);
    return 0;
}
