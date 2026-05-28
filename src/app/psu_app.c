/**
 * psu_app — single binary that runs one view against one driver.
 *
 * No arguments: open the GUI launcher (pick driver/view/port, click LAUNCH
 * which fork/execs psu_app again with the chosen flags — so each window is
 * its own process and several can run side by side).
 *
 * With --driver and --view: run that combination directly (this is what the
 * launcher invokes in its children).
 *
 * Usage:
 *   psu_app                                          # launcher GUI
 *   psu_app --list                                   # CLI: list drivers + views
 *   psu_app --driver=<id> --view=<id> [--port=<dev>] [--baud=<n>]
 */

#include "drivers/registry.h"
#include "launcher.h"
#include "psu_driver.h"
#include "views/views.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *out, const char *prog) {
    fprintf(out,
        "Usage:\n"
        "  %s                                  # open GUI launcher\n"
        "  %s --list                           # list drivers + views\n"
        "  %s --driver=<id> --view=<id> [--port=<dev>] [--baud=<n>]\n"
        "\n"
        "Run \"%s --list\" to see available drivers and views.\n",
        prog, prog, prog, prog);
}

static void list_all(void) {
    size_t n;
    const psu_driver_factory_t *const *drvs = psu_drivers_list(&n);
    printf("Drivers (%zu):\n", n);
    for (size_t i = 0; i < n; i++) {
        printf("  --driver=%-16s %s\n      %s\n",
               drvs[i]->id, drvs[i]->display_name, drvs[i]->description);
    }

    const view_def_t *const *vws = views_list(&n);
    printf("\nViews (%zu):\n", n);
    for (size_t i = 0; i < n; i++) {
        const char *status = vws[i]->run ? "" : "  [not yet ported]";
        printf("  --view=%-18s %s (needs %d ch)%s\n      %s\n",
               vws[i]->id, vws[i]->display_name,
               vws[i]->min_channels, status, vws[i]->description);
    }
}

/* If arg starts with "<prefix>", return the suffix; else NULL. */
static const char *opt_value(const char *arg, const char *prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(arg, prefix, plen) == 0) return arg + plen;
    return NULL;
}

/* Resolve our own executable path so the launcher can fork/exec us.
 * On Linux /proc/self/exe is canonical and survives PATH/cwd changes; fall
 * back to argv0 otherwise. */
static void resolve_self_exe(const char *argv0, char *out, size_t out_sz) {
    ssize_t n = readlink("/proc/self/exe", out, out_sz - 1);
    if (n > 0) { out[n] = '\0'; return; }
    snprintf(out, out_sz, "%s", argv0 ? argv0 : "psu_app");
}

int main(int argc, char **argv) {
    const char *driver_id = NULL;
    const char *view_id   = NULL;
    const char *port      = NULL;
    int         baud      = 0;

    for (int i = 1; i < argc; i++) {
        const char *v;
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            list_all();
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }
        if ((v = opt_value(argv[i], "--driver="))) { driver_id = v; continue; }
        if ((v = opt_value(argv[i], "--view=")))   { view_id   = v; continue; }
        if ((v = opt_value(argv[i], "--port=")))   { port      = v; continue; }
        if ((v = opt_value(argv[i], "--baud=")))   { baud      = atoi(v); continue; }

        fprintf(stderr, "unknown argument: %s\n\n", argv[i]);
        usage(stderr, argv[0]);
        return 2;
    }

    /* No driver/view picked from the CLI → show launcher. */
    if (!driver_id && !view_id) {
        char self_exe[PATH_MAX];
        resolve_self_exe(argv[0], self_exe, sizeof(self_exe));
        return launcher_run(self_exe);
    }

    if (!driver_id || !view_id) {
        usage(stderr, argv[0]);
        return 2;
    }

    const psu_driver_factory_t *fac = psu_drivers_find(driver_id);
    if (!fac) {
        fprintf(stderr, "unknown driver: %s\n", driver_id);
        return 2;
    }
    const view_def_t *view = views_find(view_id);
    if (!view) {
        fprintf(stderr, "unknown view: %s\n", view_id);
        return 2;
    }
    if (!view->run) {
        fprintf(stderr, "view '%s' is not yet ported\n", view_id);
        return 2;
    }

    if (baud <= 0) baud = fac->default_baud;

    psu_driver_t *drv = fac->open(port, baud);
    if (!drv) {
        fprintf(stderr, "failed to open driver '%s' on %s @ %d baud\n",
                driver_id, port ? port : "(none)", baud);
        return 1;
    }

    if (drv->caps.n_channels < view->min_channels) {
        fprintf(stderr,
                "driver '%s' has %d channel(s) but view '%s' needs %d\n",
                driver_id, drv->caps.n_channels, view_id, view->min_channels);
        drv->close(drv);
        return 2;
    }

    int rc = view->run(drv);
    drv->close(drv);
    return rc;
}
