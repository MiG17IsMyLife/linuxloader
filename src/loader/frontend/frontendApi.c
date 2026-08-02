#include "frontendApi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static char statusPath[MAX_PATH];
static char previousStage[32];
static char previousDetail[128];

static void jsonString(FILE *file, const char *value)
{
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p)
    {
        if (*p == '"' || *p == '\\')
            fputc('\\', file);
        if (*p >= 0x20)
            fputc(*p, file);
    }
    fputc('"', file);
}

void pacFrontendInitialize(void)
{
    DWORD length = GetEnvironmentVariableA("PACLOADER_FRONTEND_STATUS", statusPath,
                                           sizeof(statusPath));
    if (length == 0 || length >= sizeof(statusPath))
        statusPath[0] = '\0';
    pacFrontendReport("starting", "");
}

void pacFrontendReport(const char *stage, const char *detail)
{
    if (!statusPath[0])
        return;
    stage = stage ? stage : "unknown";
    detail = detail ? detail : "";
    if (strcmp(previousStage, stage) == 0 && strcmp(previousDetail, detail) == 0)
        return;

    char temporary[MAX_PATH + 5];
    snprintf(temporary, sizeof(temporary), "%s.tmp", statusPath);
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return;

    fprintf(file, "{\"protocol\":1,\"pid\":%lu,\"stage\":", GetCurrentProcessId());
    jsonString(file, stage);
    fputs(",\"detail\":", file);
    jsonString(file, detail);
    fputs("}\n", file);
    fclose(file);

    if (MoveFileExA(temporary, statusPath,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        snprintf(previousStage, sizeof(previousStage), "%s", stage);
        snprintf(previousDetail, sizeof(previousDetail), "%s", detail);
    }
}
