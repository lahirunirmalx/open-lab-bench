#include "views.h"

#include <string.h>

static const view_def_t k_views[] = {
    {
        .id           = "toolbar-single",
        .display_name = "Toolbar — single channel",
        .description  = "Compact strip: V/A readout, ON/OFF, SET popup. One channel.",
        .min_channels = 1,
        .run          = view_toolbar_single_run,
    },
    {
        .id           = "toolbar-dual",
        .display_name = "Toolbar — dual channel",
        .description  = "Compact strip: CH1+CH2 side by side, large V/A, SET popup per channel.",
        .min_channels = 2,
        .run          = view_toolbar_dual_run,
    },
    {
        .id           = "full-single",
        .display_name = "Full GUI — single channel",
        .description  = "VFD readouts, bar meters, scope, keypad. Not yet ported.",
        .min_channels = 1,
        .run          = NULL,
    },
    {
        .id           = "full-dual",
        .display_name = "Full GUI — dual channel",
        .description  = "Dual VFD/bars/scope + tracking. Not yet ported.",
        .min_channels = 2,
        .run          = NULL,
    },
};

static const view_def_t *k_view_ptrs[] = {
    &k_views[0], &k_views[1], &k_views[2], &k_views[3],
};

const view_def_t *const *views_list(size_t *count) {
    if (count) *count = sizeof(k_view_ptrs) / sizeof(k_view_ptrs[0]);
    return k_view_ptrs;
}

const view_def_t *views_find(const char *id) {
    if (!id) return NULL;
    size_t n = sizeof(k_view_ptrs) / sizeof(k_view_ptrs[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(k_view_ptrs[i]->id, id) == 0) return k_view_ptrs[i];
    }
    return NULL;
}
