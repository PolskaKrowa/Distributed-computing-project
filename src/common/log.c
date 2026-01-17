#include "log.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>

static FILE *log_file = NULL;
static log_level_t log_level = LOG_LEVEL_INFO;

static const char *level_to_string(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default:              return "UNKNOWN";
    }
}

int log_init(const char *path, log_level_t level)
{
    if (log_file) {
        return 0;
    }

    const char *log_path = path ? path : "program.log";
    log_file = fopen(log_path, "a");
    if (!log_file) {
        return -1;
    }

    log_level = level;
    setvbuf(log_file, NULL, _IOLBF, 0);

    return 0;
}

void log_shutdown(void)
{
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_write(log_level_t level,
               const char *file,
               int line,
               const char *fmt,
               ...)
{
    if (!log_file || level < log_level) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf),
             "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(log_file,
            "[%s] %-5s %s:%d | ",
            time_buf,
            level_to_string(level),
            file,
            line);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fputc('\n', log_file);

    if (level == LOG_LEVEL_FATAL) {
        fflush(log_file);
        abort();
    }
}
