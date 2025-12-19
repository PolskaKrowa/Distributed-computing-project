#define _GNU_SOURCE
#include "errors.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#  include <execinfo.h>   /* backtrace */
#endif

/* Maximum bytes of log tail to capture */
#define LOG_TAIL_BYTES (16 * 1024)

/* Maximum frames captured in stack trace */
#define MAX_BACKTRACE_FRAMES 64

static _Atomic int reporter_installed = 0;
static _Atomic int handling_event = 0; /* prevent re-entrancy */
static const char *g_report_url = NULL;
static char g_log_path[4096] = "program.log";

/* Helper: current timestamp string (ISO) */
static void now_string(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%S%z", &tm);
}

/* Helper: write tail of log_path to FILE* out */
static void write_log_tail(FILE *out, const char *log_path)
{
    if (!log_path) return;
    int fd = open(log_path, O_RDONLY);
    if (fd < 0) {
        fprintf(out, "Could not open log file '%s': %s\n", log_path, strerror(errno));
        return;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size == (off_t)-1) {
        fprintf(out, "Could not seek in log file '%s'\n", log_path);
        close(fd);
        return;
    }

    off_t start = (size > LOG_TAIL_BYTES) ? (size - LOG_TAIL_BYTES) : 0;
    if (lseek(fd, start, SEEK_SET) == (off_t)-1) {
        fprintf(out, "Could not seek in log file '%s'\n", log_path);
        close(fd);
        return;
    }

    char buf[4096];
    ssize_t r;
    fprintf(out, "\n----- Log tail (last %d bytes) -----\n", LOG_TAIL_BYTES);
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, r, out);
    }
    fprintf(out, "\n----- End log tail -----\n");
    close(fd);
}

/* Create a report file and return path (caller must free) */
static char *create_report_file(const char *header_info, int signum, void *fault_addr)
{
    char ts[64];
    now_string(ts, sizeof(ts));
    pid_t pid = getpid();

    char *path = NULL;
    int rc = asprintf(&path, "/tmp/crash-report-%d-%s.log", (int)pid, ts);
    if (rc < 0 || !path) {
        return NULL;
    }

    /* Replace characters that may be problematic in filename (':' -> '-') */
    for (char *p = path; *p; ++p) if (*p == ':' || *p == '+') *p = '-';

    FILE *f = fopen(path, "w");
    if (!f) {
        free(path);
        return NULL;
    }

    fprintf(f, "Crash report generated: %s\n", ts);
    fprintf(f, "PID: %d\n", (int)pid);
    fprintf(f, "Header: %s\n", header_info ? header_info : "(none)");
    if (signum) fprintf(f, "Signal: %d (%s)\n", signum, strsignal(signum));
    if (fault_addr) fprintf(f, "Fault address: %p\n", fault_addr);

    /* system info */
    struct utsname uts;
    if (uname(&uts) == 0) {
        fprintf(f, "System: %s %s %s %s\n", uts.sysname, uts.release, uts.version, uts.machine);
    }

    /* stack trace if available */
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    void *bt[MAX_BACKTRACE_FRAMES];
    int bt_size = backtrace(bt, MAX_BACKTRACE_FRAMES);
    char **bt_syms = backtrace_symbols(bt, bt_size);
    fprintf(f, "\nStack trace (frames: %d)\n", bt_size);
    if (bt_syms) {
        for (int i = 0; i < bt_size; ++i) {
            fprintf(f, "%d: %s\n", i, bt_syms[i]);
        }
        free(bt_syms);
    } else {
        fprintf(f, "No backtrace symbols available\n");
    }
#else
    fprintf(f, "Backtrace not supported on this platform\n");
#endif

    /* Tail of the runtime log */
    write_log_tail(f, g_log_path);

    fclose(f);
    return path;
}

/* Try to POST the file to g_report_url using curl if available
 * returns 0 on success, negative on failure */
static int try_post_report_with_curl(const char *report_path)
{
    if (!g_report_url || !report_path) return -1;
    /* Check curl exists */
    if (access("/usr/bin/curl", X_OK) != 0 && access("/bin/curl", X_OK) != 0) {
        return -2;
    }

    /* Build system command. Use -s to be silent. */
    char *cmd = NULL;
    int rc = asprintf(&cmd,
                      "curl -s -S -X POST -F \"report=@%s\" \"%s\" >/dev/null 2>&1",
                      report_path, g_report_url ? g_report_url : "");
    if (rc < 0 || !cmd) {
        free(cmd);
        return -3;
    }
    int sysrc = system(cmd);
    free(cmd);
    if (sysrc == -1) return -4;
    /* return the exit code of curl */
    return WEXITSTATUS(sysrc);
}

/* Prompt the user on stdin. Returns 1 for yes, 0 for no.
 * If not interactive, returns 0. */
