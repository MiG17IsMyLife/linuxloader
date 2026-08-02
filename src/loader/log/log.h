#pragma once


#include <stdarg.h>
#include <stdio.h>


// Structure to hold the formatted message and its size
typedef struct {
    char *message;  // The formatted message
    size_t size;    // The size of the formatted message
    int endWithNewLine;   // 1 if it ends with a new line
    unsigned int repeat; // How many times the message was repeated
} LogFormattedMessage;

// List of status
enum { LOG_TRACE, // 0
       LOG_DEBUG, // 1
       LOG_GAME,  // 2
       LOG_INFO,  // 3
       LOG_WARN,  // 4
       LOG_ERROR, // 5
       LOG_FATAL  // 6
};

/*
 * Check the level before entering the variadic logger.  Besides avoiding a
 * function call on hot bridges, the conditional operator prevents evaluation
 * of formatting arguments for disabled trace/debug messages.  Keep the macros
 * expression-compatible because a few downstream users treat logging calls as
 * returning an int.
 */
#define log_game(...)  (logIsEnabled(LOG_GAME)  ? logGeneric(LOG_GAME,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_trace(...) (logIsEnabled(LOG_TRACE) ? logGeneric(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_debug(...) (logIsEnabled(LOG_DEBUG) ? logGeneric(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_info(...)  (logIsEnabled(LOG_INFO)  ? logGeneric(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_warn(...)  (logIsEnabled(LOG_WARN)  ? logGeneric(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_error(...) (logIsEnabled(LOG_ERROR) ? logGeneric(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_fatal(...) (logIsEnabled(LOG_FATAL) ? logGeneric(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__) : 0)

// Macro to ease logging
#define logVA_game(...)  logVA(LOG_GAME,  __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

int logGeneric(int level, const char *file, int line, const char *message, ...);
int logVA(int level, const char *file, int line, const char *message, va_list args);
int logIsEnabled(int level);
int logSanityChecks(int level, const char *message);
int logPrintHeader(FILE *stream, int level);
int logPrintMessage(FILE *stream, LogFormattedMessage formattedMessage, int level);
LogFormattedMessage logFormatMessage(const char *message, va_list args);
FILE* logGetStream();
void logInitTimer();
void logGetElapsedTime(long *seconds, long *milliseconds);
void logSetMinLevel(int level);

#ifdef __cplusplus
}
#endif
