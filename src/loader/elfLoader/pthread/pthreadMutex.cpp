// ============================================================
// pthread_mutex.cpp - Mutex Emulation
// ============================================================

#include "pthreadEmu.hpp"
#include "pthreadInternal.hpp"
#include <pthread.h>

// ============================================================
// Mutex Attribute Functions
// ============================================================

int PthreadEmu::pthreadMutexattrInit(void* attr) {
    if (!attr) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = new PthreadMutexAttrInternal;
    a->type = LINUX_PTHREAD_MUTEX_DEFAULT;
    a->protocol = LINUX_PTHREAD_PRIO_NONE;
    a->pshared = LINUX_PTHREAD_PROCESS_PRIVATE;
    
    // Store pointer in Linux attr space
    *(void**)attr = a;
    return 0;
}

int PthreadEmu::pthreadMutexattrDestroy(void* attr) {
    if (!attr) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (a) {
        delete a;
        *(void**)attr = nullptr;
    }
    return 0;
}

int PthreadEmu::pthreadMutexattrSettype(void* attr, int type) {
    if (!attr) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;
    
    if (type != LINUX_PTHREAD_MUTEX_NORMAL &&
        type != LINUX_PTHREAD_MUTEX_RECURSIVE &&
        type != LINUX_PTHREAD_MUTEX_ERRORCHECK) {
        return LINUX_EINVAL;
    }
    
    a->type = type;
    return 0;
}

int PthreadEmu::pthreadMutexattrGettype(const void* attr, int* type) {
    if (!attr || !type) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;
    
    *type = a->type;
    return 0;
}

int PthreadEmu::pthreadMutexattrSetpshared(void* attr, int pshared) {
    if (!attr) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;
    
    // Only PRIVATE is supported
    if (pshared != LINUX_PTHREAD_PROCESS_PRIVATE) {
        return LINUX_EINVAL;
    }
    
    a->pshared = pshared;
    return 0;
}

int PthreadEmu::pthreadMutexattrGetpshared(const void* attr, int* pshared) {
    if (!attr || !pshared) return LINUX_EINVAL;
    
    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;
    
    *pshared = a->pshared;
    return 0;
}

// ============================================================
// Mutex Functions
// ============================================================

int PthreadEmu::pthreadMutexInit(void* mutex, const void* attr) {
    if (!mutex) return LINUX_EINVAL;
    
    // Get or create mutex
    PthreadMutexInternal* m = PthreadMapper::GetOrCreateMutex(mutex);
    if (!m) return LINUX_ENOMEM;
    
    // Apply attributes if provided
    if (attr) {
        PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
        if (a) {
            m->type = a->type;
        }
    }
    
    return 0;
}

int PthreadEmu::pthreadMutexDestroy(void* mutex) {
    if (!mutex) return LINUX_EINVAL;
    
    PthreadMutexInternal* m = PthreadMapper::FindMutex(mutex);
    if (!m) {
        // Already destroyed or never initialized - treat as success
        return 0;
    }
    
    // Check if locked
    if (m->lock_count > 0) {
        return LINUX_EBUSY;
    }
    
    PthreadMapper::DestroyMutex(mutex);
    return 0;
}

int PthreadEmu::pthreadMutexLock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;
    
    PthreadMutexInternal* m = PthreadMapper::GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;
    
    DWORD current_thread = GetCurrentThreadId();
    
    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            /* Windows critical sections are recursive, while a POSIX
             * default mutex is not.  Do not let an accidental same-thread
             * relock turn a protected callback into a re-entrant operation. */
            while (m->owner_thread == current_thread && m->lock_count > 0)
                Sleep(1);
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
            
        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            // Allow recursive locking
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count++;
            break;
            
        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            // Check for deadlock
            if (m->owner_thread == current_thread && m->lock_count > 0) {
                return LINUX_EDEADLK;
            }
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
    }
    
    return 0;
}

int PthreadEmu::pthreadMutexTrylock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;
    
    PthreadMutexInternal* m = PthreadMapper::GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;
    
    DWORD current_thread = GetCurrentThreadId();
    
    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
            
        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count++;
            break;
            
        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            if (m->owner_thread == current_thread && m->lock_count > 0) {
                return LINUX_EBUSY;
            }
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
    }
    
    return 0;
}

int PthreadEmu::pthreadMutexTimedlock(void* mutex, const struct timespec* abstime) {
    if (!mutex) return LINUX_EINVAL;
    if (!abstime) return pthreadMutexLock(mutex);  // No timeout = regular lock
    
    PthreadMutexInternal* m = PthreadMapper::GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;
    
    DWORD timeout_ms = TimespecToMilliseconds(abstime);
    
    if (timeout_ms == 0) {
        // Already timed out
        return pthreadMutexTrylock(mutex);
    }
    
    // Spin with timeout
    DWORD start = GetTickCount();
    
    while (true) {
        int result = pthreadMutexTrylock(mutex);
        if (result == 0) {
            return 0;  // Got the lock
        }
        
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) {
            return LINUX_ETIMEDOUT;
        }
        
        // Sleep a bit before retry
        Sleep(1);
    }
}

int PthreadEmu::pthreadMutexUnlock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;
    
    PthreadMutexInternal* m = PthreadMapper::FindMutex(mutex);
    if (!m) {
        // Unknown mutex - possibly already destroyed
        return LINUX_EINVAL;
    }
    
    DWORD current_thread = GetCurrentThreadId();
    
    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            m->owner_thread = 0;
            m->lock_count = 0;
            LeaveCriticalSection(&m->cs);
            break;
            
        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            if (m->owner_thread != current_thread) {
                return LINUX_EPERM;
            }
            m->lock_count--;
            if (m->lock_count == 0) {
                m->owner_thread = 0;
            }
            LeaveCriticalSection(&m->cs);
            break;
            
        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            if (m->owner_thread != current_thread || m->lock_count == 0) {
                return LINUX_EPERM;
            }
            m->owner_thread = 0;
            m->lock_count = 0;
            LeaveCriticalSection(&m->cs);
            break;
    }
    
    return 0;
}
