#pragma once

// ============================================================
// pthread_internal.h
// ============================================================

// Windows headers must come first
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <stdint.h>

// ============================================================
// timespec
// ============================================================
#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

// ============================================================
// Linux glibc i386
// ============================================================

// Mutex types (glibc NPTL)
#define LINUX_PTHREAD_MUTEX_NORMAL      0
#define LINUX_PTHREAD_MUTEX_RECURSIVE   1
#define LINUX_PTHREAD_MUTEX_ERRORCHECK  2
#define LINUX_PTHREAD_MUTEX_DEFAULT     LINUX_PTHREAD_MUTEX_NORMAL

// Mutex protocol
#define LINUX_PTHREAD_PRIO_NONE         0
#define LINUX_PTHREAD_PRIO_INHERIT      1
#define LINUX_PTHREAD_PRIO_PROTECT      2

// Process shared
#define LINUX_PTHREAD_PROCESS_PRIVATE   0
#define LINUX_PTHREAD_PROCESS_SHARED    1

// Detach state
#define LINUX_PTHREAD_CREATE_JOINABLE   0
#define LINUX_PTHREAD_CREATE_DETACHED   1

// Cancel state
#define LINUX_PTHREAD_CANCEL_ENABLE     0
#define LINUX_PTHREAD_CANCEL_DISABLE    1

// Cancel type
#define LINUX_PTHREAD_CANCEL_DEFERRED       0
#define LINUX_PTHREAD_CANCEL_ASYNCHRONOUS   1

// Barrier serial thread
#define LINUX_PTHREAD_BARRIER_SERIAL_THREAD (-1)

// Once init value
#define LINUX_PTHREAD_ONCE_INIT         0

// Error codes (Linux errno values)
#define LINUX_EAGAIN        11
#define LINUX_ENOMEM        12
#define LINUX_EINVAL        22
#define LINUX_EDEADLK       35
#define LINUX_EBUSY         16
#define LINUX_EPERM         1
#define LINUX_ESRCH         3
#define LINUX_ETIMEDOUT     110

// ============================================================
// Windows side structures
// ============================================================

// Mutex (CRITICAL_SECTION + recursive support)
typedef struct PthreadMutexInternal {
    CRITICAL_SECTION    cs;
    int                 type;           // NORMAL, RECURSIVE, ERRORCHECK
    volatile DWORD      owner_thread;   // For ERRORCHECK
    volatile int        lock_count;     // For ERRORCHECK
    volatile int        initialized;    // Magic number for validation
} PthreadMutexInternal;

#define MUTEX_INIT_MAGIC    0x4D555458  // "MUTX"

// Condition Variable
typedef struct PthreadCondInternal {
    CONDITION_VARIABLE  cv;
    int                 clock_id;       // CLOCK_REALTIME or CLOCK_MONOTONIC
    volatile int        initialized;
} PthreadCondInternal;

#define COND_INIT_MAGIC     0x434F4E44  // "COND"

// Read-Write Lock
typedef struct PthreadRwlockInternal {
    SRWLOCK             srw;
    volatile int        initialized;
} PthreadRwlockInternal;

#define RWLOCK_INIT_MAGIC   0x52574C4B  // "RWLK"

// Barrier
typedef struct PthreadBarrierInternal {
    CRITICAL_SECTION    cs;
    CONDITION_VARIABLE  cv;
    unsigned int        threshold;
    unsigned int        count;
    unsigned int        generation;
    volatile int        initialized;
} PthreadBarrierInternal;

#define BARRIER_INIT_MAGIC  0x42415252  // "BARR"

// Spinlock
typedef struct PthreadSpinInternal {
    volatile LONG       lock;           // 0=unlocked, 1=locked
    volatile int        initialized;
} PthreadSpinInternal;

#define SPIN_INIT_MAGIC     0x5350494E  // "SPIN"

