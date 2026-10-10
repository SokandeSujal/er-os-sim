/* Assignment 4: common process utilities and atomic pipe events.
 * Each event is formatted into a fixed buffer and emitted by one write call.
 * Since the event is smaller than PIPE_BUF, concurrent writers cannot interleave.
 * EINTR is retried so signals do not silently discard log events. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

Shared *g_shm;
int g_log_fd = -1;

const char *state_name(int state) {
    static const char *names[] = {"EMPTY", "ARRIVED", "WAIT_VENT", "WAIT_RES", "READY", "RUNNING", "DONE"};
    return state >= EMPTY && state <= DONE ? names[state] : "?";
}

void event_log(const char *type, int patient_id, const char *fmt, ...) {
    char line[LOG_LINE];
    int tick = g_shm ? g_shm->tick : 0;
    int used = snprintf(line, sizeof line, "T=%d PID=%ld EVT=%s PAT=", tick,
                        (long)getpid(), type);
    if (used < 0 || (size_t)used >= sizeof line) return;
    used += snprintf(line + used, sizeof line - (size_t)used,
                     patient_id >= 0 ? "%d " : "- ", patient_id);
    if (used < 0 || (size_t)used >= sizeof line) return;
    if (fmt && *fmt) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(line + used, sizeof line - (size_t)used - 2U, fmt, ap);
        va_end(ap);
        if (n > 0) used += n < (int)(sizeof line - (size_t)used - 2U) ? n : (int)(sizeof line - (size_t)used - 2U);
    }
    if (used > 0 && line[used - 1] != ' ') line[used++] = ' ';
    line[used++] = '\n';
    if (g_log_fd >= 0) {
        ssize_t rc;
        do rc = write(g_log_fd, line, (size_t)used); while (rc < 0 && errno == EINTR);
    }
}

int sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) < 0) if (errno != EINTR) return -1;
    return 0;
}
