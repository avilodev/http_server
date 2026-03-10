#include "logger.h"
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE*           log_fp    = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_init(const char* log_file) {
    pthread_mutex_lock(&log_mutex);
    log_fp = fopen(log_file, "a");
    pthread_mutex_unlock(&log_mutex);
}

void log_message(LogLevel level, const char* format, ...) {
    if (!log_fp) return;

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ",
             gmtime(&now));

    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&log_mutex);
    fprintf(log_fp, "[%s] [%s] ", timestamp, level_str[level]);
    vfprintf(log_fp, format, args);
    fprintf(log_fp, "\n");
    fflush(log_fp);
    pthread_mutex_unlock(&log_mutex);

    va_end(args);
}

void log_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_mutex_destroy(&log_mutex);
}
