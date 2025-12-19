#ifndef LOG_H
#define LOG_H


/*
 * Distributed Computing Project common file: log.h
 * 
 * This file is intended to create an easy-to-use logging and debugging system to let
 * developers and users easily see any errors that occur and quickly report them.
 * 
 * Usage of log.h is as follows:
 * ```c
 *  #include "common/log.h"
 *
 *  int main(void)
 *  {
 *      // initialise the logging function (USE ONLY ONCE IN EACH MAIN SCRIPT)
 *      if (log_init("runtime.log", LOG_LEVEL_DEBUG) != 0) {
 *          return 1;
 *      }
 *
 *      log_info("runtime started");            // information (useful for stating any information that may be useful for the user)
 *      log_debug("worker count = %d", 8);      // debugging data (same as info but with built-in string formatting to show data)
 *      log_warn("slow network response");      // warning (only displayed if runtime is negatively affected but not strictly disasterous)
 *      log_error("failed to open socket");     // error (only displayed if runtime is critically affected from a bug/hardware issue and certain features are unavailable)
 *      log_fatal("unable to allocate memory"); // fatal error (only displayed if runtime is unable to continue running and must exit immediately)
 *
 *      log_shutdown(); // shut down ALL LOGS (This MUST be the last function called before program exit.)
 *      return 0;
 *  }
 * 
 * ```
 * 
 * Log messages must in some way include information about which module is calling said
 * log. For instance, `log_debug("Task %ld created (size=%zu, priority=%d)")` will relate
 * to the task handling source.
 * Log messages like `log_debug("Function started.")` is ambiguous and will not be tolerated
 * within the source. Any pull requests with this sort of log calling will be rejected and
 * you will be asked to replace the log with a more appropriate message.
 * 
 * Any other form of logging will not be permitted within this project. All
 * debug/info/warning/error/fatal messages must be put through the log.h file.
 * 
*/


#include <stdio.h>
#include <stdarg.h>

/* Log levels in increasing severity */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} log_level_t;

/* Initialise logging system
 *
 * path: path to log file, NULL for default "program.log"
 * level: minimum level to record
 *
 * Must be called once at startup.
 */
int log_init(const char *path, log_level_t level);

/* Flush and close log file */
void log_shutdown(void);

/* Core logging function (internal use) */
void log_write(log_level_t level,
               const char *file,
               int line,
               const char *fmt,
               ...);

/* User-facing macros */
#define log_debug(fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define log_info(fmt, ...) \
    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define log_warn(fmt, ...) \
    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define log_error(fmt, ...) \
    log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define log_fatal(fmt, ...) \
    log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* LOG_H */