#ifndef MAIN_SHARED_H
#define MAIN_SHARED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PARSE_ARGS_SUCCESS 0
#define PARSE_ARGS_FAILURE -1
#define PARSE_ARGS_HELP 1

#define MAX_PATH_LENGTH 1024

#ifdef __linux__
#    define PATH_SEPARATOR '/'
#else
#    define PATH_SEPARATOR '\\'
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    extern char *controlsPath;
    extern char *configPath;
    int parseArgs(int argc, char *argv[], char *command, char *originalDir, char *gameELF, char *libraryPath);
    uint32_t getCrc32Mem(const uint8_t *data, size_t size);
    bool fileExists(const char *filename);
    char *myBasename(char *path);
#ifdef __cplusplus
}
#endif

#endif /* MAIN_SHARED_H */