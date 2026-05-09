#pragma once

// ============================================================
// pthread_emu.h - Linux pthread API Emulation
// ============================================================

#ifndef PTHREAD_EMU_H
#define PTHREAD_EMU_H

#include <stdint.h>

// timespec forward declaration
struct timespec;

#ifdef __cplusplus
// ============================================================
// Main PthreadEmu Class
// ============================================================

class PthreadEmu {
public:
    // Init/Shutdown
    static void Initialize();
    static void Shutdown();

    // --------------------------------------------------------
    // Thread API
    // --------------------------------------------------------
    static int pthreadCreate(void* thread, const void* attr,
        void* (*start_routine)(void*), void* arg);
    static int pthreadJoin(uint32_t thread, void** retval);
    static int pthreadDetach(uint32_t thread);
    static void pthreadExit(void* retval);
    static uint32_t pthreadSelf();
    static int pthreadEqual(uint32_t t1, uint32_t t2);

    // Thread cancel (limited support)
    static int pthreadCancel(uint32_t thread);
    static int pthreadSetcancelstate(int state, int* oldstate);
    static int pthreadSetcanceltype(int type, int* oldtype);
    static void pthreadTestcancel();

    // Thread attributes
    static int pthreadAttrInit(void* attr);
    static int pthreadAttrDestroy(void* attr);
    static int pthreadAttrSetdetachstate(void* attr, int detachstate);
    static int pthreadAttrGetdetachstate(const void* attr, int* detachstate);
    static int pthreadAttrSetstacksize(void* attr, size_t stacksize);
    static int pthreadAttrGetstacksize(const void* attr, size_t* stacksize);

    // --------------------------------------------------------
    // Mutex API
    // --------------------------------------------------------
    static int pthreadMutexInit(void* mutex, const void* attr);
    static int pthreadMutexDestroy(void* mutex);
    static int pthreadMutexLock(void* mutex);
    static int pthreadMutexTrylock(void* mutex);
    static int pthreadMutexTimedlock(void* mutex, const struct timespec* abstime);
    static int pthreadMutexUnlock(void* mutex);

    // Mutex attributes
    static int pthreadMutexattrInit(void* attr);
    static int pthreadMutexattrDestroy(void* attr);
    static int pthreadMutexattrSettype(void* attr, int type);
    static int pthreadMutexattrGettype(const void* attr, int* type);
    static int pthreadMutexattrSetpshared(void* attr, int pshared);
    static int pthreadMutexattrGetpshared(const void* attr, int* pshared);

    // --------------------------------------------------------
    // Condition Variable API
    // --------------------------------------------------------
    static int pthreadCondInit(void* cond, const void* attr);
    static int pthreadCondDestroy(void* cond);
    static int pthreadCondWait(void* cond, void* mutex);
    static int pthreadCondTimedwait(void* cond, void* mutex,
        const struct timespec* abstime);
    static int pthreadCondSignal(void* cond);
    static int pthreadCondBroadcast(void* cond);

    // Cond attributes
    static int pthreadCondattrInit(void* attr);
    static int pthreadCondattrDestroy(void* attr);
    static int pthreadCondattrSetpshared(void* attr, int pshared);
    static int pthreadCondattrGetpshared(const void* attr, int* pshared);
    static int pthreadCondattrSetclock(void* attr, int clock_id);
    static int pthreadCondattrGetclock(const void* attr, int* clock_id);

    // --------------------------------------------------------
    // Read-Write Lock API
    // --------------------------------------------------------
    static int pthreadRwlockInit(void* rwlock, const void* attr);
    static int pthreadRwlockDestroy(void* rwlock);
    static int pthreadRwlockRdlock(void* rwlock);
    static int pthreadRwlockTryrdlock(void* rwlock);
    static int pthreadRwlockTimedrdlock(void* rwlock, const struct timespec* abstime);
    static int pthreadRwlockWrlock(void* rwlock);
    static int pthreadRwlockTrywrlock(void* rwlock);
    static int pthreadRwlockTimedwrlock(void* rwlock, const struct timespec* abstime);
    static int pthreadRwlockUnlock(void* rwlock);

