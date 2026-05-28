#include "registry.h"

#include "demo.h"
#include "korad/korad.h"
#include "modbus_bridge/modbus_bridge.h"
#include "scpi_psu/scpi_psu.h"

#include <string.h>

static const psu_driver_factory_t *const k_drivers[] = {
    &modbus_bridge_factory,

    /* SCPI — Siglent + Keysight/Agilent/HP. */
    &siglent_spd_factory,
    &keysight_e3631a_factory,
    &keysight_e3633a_factory,
    &keysight_e3634a_factory,
    &keysight_e3645a_factory,

    /* SCPI — Rigol DP-series. */
    &rigol_dp832_factory,
    &rigol_dp832a_factory,
    &rigol_dp811_factory,
    &rigol_dp711_factory,

    /* SCPI — Rohde & Schwarz HMP / NGE. */
    &rs_hmp4040_factory,
    &rs_hmp4030_factory,
    &rs_hmp2030_factory,
    &rs_nge103b_factory,

    /* SCPI — Keithley / Tektronix. */
    &keithley_2230g_factory,
    &keithley_2231a_factory,

    /* SCPI — classic HP / Agilent single-output (GPIB-friendly). */
    &hp_6632a_factory,
    &hp_6633a_factory,
    &hp_6634a_factory,

    /* Non-SCPI text protocols. */
    &korad_ka_factory,

    &demo_factory,
};

const psu_driver_factory_t *const *psu_drivers_list(size_t *count) {
    if (count) *count = sizeof(k_drivers) / sizeof(k_drivers[0]);
    return k_drivers;
}

const psu_driver_factory_t *psu_drivers_find(const char *id) {
    if (!id) return NULL;
    size_t n = sizeof(k_drivers) / sizeof(k_drivers[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(k_drivers[i]->id, id) == 0) return k_drivers[i];
    }
    return NULL;
}
