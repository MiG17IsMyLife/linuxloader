// ============================================================
// pthread_semaphore.cpp - Semaphore Emulation
// ============================================================

#include "pthreadEmu.hpp"
#include "pthreadInternal.hpp"
#include <string>

// ============================================================
// Semaphore Internal Structure
// ============================================================

typedef struct SemaphoreInternal {
    HANDLE              handle;
    volatile LONG       value;
    volatile int        initialized;
    std::string         name;
    volatile LONG       refcount;
} SemaphoreInternal;


#define SEM_INIT_MAGIC  0x53454D41  // "SEMA"
#define SEM_VALUE_MAX   0x7FFFFFFF

// ============================================================
// Semaphore Mapping (similar to mutex mapping)
// ============================================================

#include <unordered_map>

static std::unordered_map<void*, SemaphoreInternal*> s_sem_map;
static std::unordered_map<std::string, SemaphoreInternal*> s_named_sems;
static SRWLOCK s_sem_lock = SRWLOCK_INIT;


static SemaphoreInternal* GetOrCreateSem(void* linux_sem, unsigned int initial_value = 0) {
    AcquireSRWLockExclusive(&s_sem_lock);
    
    auto it = s_sem_map.find(linux_sem);
    if (it != s_sem_map.end() && it->second->initialized == SEM_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_sem_lock);
        return it->second;
    }
    
    SemaphoreInternal* sem = new SemaphoreInternal;
    sem->handle = CreateSemaphoreA(NULL, initial_value, SEM_VALUE_MAX, NULL);
    sem->value = initial_value;
    sem->initialized = SEM_INIT_MAGIC;
    sem->refcount = 1;

    
    if (!sem->handle) {
        delete sem;
        ReleaseSRWLockExclusive(&s_sem_lock);
        return nullptr;
    }
    
    s_sem_map[linux_sem] = sem;
    
    ReleaseSRWLockExclusive(&s_sem_lock);
    return sem;
}

static SemaphoreInternal* FindSem(void* linux_sem) {
    AcquireSRWLockShared(&s_sem_lock);
    
    auto it = s_sem_map.find(linux_sem);
    SemaphoreInternal* result = nullptr;
    if (it != s_sem_map.end() && it->second->initialized == SEM_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_sem_lock);
    return result;
}

static void DestroySem(void* linux_sem) {
    AcquireSRWLockExclusive(&s_sem_lock);
    
    auto it = s_sem_map.find(linux_sem);
    if (it != s_sem_map.end()) {
        if (it->second) {
            if (it->second->handle) {
                CloseHandle(it->second->handle);
            }
            it->second->initialized = 0;
            delete it->second;
        }
        s_sem_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_sem_lock);
}

// ============================================================
// Semaphore API - C Functions
// ============================================================

extern "C" {

int emuSemInit(void* sem, int pshared, unsigned int value) {
    if (!sem) return -1;
    
    // pshared != 0 is not supported on Windows
    if (pshared != 0) {
        return -1;  // ENOSYS equivalent
    }
    
    SemaphoreInternal* s = GetOrCreateSem(sem, value);
    if (!s) return -1;
    
    return 0;
}

int emuSemDestroy(void* sem) {
    if (!sem) return -1;
    
    DestroySem(sem);
    return 0;
}

int emuSemWait(void* sem) {
    if (!sem) return -1;
    
    SemaphoreInternal* s = FindSem(sem);
    if (!s) {
        // Auto-initialize with value 0
        s = GetOrCreateSem(sem, 0);
        if (!s) return -1;
    }
    
    DWORD result = WaitForSingleObject(s->handle, INFINITE);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }
    
    return -1;
}

int emuSemTrywait(void* sem) {
    if (!sem) return -1;
    
    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;
    
    DWORD result = WaitForSingleObject(s->handle, 0);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }
    
    // EAGAIN - would block
    return -1;
}

int emuSemTimedwait(void* sem, const struct timespec* abs_timeout) {
    if (!sem) return -1;
    
    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;
    
    DWORD timeout_ms = INFINITE;
    if (abs_timeout) {
        timeout_ms = TimespecToMilliseconds(abs_timeout);
    }
    
    DWORD result = WaitForSingleObject(s->handle, timeout_ms);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }
    
    if (result == WAIT_TIMEOUT) {
        // ETIMEDOUT
        return -1;
    }
    
    return -1;
}

