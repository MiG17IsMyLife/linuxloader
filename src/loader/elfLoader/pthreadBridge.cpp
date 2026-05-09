#if defined(_WIN32) || defined(__MINGW32__)
#include "pthreadBridge.hpp"
#include "pthread/pthreadEmu.hpp"
#include "symbolResolver.hpp"
#include "libcBridge.hpp"
#include "../log/log.h"

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace PthreadBridge
{

    extern "C"
    {
        int emuSemInit(void *sem, int pshared, unsigned int value);
        int emuSemDestroy(void *sem);
        int emuSemWait(void *sem);
        int emuSemTrywait(void *sem);
        int emuSemPost(void *sem);
        int emuSemGetValue(void *sem, int *sval);
        void* emuSemOpen(const char* name, int oflag, int mode, unsigned int value);
        int emuSemClose(void* sem);
        int emuSemUnlink(const char* name);


        int bridge_sem_init(void *sem, int pshared, unsigned int value)
        {
            return emuSemInit(sem, pshared, value);
        }
        int bridge_sem_destroy(void *sem)
        {
            return emuSemDestroy(sem);
        }
        int bridge_sem_wait(void *sem)
        {
            return emuSemWait(sem);
        }
        int bridge_sem_trywait(void *sem)
        {
            return emuSemTrywait(sem);
        }
        int bridge_sem_post(void *sem)
        {
            return emuSemPost(sem);
        }
        int bridge_sem_getvalue(void *sem, int *sval)
        {
            return emuSemGetValue(sem, sval);
        }
        void* bridge_sem_open(const char* name, int oflag, int mode, unsigned int value)
        {
            return emuSemOpen(name, oflag, mode, value);
        }
        int bridge_sem_close(void* sem)
        {
            return emuSemClose(sem);
        }
        int bridge_sem_unlink(const char* name)
        {
            return emuSemUnlink(name);
        }

    }

    void initBridges()
    {
        log_info("Initializing Pthread Emulation...");

        PthreadEmu::Initialize();

        // Thread functions
        MAP("pthread_create", PthreadEmu::pthreadCreate);
        MAP("pthread_join", PthreadEmu::pthreadJoin);
        MAP("pthread_detach", PthreadEmu::pthreadDetach);
        MAP("pthread_exit", PthreadEmu::pthreadExit);
        MAP("pthread_self", PthreadEmu::pthreadSelf);
        MAP("pthread_equal", PthreadEmu::pthreadEqual);
        MAP("pthread_cancel", PthreadEmu::pthreadCancel);

        // Thread attributes
        MAP("pthread_attr_init", PthreadEmu::pthreadAttrInit);
        MAP("pthread_attr_destroy", PthreadEmu::pthreadAttrDestroy);
        MAP("pthread_attr_setstacksize", PthreadEmu::pthreadAttrSetstacksize);
        MAP("pthread_attr_getstacksize", PthreadEmu::pthreadAttrGetstacksize);
        MAP("pthread_attr_setdetachstate", PthreadEmu::pthreadAttrSetdetachstate);
        MAP("pthread_attr_getdetachstate", PthreadEmu::pthreadAttrGetdetachstate);

        // Scheduling
        MAP("pthread_setschedparam", PthreadEmu::pthreadSetSchedparam);
        MAP("pthread_getschedparam", PthreadEmu::pthreadGetSchedparam);
        MAP("sched_yield", PthreadEmu::schedYield);
        MAP("pthread_attr_setschedparam", LibcBridge::bridgeStubSuccess);
        MAP("pthread_attr_setschedpolicy", LibcBridge::bridgeStubSuccess);
        MAP("sched_getaffinity", LibcBridge::bridgeStubSuccess);
        MAP("sched_setaffinity", LibcBridge::bridgeStubSuccess);

        // Mutex functions
        MAP("pthread_mutex_init", PthreadEmu::pthreadMutexInit);
        MAP("pthread_mutex_destroy", PthreadEmu::pthreadMutexDestroy);
        MAP("pthread_mutex_lock", PthreadEmu::pthreadMutexLock);
        MAP("pthread_mutex_trylock", PthreadEmu::pthreadMutexTrylock);
        MAP("pthread_mutex_timedlock", PthreadEmu::pthreadMutexTimedlock);
        MAP("pthread_mutex_unlock", PthreadEmu::pthreadMutexUnlock);

        // Mutex attributes
        MAP("pthread_mutexattr_init", PthreadEmu::pthreadMutexattrInit);
        MAP("pthread_mutexattr_destroy", PthreadEmu::pthreadMutexattrDestroy);
        MAP("pthread_mutexattr_settype", PthreadEmu::pthreadMutexattrSettype);
        MAP("pthread_mutexattr_gettype", PthreadEmu::pthreadMutexattrGettype);

        // Condition variable functions
        MAP("pthread_cond_init", PthreadEmu::pthreadCondInit);
        MAP("pthread_cond_destroy", PthreadEmu::pthreadCondDestroy);
        MAP("pthread_cond_wait", PthreadEmu::pthreadCondWait);
        MAP("pthread_cond_timedwait", PthreadEmu::pthreadCondTimedwait);
        MAP("pthread_cond_signal", PthreadEmu::pthreadCondSignal);
        MAP("pthread_cond_broadcast", PthreadEmu::pthreadCondBroadcast);

        // Condition variable attributes
        MAP("pthread_condattr_init", PthreadEmu::pthreadCondattrInit);
        MAP("pthread_condattr_destroy", PthreadEmu::pthreadCondattrDestroy);

        // Read-write lock functions
        MAP("pthread_rwlock_init", PthreadEmu::pthreadRwlockInit);
        MAP("pthread_rwlock_destroy", PthreadEmu::pthreadRwlockDestroy);
        MAP("pthread_rwlock_rdlock", PthreadEmu::pthreadRwlockRdlock);
        MAP("pthread_rwlock_tryrdlock", PthreadEmu::pthreadRwlockTryrdlock);
        MAP("pthread_rwlock_wrlock", PthreadEmu::pthreadRwlockWrlock);
        MAP("pthread_rwlock_trywrlock", PthreadEmu::pthreadRwlockTrywrlock);
        MAP("pthread_rwlock_unlock", PthreadEmu::pthreadRwlockUnlock);

        // Once control
        MAP("pthread_once", PthreadEmu::pthreadOnce);

        // TLS functions
        MAP("pthread_key_create", PthreadEmu::pthreadKeyCreate);
        MAP("pthread_key_delete", PthreadEmu::pthreadKeyDelete);
        MAP("pthread_setspecific", PthreadEmu::pthreadSetSpecific);
        MAP("pthread_getspecific", PthreadEmu::pthreadGetSpecific);

        // Barrier functions
        MAP("pthread_barrier_init", PthreadEmu::pthreadBarrierInit);
        MAP("pthread_barrier_destroy", PthreadEmu::pthreadBarrierDestroy);
        MAP("pthread_barrier_wait", PthreadEmu::pthreadBarrierWait);

        // Spinlock functions
        MAP("pthread_spin_init", PthreadEmu::pthreadSpinInit);
        MAP("pthread_spin_destroy", PthreadEmu::pthreadSpinDestroy);
        MAP("pthread_spin_lock", PthreadEmu::pthreadSpinLock);
        MAP("pthread_spin_trylock", PthreadEmu::pthreadSpinTrylock);
        MAP("pthread_spin_unlock", PthreadEmu::pthreadSpinUnlock);
        MAP("sem_init", bridge_sem_init);
        MAP("sem_destroy", bridge_sem_destroy);
        MAP("sem_wait", bridge_sem_wait);
        MAP("sem_trywait", bridge_sem_trywait);
        MAP("sem_post", bridge_sem_post);
        MAP("sem_getvalue", bridge_sem_getvalue);
        MAP("sem_open", bridge_sem_open);
        MAP("sem_close", bridge_sem_close);
        MAP("sem_unlink", bridge_sem_unlink);


        MAP("pthread_once", bridgePthreadOnce);

        MAP("sched_get_priority_max", bridgeSchedGetPriorityMax);
        MAP("sched_get_priority_min", bridgeSchedGetPriorityMin);
    }

    int bridgePthreadOnce(int *once_control, void (*init_routine)(void))
    {
        if (once_control && *once_control == 0)
        {
            *once_control = 1; // Running
            if (init_routine)
            {
                init_routine();
            }
            *once_control = 2; // Completed
        }
        return 0; // Success
    }
} // namespace PthreadBridge

extern "C" int bridgeSchedGetPriorityMax(int policy)
{
    return 99;
}
extern "C" int bridgeSchedGetPriorityMin(int policy)
{
    return 0;
}

#endif