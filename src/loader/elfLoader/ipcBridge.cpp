#if defined(_WIN32) || defined(__MINGW32__)
#include "ipcBridge.hpp"
#include "../log/log.h"
#include "symbolResolver.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace IpcBridge
{

    void initBridges()
    {
        log_debug("Initializing IPC Bridges...");

        MAP("shmget", bridgeShmget);
        MAP("shmat", bridgeShmat);
        MAP("shmctl", bridgeShmctl);
        MAP("shmdt", bridgeShmdt);

        MAP("ftok", bridgeFtok);
        MAP("semget", bridgeSemget);
        MAP("semop", bridgeSemop);
        MAP("semctl", bridgeSemctl);
    }
}
extern "C"
{
    int bridgeShmget(int key, size_t size, int shmflg)
    {
        log_debug("shmget(key=0x%08X, size=%zu, flags=0x%X)", key, size, shmflg);

        if (g_shmMap.find(key) != g_shmMap.end())
        {
            log_trace("shmget: Returning existing key %d", key);
            return key;
        }

        char mapName[64];
        sprintf(mapName, "Local\\Windy_SHM_%08X", key);

        HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)size, mapName);

        if (!hMap)
        {
            log_error("shmget failed: CreateFileMapping error %lu", GetLastError());
            return -1;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            log_trace("shmget: Opened existing mapping for key %d", key);
        }

        ShmInfo info;
        info.hMap = hMap;
        info.pMem = NULL;
        info.size = size;
        info.key = key;

        g_shmMap[key] = info;

        log_debug("shmget: Created mapping for key %d, size %zu", key, size);
        return key;
    }

    void *bridgeShmat(int shmid, const void *shmaddr, int shmflg)
    {
        log_trace("shmat(id=0x%08X, addr=%p, flags=0x%X)", shmid, shmaddr, shmflg);

        if (g_shmMap.find(shmid) == g_shmMap.end())
        {
            log_error("shmat failed: Invalid shmid %d", shmid);
            return (void *)-1;
        }

        ShmInfo &info = g_shmMap[shmid];

        if (info.pMem)
        {
            return info.pMem;
        }

        info.pMem = MapViewOfFile(info.hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        if (!info.pMem)
        {
            log_error("shmat failed: MapViewOfFile error %lu", GetLastError());
            return (void *)-1;
        }

        log_debug("shmat: attached id=0x%08X at %p", shmid, info.pMem);
        return info.pMem;
    }

    int bridgeShmctl(int shmid, int cmd, void *buf)
    {
        if (cmd == 0)
        { // IPC_RMID
            log_debug("shmctl: Removing id=%d", shmid);

            if (g_shmMap.find(shmid) != g_shmMap.end())
            {
                ShmInfo &info = g_shmMap[shmid];

                if (info.pMem)
                {
                    UnmapViewOfFile(info.pMem);
                }

                if (info.hMap)
                {
                    CloseHandle(info.hMap);
                }

                g_shmMap.erase(shmid);
            }
        }

        return 0;
    }

    int bridgeShmdt(const void *shmaddr)
    {
        log_trace("shmdt(%p)", shmaddr);
        return 0;
    }

    int bridgeFtok(const char *pathname, int projectId)
    {
        if (!pathname)
        {
            errno = EINVAL;
            return -1;
        }

        // FNV-1a, folded into the 24 bits ftok leaves below the project byte.
        uint32_t hash = 2166136261u;
        for (const char *at = pathname; *at; at++)
        {
            hash ^= static_cast<unsigned char>(*at);
            hash *= 16777619u;
        }

        const int key = static_cast<int>(((projectId & 0xFF) << 24) | (hash & 0x00FFFFFF));
        log_debug("ftok(\"%s\", %d) = 0x%08X", pathname, projectId, key);
        return key;
    }

    static std::map<int, SemInfo> g_semMap;
    static std::mutex g_semMutex;

    int bridgeSemget(int key, int semaphoreCount, int flags)
    {
        std::lock_guard<std::mutex> lock(g_semMutex);

        auto existing = g_semMap.find(key);
        if (existing != g_semMap.end())
            return key;

        if (semaphoreCount <= 0 || semaphoreCount > 64)
        {
            log_error("semget: refusing a set of %d semaphores for key 0x%08X", semaphoreCount, key);
            errno = EINVAL;
            return -1;
        }

        SemInfo info;
        info.handles.resize(semaphoreCount, nullptr);
        info.values.resize(semaphoreCount, 0);

        for (int index = 0; index < semaphoreCount; index++)
        {
            char name[64];
            // Local for the same reason the shared segments are; see bridgeShmget.
            sprintf(name, "Local\\Windy_SEM_%08X_%d", key, index);
            info.handles[index] = CreateSemaphoreA(nullptr, 0, 0x7FFFFFFF, name);
            if (!info.handles[index])
            {
                log_error("semget failed: CreateSemaphore error %lu", GetLastError());
                for (HANDLE handle : info.handles)
                    if (handle)
                        CloseHandle(handle);
                errno = ENOSPC;
                return -1;
            }
        }

        g_semMap[key] = info;
        log_debug("semget(key=0x%08X, count=%d, flags=0x%X)", key, semaphoreCount, flags);
        return key;
    }

    int bridgeSemop(int semaphoreId, struct LinuxSembuf *operations, unsigned int count)
    {
        std::lock_guard<std::mutex> lock(g_semMutex);

        auto entry = g_semMap.find(semaphoreId);
        if (entry == g_semMap.end() || !operations)
        {
            errno = EINVAL;
            return -1;
        }
        SemInfo &info = entry->second;

        for (unsigned int index = 0; index < count; index++)
        {
            const LinuxSembuf &operation = operations[index];
            if (operation.sem_num >= info.handles.size())
            {
                errno = EFBIG;
                return -1;
            }
            HANDLE handle = info.handles[operation.sem_num];

            if (operation.sem_op > 0)
            {
                ReleaseSemaphore(handle, operation.sem_op, nullptr);
                info.values[operation.sem_num] += operation.sem_op;
            }
            else if (operation.sem_op < 0)
            {
                // IPC_NOWAIT is 0x800 on Linux.
                const DWORD timeout = (operation.sem_flg & 0x800) ? 0 : INFINITE;
                for (int taken = 0; taken < -operation.sem_op; taken++)
                {
                    if (WaitForSingleObject(handle, timeout) != WAIT_OBJECT_0)
                    {
                        errno = EAGAIN;
                        return -1;
                    }
                    info.values[operation.sem_num]--;
                }
            }
        }

        return 0;
    }

    int bridgeSemctl(int semaphoreId, int semaphoreNumber, int command, ...)
    {
        constexpr int linuxIpcRmid = 0;
        constexpr int linuxSetval = 16;
        constexpr int linuxGetval = 12;

        std::lock_guard<std::mutex> lock(g_semMutex);

        auto entry = g_semMap.find(semaphoreId);
        if (entry == g_semMap.end())
        {
            errno = EINVAL;
            return -1;
        }
        SemInfo &info = entry->second;

        if (command == linuxIpcRmid)
        {
            for (HANDLE handle : info.handles)
                if (handle)
                    CloseHandle(handle);
            g_semMap.erase(entry);
            return 0;
        }

        if (semaphoreNumber < 0 || static_cast<size_t>(semaphoreNumber) >= info.handles.size())
        {
            errno = EFBIG;
            return -1;
        }

        if (command == linuxGetval)
            return info.values[semaphoreNumber];

        if (command == linuxSetval)
        {
            va_list arguments;
            va_start(arguments, command);
            const int wanted = va_arg(arguments, int);
            va_end(arguments);

            HANDLE handle = info.handles[semaphoreNumber];
            while (info.values[semaphoreNumber] > wanted)
            {
                if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0)
                    break;
                info.values[semaphoreNumber]--;
            }
            if (wanted > info.values[semaphoreNumber])
            {
                ReleaseSemaphore(handle, wanted - info.values[semaphoreNumber], nullptr);
                info.values[semaphoreNumber] = wanted;
            }
            return 0;
        }

        log_debug("semctl: command %d answered as a no-op", command);
        return 0;
    }
}

#endif