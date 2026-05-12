#if defined(_WIN32) || defined(__MINGW32__)
#include "memoryManager.hpp"
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <wchar.h>

#include "../log/log.h"
#include <stdio.h>

void *MemoryManager::AllocateExecutable(size_t size, void *requestedAddr)
{
    void* ptr = nullptr;
    if (requestedAddr) {
        ptr = VirtualAlloc(requestedAddr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    }
    if (!ptr) {
        ptr = VirtualAlloc(requestedAddr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    if (!ptr && requestedAddr != nullptr)
    {
        uint8_t *scan = static_cast<uint8_t *>(requestedAddr);
        uint8_t *end = scan + size;
        bool canUse = true;

        while (scan < end)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(scan, &mbi, sizeof(mbi)))
            {
                canUse = false;
                break;
            }

            SIZE_T regionSize = mbi.RegionSize;
            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + regionSize;
            if (regionEnd > reinterpret_cast<uintptr_t>(end))
                regionSize = end - scan;

            if (mbi.State == MEM_COMMIT)
            {
                DWORD oldProtect;
                if (!VirtualProtect(scan, regionSize, PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    log_error("AllocateExecutable: VirtualProtect failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = false;
                    break;
                }
            }
            else if (mbi.State == MEM_FREE)
            {
                if (!VirtualAlloc(scan, regionSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                {
                    log_error("AllocateExecutable: VirtualAlloc(FREE) failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = false;
                    break;
                }
            }
            else if (mbi.State == MEM_RESERVE)
            {
                if (!VirtualAlloc(scan, regionSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                {
                    log_error("AllocateExecutable: VirtualAlloc(RESERVE) failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = false;
                    break;
                }
            }

            scan = reinterpret_cast<uint8_t *>(mbi.BaseAddress) + mbi.RegionSize;
        }

        if (canUse)
        {
            log_info("AllocateExecutable: Using taken-over memory at %p (%zu bytes)", requestedAddr, size);
            ptr = requestedAddr;
        }
    }
    if (!ptr && requestedAddr != nullptr)
    {
        DWORD err = GetLastError();
        log_error("VirtualAlloc failed for address %p, size %zu. Error: %lu", requestedAddr, size, err);
        
        uint8_t* scan = static_cast<uint8_t*>(requestedAddr);
        uint8_t* end = scan + size;
        
        log_error("Scanning region %p to %p for conflicts...", scan, end);
        while (scan < end)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(scan, &mbi, sizeof(mbi)))
            {
                if (mbi.State != MEM_FREE)
                {
                    log_error("Conflict found at %p - RegionSize: 0x%zx, State: 0x%lx, Type: 0x%lx",
                              mbi.BaseAddress, mbi.RegionSize, mbi.State, mbi.Type);
                    break;
                }
                scan += mbi.RegionSize;
            }
            else
            {
                log_error("VirtualQuery failed at %p", scan);
                break;
            }
        }
    }
    return ptr;
}

#include <psapi.h>

void* MemoryManager::ReserveAddressSpace(size_t size, void* requestedAddr)
{
    void* result = VirtualAlloc(requestedAddr, size, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (result) {
        log_info("MemoryManager: Pre-reserved address space: %p - %p (%zu bytes)", result, (char *)result + size, size);
        return result;
    }

    if (!requestedAddr)
        return nullptr;

    log_warn("MemoryManager: Pre-reserve failed at %p (%zu bytes). Taking over existing committed memory...",
             requestedAddr, size);

    uintptr_t scanAddr = reinterpret_cast<uintptr_t>(requestedAddr);
    uintptr_t alignedMax = scanAddr + size;
    bool allOk = true;

    while (scanAddr < alignedMax)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void *>(scanAddr), &mbi, sizeof(mbi)))
            break;

        if (mbi.State == MEM_FREE)
        {
            SIZE_T freeSize = mbi.RegionSize;
            uintptr_t freeEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + freeSize;
            if (freeEnd > alignedMax)
                freeSize = alignedMax - scanAddr;

            void *allocated = VirtualAlloc(reinterpret_cast<void *>(scanAddr), freeSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (allocated)
            {
                log_info("  Claimed free region at %p (%zu bytes)", allocated, freeSize);
            }
            else
            {
                log_error("  Failed to claim free region at %p (%zu bytes). Error: %lu", reinterpret_cast<void *>(scanAddr), freeSize, GetLastError());
                allOk = false;
            }
        }
        else if (mbi.State == MEM_COMMIT)
        {
            char moduleName[MAX_PATH] = {0};
            if (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED)
                GetMappedFileNameA(GetCurrentProcess(), reinterpret_cast<void *>(scanAddr), moduleName, MAX_PATH);

            log_warn("  Taking over committed region at %p (Size=0x%zx, Type=0x%lx) %s%s", mbi.BaseAddress, mbi.RegionSize, mbi.Type,
                     moduleName[0] ? "Module: " : "", moduleName[0] ? moduleName : "");

            DWORD oldProtect;
            SIZE_T protectSize = mbi.RegionSize;
            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + protectSize;
            if (regionEnd > alignedMax)
                protectSize = alignedMax - scanAddr;

            if (!VirtualProtect(reinterpret_cast<void *>(scanAddr), protectSize, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                log_error("  VirtualProtect failed at %p (%zu bytes). Error: %lu", reinterpret_cast<void *>(scanAddr), protectSize, GetLastError());
                allOk = false;
            }
        }
        else if (mbi.State == MEM_RESERVE)
        {
            SIZE_T commitSize = mbi.RegionSize;
            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + commitSize;
            if (regionEnd > alignedMax)
                commitSize = alignedMax - scanAddr;

            void *committed = VirtualAlloc(reinterpret_cast<void *>(scanAddr), commitSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (committed)
            {
                log_info("  Committed reserved region at %p (%zu bytes)", committed, commitSize);
            }
            else
            {
                log_error("  Failed to commit reserved region at %p (%zu bytes). Error: %lu", reinterpret_cast<void *>(scanAddr), commitSize, GetLastError());
                allOk = false;
            }
        }

        scanAddr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return allOk ? requestedAddr : nullptr;
}

bool MemoryManager::CommitSegment(uintptr_t vaddr, uintptr_t memsz)
{
    uintptr_t alignedVAddr = vaddr & ~0xFFF;
    uintptr_t vEnd = vaddr + memsz;
    uintptr_t alignedEnd = (vEnd + 0xFFF) & ~0xFFF;
    uintptr_t alignedSize = alignedEnd - alignedVAddr;

    LPVOID result = VirtualAlloc((LPVOID)alignedVAddr, alignedSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!result) {
        log_error("MemoryManager: VirtualAlloc MEM_COMMIT failed! VAddr: %p, Size: 0x%zx, Error: %lu",
                  (void*)alignedVAddr, alignedSize, GetLastError());
    }
    return result != NULL;
}

void *MemoryManager::customMemmove(void *dest, const void *src, size_t n)
{
    return memmove(dest, src, n);
}

wchar_t *MemoryManager::customWmemmove(wchar_t *dest, const wchar_t *src, size_t n)
{
    return wmemmove(dest, src, n);
}

int MemoryManager::customStrcoll(const char *s1, const char *s2)
{
    return strcoll(s1, s2);
}

size_t MemoryManager::customStrxfrm(char *dest, const char *src, size_t n)
{
    return strxfrm(dest, src, n);
}

void *MemoryManager::customMalloc(size_t size)
{
    return _aligned_malloc(size, 16);
}

struct mallinfo MemoryManager::customMallinfo(void)
{
    struct mallinfo Info = {};

    HANDLE Heap = GetProcessHeap();
    if (!Heap)
        return Info;

    HeapLock(Heap);

    PROCESS_HEAP_ENTRY Entry = {};
    while (HeapWalk(Heap, &Entry))
    {
        if (Entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
        {
            Info.uordblks += (int)Entry.cbData;
        }
        else
        {
            Info.fordblks += (int)Entry.cbData;
        }
    }

    HeapUnlock(Heap);

    Info.arena = Info.uordblks + Info.fordblks;
    return Info;
}

void MemoryManager::customFree(void *ptr)
{
    _aligned_free(ptr);
}

void *MemoryManager::customCalloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb)
    {
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = _aligned_malloc(total, 16);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *MemoryManager::customRealloc(void *ptr, size_t size)
{
    return _aligned_realloc(ptr, size, 16);
}

void *MemoryManager::customMemalign(size_t alignment, size_t size)
{
    return _aligned_malloc(size, alignment);
} 

int MemoryManager::customPosixMemalign(void **ptr, size_t alignment, size_t size)
{
    if (alignment % sizeof(void*) != 0 || (alignment & (alignment - 1)) != 0)
    {
        return EINVAL;
    }

    *ptr = _aligned_malloc(size, alignment);
    return *ptr ? 0 : ENOMEM;
}

char *MemoryManager::customStrndup(const char *src, size_t size)
{
    size_t len = strnlen(src, size);
    len = len < size ? len : size;
    char *dst = (char *)_aligned_malloc(len + 1, 16);
    if (!dst)
        return NULL;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

char *MemoryManager::customStrdup(const char *s)
{
    if (!s)
        return nullptr;
    size_t len = strlen(s) + 1;
    void *ptr = _aligned_malloc(len, 16);
    if (ptr)
        memcpy(ptr, s, len);
    return (char *)ptr;
}

#endif