// Thread info
typedef struct PthreadThreadInternal {
    HANDLE              handle;
    DWORD               win_thread_id;
    uint32_t            linux_thread_id;
    void* (*start_routine)(void*);
    void* arg;
    void* retval;
    volatile int        detached;
    volatile int        exited;
} PthreadThreadInternal;

// Thread attribute
typedef struct PthreadAttrInternal {
    int     detach_state;
    size_t  stack_size;
    int     sched_policy;
    int     sched_priority;
} PthreadAttrInternal;

// Mutex attribute
typedef struct PthreadMutexAttrInternal {
    int     type;
    int     protocol;
    int     pshared;
} PthreadMutexAttrInternal;

// Cond attribute
typedef struct PthreadCondAttrInternal {
    int     pshared;
    int     clock_id;
} PthreadCondAttrInternal;

// Rwlock attribute
typedef struct PthreadRwlockAttrInternal {
    int     pshared;
} PthreadRwlockAttrInternal;

// Barrier attribute
typedef struct PthreadBarrierAttrInternal {
    int     pshared;
} PthreadBarrierAttrInternal;

// ============================================================
// Mapping Manager
// ============================================================

class PthreadMapper {
public:
    static void Initialize();
    static void Shutdown();

    // Mutex mapping
    static PthreadMutexInternal* GetOrCreateMutex(void* linux_mutex);
    static PthreadMutexInternal* FindMutex(void* linux_mutex);
    static void                     DestroyMutex(void* linux_mutex);

    // Cond mapping
    static PthreadCondInternal* GetOrCreateCond(void* linux_cond);
    static PthreadCondInternal* FindCond(void* linux_cond);
    static void                     DestroyCond(void* linux_cond);

    // Rwlock mapping
    static PthreadRwlockInternal* GetOrCreateRwlock(void* linux_rwlock);
    static PthreadRwlockInternal* FindRwlock(void* linux_rwlock);
    static void                     DestroyRwlock(void* linux_rwlock);

    // Barrier mapping
    static PthreadBarrierInternal* GetOrCreateBarrier(void* linux_barrier, unsigned int count);
    static PthreadBarrierInternal* FindBarrier(void* linux_barrier);
    static void                     DestroyBarrier(void* linux_barrier);

    // Spin mapping
    static PthreadSpinInternal* GetOrCreateSpin(void* linux_spin);
    static PthreadSpinInternal* FindSpin(void* linux_spin);
    static void                     DestroySpin(void* linux_spin);

    // Thread mapping
    static PthreadThreadInternal* CreateThread(uint32_t* out_linux_tid);
    static void                     RegisterThread(uint32_t linux_tid, DWORD win_tid);
    static PthreadThreadInternal* FindThread(uint32_t linux_tid);
    static PthreadThreadInternal* FindThreadByWinId(DWORD win_tid);
    static void                     DestroyThread(uint32_t linux_tid);
    static uint32_t                 GetCurrentLinuxTid();

private:
    static SRWLOCK                  s_lock;
    static volatile bool            s_initialized;
};

// ============================================================
// Utility 
// ============================================================

// Linux timespec -> Windows milliseconds
inline DWORD TimespecToMilliseconds(const struct timespec* ts) {
    if (!ts) return INFINITE;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULARGE_INTEGER now;
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;

    // FILETIME is 100ns since 1601, timespec is since 1970
    // Difference: 11644473600 seconds
    const uint64_t EPOCH_DIFF = 116444736000000000ULL;

    // Convert timespec to FILETIME units (100ns)
    uint64_t target = (uint64_t)ts->tv_sec * 10000000ULL +
        (uint64_t)ts->tv_nsec / 100ULL +
        EPOCH_DIFF;

    if (target <= now.QuadPart) {
        return 0;  // Already passed
    }

    uint64_t diff_100ns = target - now.QuadPart;
    uint64_t diff_ms = diff_100ns / 10000;

    if (diff_ms > 0xFFFFFFFF) {
        return INFINITE;
    }

    return (DWORD)diff_ms;
}