static int prompt_user_yesno(const char *prompt)
{
    if (!isatty(STDIN_FILENO)) return 0;

    fprintf(stderr, "%s [Y/n]: ", prompt);
    fflush(stderr);

    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    /* Accept empty (enter) as yes */
    if (buf[0] == '\n' || buf[0] == '\0') return 1;
    if (buf[0] == 'y' || buf[0] == 'Y') return 1;
    return 0;
}

/* Central handler invoked from signals and terminate path.
 * This is not async-signal-safe in all branches. We minimise work in the
 * actual signal handler, but accept the practical trade-off here for more
 * useful reports. */
static void handle_crash_event(const char *header, int signum, void *fault_addr)
{
    /* Prevent re-entrancy */
    int expected = 0;
    if (!atomic_compare_exchange_strong(&handling_event, &expected, 1)) {
        /* already handling something */
        return;
    }

    /* Try to log via common logger if available */
    log_error("Unhandled event: %s (signal=%d addr=%p). Creating crash report.",
              header ? header : "exception", signum, fault_addr);

    char *report_path = create_report_file(header, signum, fault_addr);
    if (!report_path) {
        log_error("Failed to create crash report file");
        atomic_store(&handling_event, 0);
        return;
    }

    fprintf(stderr, "\nA fatal error occurred. Crash report created at:\n  %s\n", report_path);

    /* If interactive, ask user whether to upload */
    if (prompt_user_yesno("Would you like to submit this crash report to the developers?")) {
        int post_rc = try_post_report_with_curl(report_path);
        if (post_rc == 0) {
            log_info("Report successfully posted to %s", g_report_url ? g_report_url : "(none)");
            fprintf(stderr, "Report submitted successfully.\n");
        } else {
            log_warn("Report upload failed (curl exit=%d). Ask user to send %s manually.", post_rc, report_path);
            fprintf(stderr, "Automatic upload failed. Please email the file to the developers.\n");
        }
    } else {
        fprintf(stderr, "Crash report saved to: %s\n", report_path);
        log_info("User declined to upload crash report; saved at %s", report_path);
    }

    /* Best effort: flush logger */
    log_shutdown();

    /* leave report on disk for post-mortem */
    free(report_path);

    atomic_store(&handling_event, 0);
}

/* POSIX signal handler. We keep it small; we call handle_crash_event which may
 * use non async-signal-safe APIs; this is acceptable as a practical measure
 * in many environments. If you want strict async-signal-safety, implement a
 * separate crash reporter process and only write minimal info here. */
static void crash_signal_handler(int sig, siginfo_t *si, void *ucontext)
{
    void *fault_addr = si ? si->si_addr : NULL;
    /* Minimally notify on stderr using async-safe write */
    const char *msg = "Fatal signal received. Generating crash report...\n";
    write(STDERR_FILENO, msg, strlen(msg));
    handle_crash_event("signal", sig, fault_addr);

    /* Reset default handler and re-raise to let kernel produce core or default behaviour */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Install signal handlers */
static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS,  &sa, NULL);
#endif
}

/* Uninstall signal handlers by restoring defaults */
static void uninstall_signal_handlers(void)
{
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE,  SIG_DFL);
    signal(SIGILL,  SIG_DFL);
#ifdef SIGBUS
    signal(SIGBUS,  SIG_DFL);
#endif
}

#ifdef __cplusplus
#include <exception>
#include <iostream>

/* C++ terminate handler */
static void cpp_terminate_handler()
{
    std::exception_ptr eptr = std::current_exception();
    const char *what = "unknown";
    char header[512];
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            what = e.what();
        } catch (...) {
            what = "non-std exception";
        }
    }

    snprintf(header, sizeof(header), "C++ terminate: %s", what);
    /* print minimal message to stderr */
    std::cerr << "Unhandled C++ exception: " << what << ". Generating crash report...\n";
    handle_crash_event(header, 0, NULL);

    /* abort to produce core if configured */
    abort();
}

#endif /* __cplusplus */

int error_reporter_install(const char *report_url, const char *log_path)
{
    if (atomic_load(&reporter_installed)) return 0;

    if (report_url) g_report_url = strdup(report_url);
    if (log_path) {
        strncpy(g_log_path, log_path, sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';
    } else {
        /* check environment variable LOG_PATH first */
        const char *env = getenv("ERROR_REPORTER_LOG_PATH");
        if (env && env[0]) strncpy(g_log_path, env, sizeof(g_log_path) - 1);
    }

    install_signal_handlers();

#ifdef __cplusplus
    std::set_terminate(cpp_terminate_handler);
#endif

    atomic_store(&reporter_installed, 1);
    log_info("Error reporter installed (log_path=%s report_url=%s)", g_log_path, g_report_url ? g_report_url : "(none)");
    return 0;
}

void error_reporter_uninstall(void)
{
    if (!atomic_load(&reporter_installed)) return;
    uninstall_signal_handlers();
#ifdef __cplusplus
    /* no portable way to restore previous terminate handler here */
#endif
    atomic_store(&reporter_installed, 0);
    log_info("Error reporter uninstalled");
}