int emuSemPost(void* sem) {
    if (!sem) return -1;
    
    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;
    
    InterlockedIncrement(&s->value);
    
    if (!ReleaseSemaphore(s->handle, 1, NULL)) {
        InterlockedDecrement(&s->value);
        return -1;
    }
    
    return 0;
}

int emuSemGetValue(void* sem, int* sval) {
    if (!sem || !sval) return -1;
    
    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;
    
    *sval = (int)s->value;
    return 0;
}

void* emuSemOpen(const char* name, int oflag, int mode, unsigned int value) {
    if (!name) return (void*)-1;
    
    AcquireSRWLockExclusive(&s_sem_lock);
    
    std::string sem_name = name;
    auto it = s_named_sems.find(sem_name);
    
    if (it != s_named_sems.end()) {
        // Linux O_CREAT=0x40, O_EXCL=0x80 (octal 0100, 0200)
        if ((oflag & 0x40) && (oflag & 0x80)) {
            ReleaseSRWLockExclusive(&s_sem_lock);
            // EEXIST
            return (void*)-1;
        }
        it->second->refcount++;
        ReleaseSRWLockExclusive(&s_sem_lock);
        return (void*)it->second;
    }
    
    if (!(oflag & 0x40)) { // !O_CREAT
        ReleaseSRWLockExclusive(&s_sem_lock);
        // ENOENT
        return (void*)-1;
    }
    
    // Windows name munging: semaphores are Global
    std::string win_name = "Global\\";
    for(char c : sem_name) {
        if (c == '/') continue;
        if (isalnum(c) || c == '_') win_name += c;
    }
    
    SemaphoreInternal* sem = new SemaphoreInternal;
    sem->handle = CreateSemaphoreA(NULL, value, SEM_VALUE_MAX, win_name.c_str());
    sem->value = value;
    sem->initialized = SEM_INIT_MAGIC;
    sem->name = sem_name;
    sem->refcount = 1;
    
    if (!sem->handle) {
        delete sem;
        ReleaseSRWLockExclusive(&s_sem_lock);
        return (void*)-1;
    }
    
    s_named_sems[sem_name] = sem;
    s_sem_map[sem] = sem; // Map to itself so emuSemWait works
    
    ReleaseSRWLockExclusive(&s_sem_lock);
    return (void*)sem;
}

int emuSemClose(void* sem) {
    if (!sem || sem == (void*)-1) return -1;
    
    AcquireSRWLockExclusive(&s_sem_lock);
    
    SemaphoreInternal* s = (SemaphoreInternal*)sem;
    if (s && s->initialized == SEM_INIT_MAGIC) {
        s->refcount--;
        if (s->refcount <= 0 && s->name.empty()) {
            // Fully destroy if unlinked and no more references
            if (s->handle) CloseHandle(s->handle);
            s_sem_map.erase(s);
            delete s;
        }
    }
    
    ReleaseSRWLockExclusive(&s_sem_lock);
    return 0;
}

int emuSemUnlink(const char* name) {
    if (!name) return -1;
    
    AcquireSRWLockExclusive(&s_sem_lock);
    
    std::string sem_name = name;
    auto it = s_named_sems.find(sem_name);
    if (it != s_named_sems.end()) {
        SemaphoreInternal* s = it->second;
        s->name.clear(); // Mark as unlinked
        s_named_sems.erase(it);
        
        if (s->refcount <= 0) {
            if (s->handle) CloseHandle(s->handle);
            s_sem_map.erase(s);
            delete s;
        }
        
        ReleaseSRWLockExclusive(&s_sem_lock);
        return 0;
    }
    
    ReleaseSRWLockExclusive(&s_sem_lock);
    return -1;
}


} // extern "C"

// ============================================================
// Cleanup function for semaphores
// ============================================================

void SemaphoreCleanup() {
    AcquireSRWLockExclusive(&s_sem_lock);
    
    for (auto& pair : s_sem_map) {
        if (pair.second) {
            if (pair.second->handle) {
                CloseHandle(pair.second->handle);
            }
            delete pair.second;
        }
    }
    s_sem_map.clear();
    
    ReleaseSRWLockExclusive(&s_sem_lock);
}
