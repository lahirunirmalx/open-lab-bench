/**
 * Small DMM helper functions used by every driver and view.
 */

#include "dmm_driver.h"

const char *dmm_mode_label(dmm_mode_t m) {
    switch (m) {
        case DMM_MODE_DC_VOLTS:    return "DCV";
        case DMM_MODE_AC_VOLTS:    return "ACV";
        case DMM_MODE_DC_AMPS:     return "DCI";
        case DMM_MODE_AC_AMPS:     return "ACI";
        case DMM_MODE_OHMS_2W:     return "OHM";
        case DMM_MODE_OHMS_4W:     return "4W-OHM";
        case DMM_MODE_CAPACITANCE: return "CAP";
        case DMM_MODE_FREQUENCY:   return "FREQ";
        case DMM_MODE_PERIOD:      return "PER";
        case DMM_MODE_DIODE:       return "DIODE";
        case DMM_MODE_CONTINUITY:  return "CONT";
        case DMM_MODE_TEMPERATURE: return "TEMP";
        default:                   return "???";
    }
}

const char *dmm_mode_unit(dmm_mode_t m) {
    switch (m) {
        case DMM_MODE_DC_VOLTS:
        case DMM_MODE_AC_VOLTS:
        case DMM_MODE_DIODE:       return "V";
        case DMM_MODE_DC_AMPS:
        case DMM_MODE_AC_AMPS:     return "A";
        case DMM_MODE_OHMS_2W:
        case DMM_MODE_OHMS_4W:
        case DMM_MODE_CONTINUITY:  return "Ω";
        case DMM_MODE_CAPACITANCE: return "F";
        case DMM_MODE_FREQUENCY:   return "Hz";
        case DMM_MODE_PERIOD:      return "s";
        case DMM_MODE_TEMPERATURE: return "°C";
        default:                   return "";
    }
}
