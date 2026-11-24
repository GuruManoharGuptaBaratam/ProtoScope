# Phase 1 — ESP32-S3 foundation

**Milestone:** M1 — repeatable ESP-IDF build and board-only boot  
**Target:** ESP32-S3 N16R8 (module suffix confirmed by user)  
**ESP-IDF:** 5.5.x (v5.5.6 pinned for the milestone)  
**External protocol drivers:** intentionally not started in Phase 1

This document describes the foundation retained by firmware 0.2.0. Phase 2
adds UART commands but leaves the external driver stopped at boot. Follow
[the N16R8 recovery checklist](psram-recovery-and-post-flash.md) for the reported
PSRAM error, then [Phase 2](phase-2-uart.md) for interface testing.

## 1. Phase boundary

Phase 1 establishes the project skeleton, build configuration, safe GPIO state,
clock/power policy, diagnostics, FreeRTOS execution structure, and a development
console. It does not claim that UART, CAN, RS-485, I2C, SPI, TFT, rotary
encoder, or microSD communication works.

The Phase-0 GPIO allocation is represented in one place:
`components/proto_board/include/proto_board.h`. The board layer validates the
plan for duplicate or invalid pins on every boot. It then parks assigned pins in
inactive states without installing peripheral drivers.

## 2. Code and module layout

```text
ProtoScope/
├── CMakeLists.txt                 ESP-IDF project entry
├── version.txt                    firmware version without requiring Git
├── sdkconfig.defaults             reproducible target defaults
├── partitions.csv                lower 8 MB of 16 MB flash; dual OTA slots
├── pytest.ini
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml         ESP-IDF version constraint
│   └── app_main.cpp              ordered startup only
├── components/
│   ├── proto_board/              pins, pin-plan validation, safe GPIO state
│   ├── proto_platform/           NVS, clock/power, chip/heap diagnostics
│   ├── proto_tasks/              supervisor, worker queue, health counters
│   └── proto_console/            UART0 REPL and diagnostic commands
├── tests/
│   └── test_phase1_boot.py       pytest-embedded hardware smoke test
└── tools/
    ├── idf_phase1.sh             select Phase 1 SDK/Python and run idf.py
    └── verify_phase1.sh          environment/reconfigure/build check
```

This layout keeps hardware policy out of `app_main`, leaves future interface
drivers as separate components, and prevents the later route engine, UI, and
storage work from becoming mutually dependent.

Recommended later additions are:

```text
components/proto_event/       common event envelope (implemented in Phase 6)
components/proto_route/       route registry and executor (implemented in Phase 7)
components/proto_uart/        external UART adapter
components/proto_twai/        Classic CAN/TWAI adapter (implemented in Phase 3)
components/proto_rs485/       half-duplex UART adapter (implemented in Phase 4)
components/proto_i2c/         controller transaction adapter (implemented in Phase 5)
components/proto_spi/         external controller transaction adapter (implemented in Phase 5)
components/proto_ui/          OLED and encoder (implemented in Phase 9)
components/proto_storage/     microSD and logger (implemented in Phase 8)
```

Do not add pair-specific components such as `uart_to_can`; conversion routes
must be composed from source, mapping, and destination layers.

## 3. First-time workstation setup

ESP-IDF does not support spaces in the project path. The current authoring path
contains `My Projects`, so create the build checkout at a no-space path such as
`$HOME/esp/ProtoScope` before running any `idf.py` command. Do not rely on a
symlink to the space-containing directory: `idf.py` resolves the physical path.

The manifest requires `>=5.5,<6.0`; the milestone baseline is **v5.5.6**.
`source ~/esp/esp-idf-v5.4/export.sh` selects 5.4 and cannot meet that requirement.
ESP-IDF 6.x is also outside the allowed range. Keep other installed releases for
their own projects, and activate 5.5.6 in a **fresh terminal** for ProtoScope.

Install Espressif ESP-IDF v5.5.6 using the official installer or clone. Clone only
if `$HOME/esp/esp-idf-v5.5.6` does not already exist:

