#include <sys/types.h>

int initPatch();

// int patchedPrintf(char *format,...);
// int patchedPuts(char *s);

void callLoadCard();
void callEjectCard();

#ifdef __cplusplus
extern "C"
{
#endif
    void patchSO(uintptr_t base, const char *soPath);
#ifdef __cplusplus
}
#endif