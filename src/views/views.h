/**
 * Views — UI layouts that render a psu_driver_t.
 *
 * Each view exposes a single run() function that owns its SDL window for the
 * duration of the call and returns when the user closes it. Views never
 * touch driver internals or transport details.
 */

#ifndef VIEWS_VIEWS_H
#define VIEWS_VIEWS_H

#include "psu_driver.h"
#include <stddef.h>

typedef struct {
    const char *id;            /* stable id, used on CLI: "toolbar-single" */
    const char *display_name;  /* shown in launcher */
    const char *description;   /* one-line tooltip */
    int         min_channels;  /* view requires >= this many driver channels */
    /*
     * Run the view. Owns the SDL window/renderer/fonts for the call. Returns
     * 0 on clean exit, non-zero on init failure. May be NULL for views not
     * yet ported.
     */
    int (*run)(psu_driver_t *drv);
} view_def_t;

const view_def_t *const *views_list(size_t *count);
const view_def_t *views_find(const char *id);

/* Individual view entry points (declared here so the registry can wire them). */
int view_toolbar_single_run(psu_driver_t *drv);
int view_toolbar_dual_run  (psu_driver_t *drv);

#endif
