#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Returns non-zero when filename is the ES1 cabinet audio module. */
int es1AudioDlopen(const char *filename, void **handle);
void *es1AudioDlsym(void *handle, const char *symbol);
int es1AudioDlclose(void *handle);

#ifdef __cplusplus
}
#endif
