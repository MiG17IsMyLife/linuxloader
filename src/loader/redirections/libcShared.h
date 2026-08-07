#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int sharedKswap_collect(void *p);
int sharedSystem(const char *command);
char *sharedStrncpy(char *dest, const char *src, size_t n);
int sharedSetenv(const char *name, const char *value, int overwrite);
char *sharedGetenv(const char *name);
int sharedUnsetenv(const char *name);
char *shared__strdup(const char *string);
struct tm *sharedLocaltime_R(const time_t *timep, struct tm *result);
struct tm *sharedGmtime_r(const time_t *timep, struct tm *result);
float sharedPowf(float base, float exponent);
int sharedIopl(int level);

void *sharedDlopen(const char *filename, int flags);
void *sharedDlsym(void *handle, const char *symbol);
int sharedDlclose(void *handle);
char *sharedDlerror();

int es1AudioDlopen(const char *filename, void **handle);
void *es1AudioDlsym(void *handle, const char *symbol);
int es1AudioDlclose(void *handle);

#ifdef __cplusplus
}
#endif