```bash
mkdir -p "$HOME/esp"
git clone --branch v5.5.6 --recursive https://github.com/espressif/esp-idf.git \
    "$HOME/esp/esp-idf-v5.5.6"
```

Install the compiler tools and Python environment, including when the source
folder was cloned earlier but installation was not completed:

```bash
cd "$HOME/esp/esp-idf-v5.5.6"
./install.sh esp32s3
source ./export.sh
idf.py --version
```

Expected version: `ESP-IDF v5.5.6`. Run `source` again in each new terminal;
`export.sh` activates a version and does not install or upgrade it.

For everyday Phase 1 commands, use the project launcher to avoid inheriting an
older SDK or Python environment from another project:

```bash
cd "$HOME/esp/ProtoScope"
./tools/idf_phase1.sh --version
./tools/verify_phase1.sh
```

The launcher selects `$HOME/esp/esp-idf-v5.5.6` and an installed
`$HOME/.espressif/python_env/idf5.5_py*_env` in a child shell. It accepts all
`idf.py` arguments, for example `./tools/idf_phase1.sh menuconfig` and
`./tools/idf_phase1.sh -p PORT flash monitor`. For tools installed somewhere else,
set `PROTOSCOPE_IDF_TOOLS_PATH` to that tools directory.

If a build command still prints `idf5.4_py3.9_env`, that terminal is running
the 5.4 environment. Cleaning the build cannot change the activated SDK. Use
`./tools/verify_phase1.sh` to rebuild; a failed version check does not require
another `fullclean`. Bare `idf.py` commands still use the parent terminal's
environment, so use `./tools/idf_phase1.sh` consistently for this project.

If Python on macOS reports `CERTIFICATE_VERIFY_FAILED` while downloading the
tools, retry the installer with macOS's trusted CA bundle:

```bash
SSL_CERT_FILE=/etc/ssl/cert.pem ./install.sh esp32s3
```

This keeps HTTPS certificate verification enabled. For a missing Python virtual
environment, rerun `install.sh` and `export.sh` in the same fresh terminal so
both use the same Python version.

> **Update (firmware 0.3.0):** `./tools/idf_phase1.sh` now does this copy
> automatically whenever the project path contains spaces. It mirrors the source
> to `~/esp/protoscope-build` (override with `PROTOSCOPE_BUILD_MIRROR`) before
> every command and builds/flashes from there, so the manual rsync below is only
> needed if you prefer your own copy. See [`vscode-and-idf-setup.md`](vscode-and-idf-setup.md).

Create a clean build copy from the current workspace. This excludes the failed
build cache and generated configuration while retaining `sdkconfig.defaults`:

```bash
mkdir -p "$HOME/esp/ProtoScope"
rsync -a --exclude='/build/' --exclude='/sdkconfig' \
    --exclude='/sdkconfig.old' --exclude='/dependencies.lock' \
    --exclude='/managed_components/' --exclude='.DS_Store' \
    "$HOME/Desktop/My Projects/ProtoScope/" "$HOME/esp/ProtoScope/"
```

The first copy should go into a new directory. For later source updates, the same
command updates the build copy while preserving its generated configuration.
Avoid editing both copies independently. Open `$HOME/esp/ProtoScope` in VS Code
when using its build buttons.

The patch release may be advanced within ESP-IDF 5.5.x after a clean build and
boot regression. A major-version migration requires a separate compatibility
pass. Record `idf.py --version` in the Phase 1 test evidence.

From the no-space project checkout:

