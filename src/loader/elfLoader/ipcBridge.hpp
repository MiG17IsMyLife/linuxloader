#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <windows.h>
#include <map>
#include <mutex>
#include <stddef.h>
#include <vector>

struct ShmInfo
{
    HANDLE hMap;
    void *pMem;
    size_t size;
    int key;
};

static std::map<int, ShmInfo> g_shmMap;

struct SemInfo
{
    std::vector<HANDLE> handles;
    std::vector<int> values;
};

namespace IpcBridge 
{
    void initBridges();
}

/*
 * The i386 Linux layout of the third semop()/semctl() argument.  Only the
 * fields the guest fills are named; the union member it passes for semctl is
 * always the first word.
 */
struct LinuxSembuf
{
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

extern "C"
{
    int bridgeShmget(int key, size_t size, int shmflg);
    void *bridgeShmat(int shmid, const void *shmaddr, int shmflg);
    int bridgeShmctl(int shmid, int cmd, void *buf);
    int bridgeShmdt(const void *shmaddr);

    int bridgeFtok(const char *pathname, int projectId);
    int bridgeSemget(int key, int semaphoreCount, int flags);
    int bridgeSemop(int semaphoreId, struct LinuxSembuf *operations, unsigned int count);
    int bridgeSemctl(int semaphoreId, int semaphoreNumber, int command, ...);
};