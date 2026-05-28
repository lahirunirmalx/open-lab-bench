# Architecture

The app is split so each PSU model is a *driver* and each layout is a *view*.
Any view can run against any driver, and a single binary can launch any
combination — including several instances in parallel (the launcher
fork/execs `psu_app` per window).

## Layout

```text
include/
  psu_driver.h                    Public driver interface — every view talks only to this.

src/
  app/
    psu_app.c                     Entry point. No args → launcher; with --driver/--view → run that combination.
    launcher.{c,h}                SDL launcher window: pick driver/view/port, LAUNCH spawns new instance.
    psu_probe.c                   Non-SDL CLI for sanity-checking drivers without launching the GUI.

  drivers/
    registry.{c,h}                List of compiled-in driver factories — one line per driver.
    demo.{c,h}                    Synthetic 2-channel PSU (no hardware required).
    modbus_bridge/                ESP32 Modbus-text-bridge driver (Riden RD60xx etc.).
      modbus_bridge.{c,h}
      psu_protocol.{c,h}          Wire protocol used only by this driver.
    scpi_psu/                     One driver, many profiles. Currently shipped:
      scpi_psu.{c,h}                Siglent SPD3303; Keysight E3631A/3A/4A/5A;
                                    Rigol DP832/832A/811/711; R&S HMP4040/4030/2030/NGE103B;
                                    Keithley 2230G/2231A; classic HP 6632A/6633A/6634A.
                                  Works over either USB-serial or Prologix GPIB transport.
    korad/                        Korad KA-protocol driver — Korad / TENMA / Velleman / Hanmatek / clones.
      korad.{c,h}

  transport/                      Wire layers shared by drivers.
    serial_port.{c,h}             POSIX serial helper.
    scpi.{c,h}                    SCPI client API + port-spec parser (serial:/prologix: schemes).
    scpi_serial.c                 SCPI transport: direct USB-serial.
    scpi_prologix.c               SCPI transport: Prologix GPIB-USB-HPIB controller.

  views/
    views.h                       view_def_t + entry-point declarations.
    registry.c                    List of compiled-in views.
    toolbar_single.c              Compact 1-channel strip (ported).
    toolbar_dual.c                Compact 2-channel strip (ported).
    (full_single / full_dual — pending Phase 3c/d/e)

legacy/                           The original four standalone GUIs, kept building during the
                                  refactor. Each is self-contained. Deleted once all views are ported.

screenshots/  Makefile  README.md  LICENSE  .gitignore
```

## Layering

```text
   views   ──depend on──>  psu_driver.h          ◄── only public interface
                                ▲
                                │ implemented by
                                │
   drivers ──own their own── wire encoding (e.g. drivers/modbus_bridge/psu_protocol.c,
                                drivers/scpi_psu/scpi_psu.c, drivers/korad/korad.c)
                                │
                                ▼
                          transport/ (serial_port, scpi*)
```

Rules:

- Views never include driver or transport headers.
- Drivers never include view or app headers.
- The registry is the only place that knows the full list of drivers — adding
  a model is one line in `src/drivers/registry.c` + a new sibling folder (or
  one extra entry in `scpi_psu/scpi_psu.c` if it's another SCPI instrument).

## Port-spec grammar (SCPI drivers)

SCPI-class drivers (Siglent, Keysight) accept any of these `--port=...` formats:

```text
serial:/dev/ttyUSB0                 # direct USB-serial (default baud from factory)
serial:/dev/ttyUSB0:9600            # with explicit baud override
prologix:/dev/ttyUSB0:5             # Prologix GPIB-USB-HPIB controller, GPIB addr 5
prologix:/dev/ttyUSB0:5:115200      # with explicit controller baud
/dev/ttyUSB0                        # shorthand → serial:/dev/ttyUSB0
```

That same `--port=prologix:...` form lets any compiled-in Keysight/Agilent/HP
profile reach instruments living on a GPIB bus, since the transport is the
only thing that changes.

## How to add a new PSU model

**SCPI instrument** (Siglent / Keysight / Agilent / HP / any other SCPI PSU):

1. Add a `scpi_psu_profile_t` entry in `src/drivers/scpi_psu/scpi_psu.c` —
   one struct with the model's command formats and channel count.
2. Define a thin `static psu_driver_t *open_<model>(...)` that calls
   `scpi_psu_open(&k_<model>, ...)`.
3. Add a `psu_driver_factory_t <model>_factory` and declare it in
   `scpi_psu.h`.
4. Register it in `src/drivers/registry.c` and add the file to `DRIVER_SRCS`
   in the Makefile (already there for scpi_psu).

**Non-SCPI instrument** (custom wire protocol):

1. Create `src/drivers/<my_model>/<my_model>.{c,h}` implementing the
   `psu_driver_t` vtable + a factory.
2. Add it to `k_drivers[]` in `src/drivers/registry.c`.
3. Add its source to `DRIVER_SRCS` in the Makefile.

Every view picks it up automatically.

## How to add a new view layout

1. Create `src/views/<my_view>.c` exposing `int view_<my_view>_run(psu_driver_t *);`.
2. Declare its entry point in `src/views/views.h`.
3. Add an entry to `k_views[]` in `src/views/registry.c`.
4. Add its source to `VIEW_SRCS` in the Makefile.

The launcher discovers it automatically and greys it out for drivers whose
`n_channels_hint` is below the view's `min_channels`.
