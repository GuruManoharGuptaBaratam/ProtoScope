# N16R8 recovery and post-flash checklist

The user confirmed **N16R8** on 2026-11-10. This means **16 MB flash and
8 MB Octal PSRAM**. The previous build selected 8 MB flash and Quad PSRAM
for a provisional N8R2 board. The reported `quad_psram` failure happens
before `app_main()`, so application code cannot repair it at runtime.

## Apply the fix

From the authoring workspace, copy the updated sources to the no-space build
directory. This preserves its generated configuration for the migration:

```bash
rsync -a --exclude='/build/' --exclude='/sdkconfig' \
    --exclude='/sdkconfig.old' --exclude='/sdkconfig.before-n16r8-*' \
    --exclude='/dependencies.lock' --exclude='/managed_components/' \
    --exclude='.DS_Store' \
    "$HOME/Desktop/My Projects/ProtoScope/" "$HOME/esp/ProtoScope/"
cd "$HOME/esp/ProtoScope"
python3 tools/configure_n16r8.py
./tools/idf_phase1.sh reconfigure build
./tools/idf_phase1.sh -p PORT flash monitor
```

Use the actual programming port for `PORT`. Exit a running ESP-IDF monitor with
Ctrl+] before flashing. The migration saves a timestamped copy of the old
`sdkconfig` and updates only the memory configuration. Changing
`sdkconfig.defaults` alone does **not** override an existing `sdkconfig`.

Equivalent menuconfig settings (`./tools/idf_phase1.sh menuconfig`):

- **Serial flasher config → Flash size → 16 MB**.
- **Component config → ESP PSRAM → Support for external, SPI-connected RAM**:
  enabled.
- **SPI RAM config → Mode (QUAD/OCT) → Octal Mode PSRAM**.
- RAM clock: **40 MHz** for initial verification; type: **Auto-detect**.
- Initialize PSRAM during startup and run its memory test: enabled.
- Ignore PSRAM when not found: disabled.

Flash mode and PSRAM mode are separate settings. Octal PSRAM does not require
enabling Octal flash. The partition offsets are unchanged and use only the
lower 8 MB of the 16 MB flash; erasing all flash is unnecessary for this fix.

## Board-only acceptance

Disconnect external fixtures for the first boot and leave the external UART
driver stopped. Check these in order:

| Test | Expected result |
|---|---|
| First boot | No `quad_psram` error, reset loop, panic, brownout or watchdog |
| PSRAM initialization | Octal PSRAM detected; ESP-IDF memory test passes |
| Application diagnostics | `Flash: 16 MB`, `PSRAM: 8388608 bytes`, ready banner within 10 seconds |
| `help` | Lists diagnostic commands |
| `status` | `psram_initialized: yes`; runtime queue overflows zero |
| `heap` | Positive current/minimum heap values; no continuing decline after warm-up |
| Idle | Health record every 10 seconds; console remains responsive |
| `reboot` | Same successful boot and memory result |
| Reset/power cycles | At least five successful warm/cold boots |
| 30-minute soak | No reset/panic; runtime queue overflows remain zero; stable heap |

Record the complete module/carrier revision, serial port, firmware version,
ESP-IDF version and boot log. Only the N16R8 suffix is currently confirmed.
No board serial port was present during development, so a successful local
build is not evidence of a successful physical boot.

If Octal mode at 40 MHz still fails, verify the generated settings and that
the freshly built image was flashed to this board. Power off, disconnect
external wiring, check power/cable and retry. GPIO35/36/37 are reserved for
octal memory. Capture the complete new log if failure persists; do not mask it
with `CONFIG_SPIRAM_IGNORE_NOTFOUND`.