    // Rwlock attributes
    static int pthreadRwlockattrInit(void* attr);
    static int pthreadRwlockattrDestroy(void* attr);
    static int pthreadRwlockattrSetpshared(void* attr, int pshared);
    static int pthreadRwlockattrGetpshared(const void* attr, int* pshared);

    // --------------------------------------------------------
    // Barrier API
    // --------------------------------------------------------
    static int pthreadBarrierInit(void* barrier, const void* attr,
        unsigned int count);
    static int pthreadBarrierDestroy(void* barrier);
    static int pthreadBarrierWait(void* barrier);

    // Barrier attributes
    static int pthreadBarrierattrInit(void* attr);
    static int pthreadBarrierattrDestroy(void* attr);
    static int pthreadBarrierattrSetpshared(void* attr, int pshared);
    static int pthreadBarrierattrGetpshared(const void* attr, int* pshared);

    // --------------------------------------------------------
    // Spinlock API
    // --------------------------------------------------------
    static int pthreadSpinInit(void* lock, int pshared);
    static int pthreadSpinDestroy(void* lock);
    static int pthreadSpinLock(void* lock);
    static int pthreadSpinTrylock(void* lock);
    static int pthreadSpinUnlock(void* lock);

    // --------------------------------------------------------
    // Once API
    // --------------------------------------------------------
    static int pthreadOnce(void* once_control, void (*init_routine)(void));

    // --------------------------------------------------------
    // Thread-Local Storage (TLS) API
    // --------------------------------------------------------
    static int pthreadKeyCreate(void* key, void (*destructor)(void*));
    static int pthreadKeyDelete(uint32_t key);
    static int pthreadSetSpecific(uint32_t key, const void* value);
    static void* pthreadGetSpecific(uint32_t key);

    // --------------------------------------------------------
    // Scheduling API (limited support)
    // --------------------------------------------------------
    static int pthreadSetSchedparam(uint32_t thread, int policy,
        const void* param);
    static int pthreadGetSchedparam(uint32_t thread, int* policy,
        void* param);
    static int schedYield();

    // --------------------------------------------------------
    // Misc
    // --------------------------------------------------------
    static int pthreadGetConcurrency();
    static int pthreadSetConcurrency(int new_level);
};
#endif

// ============================================================
// Semaphore API (C functions)
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif
    int emuSemInit(void* sem, int pshared, unsigned int value);
    int emuSemDestroy(void* sem);
    int emuSemWait(void* sem);
    int emuSemTrywait(void* sem);
    int emuSemTimedwait(void* sem, const struct timespec* abs_timeout);
    int emuSemPost(void* sem);
    int emuSemGetValue(void* sem, int* sval);
    void* emuSemOpen(const char* name, int oflag, int mode, unsigned int value);
    int emuSemClose(void* sem);
    int emuSemUnlink(const char* name);


    // Cleanup function for semaphores (called by PthreadEmu::Shutdown)
    void SemaphoreCleanup();
#ifdef __cplusplus
}
#endif

// ============================================================
// C API Exports
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

    void emuPthreadInit(void);
    void emuPthreadShutdown(void);

    int emuPthreadCreate(void* thread, const void* attr, void* (*start_routine)(void*), void* arg);
    int emuPthreadJoin(uint32_t thread, void** retval);
    int emuPthreadDetach(uint32_t thread);
    void emuPthreadExit(void* retval);
    uint32_t emuPthreadSelf(void);
    int emuPthreadEqual(uint32_t t1, uint32_t t2);

    int emuPthreadMutexInit(void* mutex, const void* attr);
    int emuPthreadMutexDestroy(void* mutex);
    int emuPthreadMutexLock(void* mutex);
    int emuPthreadMutexTrylock(void* mutex);
    int emuPthreadMutexTimedlock(void* mutex, const struct timespec* abstime);
    int emuPthreadMutexUnlock(void* mutex);

#ifdef __cplusplus
}
#endif

#endif // PTHREAD_EMU_H
