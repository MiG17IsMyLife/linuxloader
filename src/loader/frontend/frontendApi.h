#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional frontend contract.  CustomTP/TeknoParrotUI can set
 * PACLOADER_FRONTEND_STATUS to a JSON file path and observe loader state.
 * With the variable unset pacloader remains a normal standalone executable.
 */
void pacFrontendInitialize(void);
void pacFrontendReport(const char *stage, const char *detail);

#ifdef __cplusplus
}
#endif
