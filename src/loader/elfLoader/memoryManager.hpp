#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

struct mallinfo
{
    int arena;     // Non-mmapped space allocated from system (bytes)
    int ordblks;   // Number of free chunks
    int smblks;    // Number of free fastbin blocks
    int hblks;     // Number of mmapped regions
    int hblkhd;    // Space in mmapped regions (bytes)
    int usmblks;   // Maximum total allocated space (bytes)
    int fsmblks;   // Space in freed fastbin blocks (bytes)
    int uordblks;  // Total allocated space (bytes)
    int fordblks;  // Total free space (bytes)
    int keepcost;  // Top-most releasable space (bytes)
};


class MemoryManager {
public:
    static MemoryManager& GetInstance() {
        static MemoryManager instance;
        return instance;
    }

    MemoryManager(MemoryManager const&) = delete;
    void operator=(MemoryManager const&) = delete;

    void* AllocateExecutable(size_t size, void* requestedAddr = nullptr);
    void* ReserveAddressSpace(size_t size, void* requestedAddr);
    bool CommitSegment(uintptr_t vaddr, uintptr_t memsz);

    static void* customMalloc(size_t size);
    static struct mallinfo customMallinfo(void);
    static void* customCalloc(size_t nmemb, size_t size);
    static void* customRealloc(void* ptr, size_t size);
    static void customFree(void* ptr);
    static void* customMemalign(size_t alignment, size_t size);
    static int customPosixMemalign(void **ptr, size_t alignment, size_t size);
    static void* customMemmove(void* dest, const void* src, size_t n);
    static wchar_t* customWmemmove(wchar_t* dest, const wchar_t* src, size_t n);
    static int customStrcoll(const char *s1, const char *s2);
    static size_t customStrxfrm(char *dest, const char *src, size_t n);
    static char *customStrndup(const char *src, size_t size);
    static char *customStrdup(const char *s);

private:
    MemoryManager() {}
    ~MemoryManager() {}
};
