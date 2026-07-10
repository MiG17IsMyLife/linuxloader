#ifdef __linux__
#include <dlfcn.h>

#include "../config/config.h"

#ifdef __linux__
#define REAL_FUNC(name) dlsym(RTLD_NEXT, #name)
#endif

extern uint32_t gId;
extern int gGrp;

static void *(*real_malloc)(size_t) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;
static void (*real_free)(void *) = NULL;
static int (*real_posix_memalign)(void **, size_t, size_t) = NULL;


static void init_mem_hooks(void)
{
    if (!real_malloc)
    {
        real_malloc = REAL_FUNC(malloc);
        real_calloc = REAL_FUNC(calloc);
        real_realloc = REAL_FUNC(realloc);
        real_free = REAL_FUNC(free);
    }
    if (!real_posix_memalign)
    {
        real_posix_memalign = REAL_FUNC(posix_memalign);
    }
}


void *malloc(size_t size)
{
    init_mem_hooks();
    void *p = real_malloc(size);
    if (p)
    {
        memset(p, 0, size); 
    }
    return p;
}


void *realloc(void *ptr, size_t size)
{
    init_mem_hooks();
    return real_realloc(ptr, size);
}


void *calloc(size_t nmemb, size_t size)
{
    init_mem_hooks();
    return real_calloc(nmemb, size);
}
void free(void *ptr)
{
    init_mem_hooks();
    real_free(ptr);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    init_mem_hooks();
    int res = real_posix_memalign(memptr, alignment, size);
    if (res == 0 && *memptr)
    {
        memset(*memptr, 0, size);
    }
    return res;
}

void *emcpy(void *dest, const void *src, size_t n)
{
    if (gGrp == GROUP_ABC && dest == src)
    {
        return memmove(dest, src, n);
    }
    void *(*_memcpy)(void *dest, const void *src, size_t n) = REAL_FUNC(memcpy);
    return _memcpy(dest, src, n);
}
#endif