/**
 * Driver registry — single point that knows every compiled-in PSU driver
 * factory. Adding a new model is a one-line change to registry.c.
 */

#ifndef DRIVERS_REGISTRY_H
#define DRIVERS_REGISTRY_H

#include "psu_driver.h"
#include <stddef.h>

/**
 * Returns the array of known factories. *count is set to the array length.
 * Pointers in the array are valid for the lifetime of the program.
 */
const psu_driver_factory_t *const *psu_drivers_list(size_t *count);

/**
 * Look up a factory by its id (e.g. "modbus-bridge"). Returns NULL if no
 * driver with that id is compiled in.
 */
const psu_driver_factory_t *psu_drivers_find(const char *id);

#endif
