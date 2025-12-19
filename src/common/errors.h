#ifndef ERRORS_H
#define ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Distributed Computing Project common file: errors.h
 *
 * This file is intended to allow for the program to automatically detect
 * and report critical errors and crashes that the program may experience.
 * 
 * Usage of errors.h is as follows:
 * ```c
 *  #include "common/errors.h"
 *
 *  int main(void)
 *  {
 *      // other initialisation functions, including log_init(...)
 *
 *      // Install reporter; optional report URL and log path
 *      // Preferrably place this right after log_init to capture any crashes caused by log.h
 *      error_reporter_install("https://your.crash.endpoint/upload", "runtime.log");
 * 
 *      // rest of program...
 *
 *      return 0;
 *  }
 * 
 * ```
 * 
 * The errors.h file is intended to simply run in the background at all times, which is
 * why there's only an initialisation function and nothing else. The script will automatically
 * detect fatal errors, unhandled exceptions and crashes and report them to the provided crash
 * endpoint.
 * This file must be used in tandem with the log.h file
 * 
 */
int error_reporter_install(const char *report_url, const char *log_path);

/* Uninstall handlers (optional, for tests) */
void error_reporter_uninstall(void);

#ifdef __cplusplus
}
#endif

#endif /* ERRORS_H */