```bash
cd "$HOME/esp/ProtoScope"
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

`set-target` resets the generated configuration; use it for initial setup or a
target change. Subsequent builds can use `idf.py build` or
`./tools/verify_phase1.sh`, which selects the Phase 1 environment and preserves
reviewed board settings. A fresh
build also gets `esp32s3` from `sdkconfig.defaults` without `set-target`.

In VS Code, run **ESP-IDF: Select Current ESP-IDF Version** and select the
`esp-idf-v5.5.6` installation. The workspace's `idf.currentSetup` points to that
path; the extension's selected environment must agree with it. Run **ESP-IDF:
Doctor Command** to inspect the result, and create a new ESP-IDF terminal after
switching versions. The former v6.0.1 setup is not suitable for this milestone.

Common configuration messages:

| Message | Meaning and recovery |
| --- | --- |
| `no versions of idf match >=5.5,<6.0` or `Phase 1 requires ESP-IDF >=5.5,<6.0` | The activated SDK is outside the manifest range. Use `./tools/verify_phase1.sh` from the build copy to select v5.5.6 and its Python environment. Do not lower the dependency requirement to hide the mismatch. |
| `IDF_TARGET not set, using default target: esp32` | The original build did not select the S3. Use `idf.py set-target esp32s3`; the updated defaults also select S3 on a fresh build. |
| `not a git repository` / `Could not use 'git describe'` | Version discovery notices, not the dependency-solving failure. `version.txt` provides the firmware version without requiring a Git repository. |
| `Dependencies lock doesn't exist` | Normal on the first configuration; successful dependency solving creates the lock file. |
| `Directory ... doesn't seem to be a CMake build directory` | A failed configure left an incomplete `build/`, so `set-target`/`fullclean` refuses to remove it. Use the clean copy above, or rename `build/` to an unused backup name before retrying. |

Review these menuconfig values before the first flash:

- Target is ESP32-S3.
- Flash size is 16 MB for the confirmed N16R8 module.
- PSRAM is enabled in Octal mode at 40 MHz for the R8 part; startup memory test enabled.
- CPU maximum is 240 MHz; Phase 1 dynamic minimum is 80 MHz.
- Light sleep is disabled so serial diagnostics remain deterministic.
- Console is UART0 at 115200 baud.
- Partition table is `partitions.csv` and fits the detected flash.

The original N8R2 assumption was corrected on 2026-11-10 after the user
confirmed N16R8. Follow [N16R8 recovery](psram-recovery-and-post-flash.md)
to update an existing generated `sdkconfig`; defaults alone do not override it.
The partition table still uses the lower 8 MB; the upper 8 MB remains reserved.

## 4. Startup sequence

`app_main()` performs an intentionally short, fail-fast sequence:

1. Set the configured log level.
2. Initialize NVS, erasing only when ESP-IDF reports an incompatible/filled NVS
   partition during this foundation phase.
3. Apply the 80–240 MHz dynamic frequency policy; keep automatic light sleep
   off.
4. Validate the GPIO map and park pins in safe states.
5. Start the FreeRTOS worker queue and supervisor task.
6. Print chip, flash, PSRAM, reset, and heap diagnostics.
7. Register and start the UART0 console.
8. Emit `ProtoScope Phase 1 foundation ready`.

The worker queue is a small architectural seam for later asynchronous work. It
is not the future protocol event queue. The supervisor emits a low-rate health
record and exposes queue-overflow and task-cycle counters.

## 5. Console and logging

Connect the board's USB-to-UART/programming connector and open the ESP-IDF
monitor at 115200 baud. Available commands are:

- `help` — list commands.
- `status` — show uptime, heap, PSRAM state, and task counters.
- `heap` — show current and minimum free heap.
- `reboot` — restart the board.

Expected boot evidence includes:

```text
Pin plan validated; allocated GPIOs parked in Phase-1 safe states
FreeRTOS runtime started: worker priority 5, supervisor priority 4
Interactive console ready on UART0 at 115200 baud
ProtoScope Phase 1 foundation ready
Phase 2 UART available; stopped until 'uart start'
Phase 3 CAN available; stopped until 'can start'
Phase 4 RS-485 available; stopped until 'rs485 start'
Phase 5 I2C/SPI controllers available; stopped until 'i2c start' / 'spi start'
Phase 6 common events available through 'msg'
Phase 7 routing engine available through 'route'; all 20 routes disabled
Phase 8 monitor and microSD logger available through 'monitor' and 'log'
Phase 9 display and encoder UI available through 'display' and 'ui'
```

