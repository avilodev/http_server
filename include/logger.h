// logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include "types.h"
#include "utils.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

typedef enum {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
} log_level;

void log_init(const char* log_file);
void log_message(log_level level, const char* format, ...);
void log_close(void);

#endif /* LOGGER_H */