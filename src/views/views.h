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
#include "dmm_driver.h"
#include <stddef.h>

/* ---- PSU views ---- */

typedef struct {
    const char *id;            /* stable id, used on CLI: "toolbar-single" */
    const char *display_name;  /* shown in launcher */
    const char *description;   /* one-line tooltip */
    int         min_channels;  /* view requires >= this many driver channels */
    int (*run)(psu_driver_t *drv);
} view_def_t;

const view_def_t *const *views_list(size_t *count);
const view_def_t *views_find(const char *id);

int view_toolbar_single_run(psu_driver_t *drv);
int view_toolbar_dual_run  (psu_driver_t *drv);
int view_full_single_run   (psu_driver_t *drv);
int view_full_dual_run     (psu_driver_t *drv);

/* ---- DMM views ---- */

typedef struct {
    const char *id;            /* stable id, used on CLI: "dmm-toolbar" / "dmm-full" */
    const char *display_name;
    const char *description;
    int (*run)(dmm_driver_t *drv);
} dmm_view_def_t;

const dmm_view_def_t *const *dmm_views_list(size_t *count);
const dmm_view_def_t *dmm_views_find(const char *id);

int view_dmm_toolbar_run(dmm_driver_t *drv);
int view_dmm_full_run   (dmm_driver_t *drv);

#endif