## 6. Testing and verification

### Build verification

```bash
cd "$HOME/esp/ProtoScope"
./tools/verify_phase1.sh
./tools/idf_phase1.sh size-components
```

Pass criteria:

- clean compile with no project warnings;
- app and partition image fit the lower-8-MB layout on the 16 MB flash;
- no unexpected large component is linked;
- generated `sdkconfig` retains the reviewed target, flash, console, CPU, and
  PSRAM settings.

### Board smoke test

With only the ESP32-S3 board connected by a known data-capable USB cable:

```bash
./tools/idf_phase1.sh -p PORT flash monitor
```

Pass criteria:

- flash, reset, and boot repeatably at least five times;
- no boot loop, panic, watchdog, or brownout message;
- detected flash is 16 MB and PSRAM is initialized at 8388608 bytes;
- the ready banner appears within 10 seconds after reset;
- `help`, `status`, and `heap` respond;
- the supervisor logs at the configured interval;
- `minimum_free_heap_bytes` remains stable during a 30-minute idle soak;
- `runtime_queue_overflows` stays zero;
- `reboot` returns to the same ready state.

### Automated hardware smoke test

Install Espressif's pytest-embedded dependencies in the active ESP-IDF Python
environment, then run:

```bash
pytest --embedded-services esp,idf -k phase1_boot
```

If the local ESP-IDF release expects explicit serial/target options, pass its
documented `--port` and `--target esp32s3` arguments. Save the test log with the
board markings, ESP-IDF version, firmware Git revision (once the directory is a
repository), port, and test date.

### Optional electrical verification

With no modules attached, use a high-impedance multimeter or logic analyzer to
confirm that chip-select and serial-TX idle pins do not show an unsafe voltage.
Do not short or load GPIOs merely to test the park state.

## 7. Physical hardware and wiring callouts

### Required now

- One verified ESP32-S3 development board.
- One data-capable USB cable connected to the board's documented programming
  connector.
- A host computer running ESP-IDF.

No breadboard, CAN transceiver, RS-485 module, TFT, encoder, microSD module, I2C
fixture, SPI fixture, termination resistor, or external power supply is required
for the Phase 1 acceptance test.

### Explicitly do not wire yet

- Do not attach 5 V logic to any GPIO.
- Do not attach the CAN or RS-485 bus pins before the exact module schematic,
  pin labels, supply, termination, and biasing are recorded.
- Do not attach TFT or microSD until their shared-bus topology, logic voltage,
  and current draw are confirmed.
- Do not add I2C pull-ups until existing breakout pull-ups and the intended bus
  voltage are known.
- Do not attach external JTAG to GPIO39–GPIO42; those pins are allocated to the
  future external SPI port. Use the board's documented USB Serial/JTAG path if
  debugging is required.

### Required before Phase 2 interface bring-up

The N16R8 suffix is confirmed. Record the complete board/module marking and
board revision, verify the
16 MB flash / 8 MB Octal PSRAM identity, confirm which USB connector is used for
programming, and freeze the GPIO table. For each module about to be connected,
record the exact SKU, schematic/pinout, supply voltage, logic voltage, current
budget, and termination/pull-up/bias arrangement. Perform a power-off
continuity review before applying USB power.

## 8. Phase 1 exit criteria

Phase 1 is complete when:

- the project builds from a clean ESP-IDF 5.5.x environment at a path without
  spaces;
- the physical board identity matches the selected configuration;
- five cold/warm boot cycles pass;
- the console and health task work for a 30-minute soak;
- no external interface driver or unverified module is needed for the test;
- the build and test evidence is saved with toolchain and hardware identity.

## 9. ESP-IDF references

- [ESP-IDF v5.5.6 Linux/macOS setup](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/linux-macos-setup.html)
- [ESP32-S3 power management API](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/power_management.html)
- [ESP-IDF console component](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/console.html)
- [ESP-IDF releases](https://github.com/espressif/esp-idf/releases)
