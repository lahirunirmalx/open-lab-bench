/**
 * Thin platform shim — abstracts the handful of POSIX-only APIs the rest
 * of the codebase relies on (time, process spawn, self-exe lookup) so the
 * app builds on Linux/macOS AND on Windows (MinGW-w64).
 *
 * Threads stay on <pthread.h> because MinGW-w64 ships winpthread; the
 * concurrency code doesn't need to know which OS it's on.
 *
 * Selection of backend is done at compile time by the Makefile:
 *   POSIX → platform_posix.c
 *   Win32 → platform_win32.c
 */

#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- time --------------------------------------------------------------- */

/** Block for `ms` milliseconds. */
void pl_sleep_ms(unsigned ms);

/** Wall-clock milliseconds, suitable for short interval / timestamp use. */
uint64_t pl_now_ms(void);

/* ---- process / self ----------------------------------------------------- */

/**
 * Resolve the current executable's absolute path. Useful when the
 * launcher needs to fork/exec a fresh copy of itself.
 * Returns true on success; on failure leaves `out` untouched.
 */
bool pl_self_exe(char *out, size_t outlen);

/**
 * Spawn a detached process: `exe` with `argv` (NULL-terminated).
 * `argv[0]` should typically equal `exe`. Caller does not wait().
 *
 * - POSIX: fork() + execv(). SIGCHLD is set to SIG_IGN on first call so
 *   children auto-reap without leaving zombies.
 * - Win32: CreateProcessW with CREATE_NEW_CONSOLE. argv values are
 *   quoted if they contain spaces; embedded quotes are NOT escaped (the
 *   launcher only emits simple values, so this is fine).
 *
 * Returns true if the child was successfully launched.
 */
bool pl_spawn(const char *exe, char *const argv[]);

#endif
