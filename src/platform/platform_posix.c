/**
 * POSIX backend for the platform shim. Selected by the Makefile when
 * building on Linux/macOS/BSD.
 */

#include "platform.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

void pl_sleep_ms(unsigned ms) {
    if (ms == 0) return;
    usleep((useconds_t)ms * 1000u);
}

uint64_t pl_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + tv.tv_usec / 1000u;
}

bool pl_self_exe(char *out, size_t outlen) {
    if (!out || outlen == 0) return false;
    ssize_t n = readlink("/proc/self/exe", out, outlen - 1);
    if (n <= 0) return false;
    out[n] = '\0';
    return true;
}

/* SIGCHLD-ignore is installed lazily on the first spawn so launched
 * children auto-reap and never leave zombies. */
static void ensure_no_zombies(void) {
    static bool installed = false;
    if (installed) return;
    signal(SIGCHLD, SIG_IGN);
    installed = true;
}

bool pl_spawn(const char *exe, char *const argv[]) {
    if (!exe || !argv) return false;
    ensure_no_zombies();
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execv(exe, argv);
        _exit(127);
    }
    return true;
}
