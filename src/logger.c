#include "logger.h"
#include <stdarg.h>
#include <time.h>

static FILE* log_fp = NULL;

/**
 * Opens the server log file.
 * 
 * @param log_file The path to the log file.
 *
 * @warning Does not check for errors.
 */
void log_init(const char* log_file) {
    log_fp = fopen(log_file, "a");
}

/**
 * Writes to the log_file
 * 
 * Uses the format and type message to update the log file. 
 * Uses va_list arguments to format the file.
 * 
 * @param level Type of message being sent to the log file.
 * @param format Formats the output of the log file.
 */
void log_message(LogLevel level, const char* format, ...) {
    if (!log_fp) return;
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    fprintf(log_fp, "[%s] [%s] ", timestamp, level_str[level]);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_fp, format, args);
    va_end(args);
    
    fprintf(log_fp, "\n");
    fflush(log_fp);
}

/**
 * Closes the server log file.
 *
 * @warning Does not check for errors.
 */
void log_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}