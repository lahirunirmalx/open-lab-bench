/**
 * Launcher window — pick a driver + view + port, click "Launch" to spawn an
 * independent psu_app process for that combination. Stays open so the user
 * can launch several instances side by side.
 */

#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

/*
 * self_exe_path: absolute path of the current binary, used to fork/exec
 * additional instances. The caller should resolve it (e.g. via /proc/self/exe).
 */
int launcher_run(const char *self_exe_path);

#endif
