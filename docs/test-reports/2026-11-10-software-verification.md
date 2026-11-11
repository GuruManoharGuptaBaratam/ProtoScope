# N16R8 recovery and Phase 2 software verification

Date: 2026-11-10 (Asia/Kolkata). Firmware: **0.2.0**. SDK: **ESP-IDF v5.5.6**.

| Check | Result |
|---|---|
| Original generated configuration | Confirmed 8 MB flash and Quad PSRAM, matching the failing monitor log |
| Module identity | User confirmed N16R8 suffix; complete carrier revision still pending |
| Migration of existing sdkconfig | Passed; 16 MB flash / Octal PSRAM / 40 MHz / memory test enabled / ignore-notfound disabled |
| Fresh defaults configuration | Passed; same memory settings without an existing sdkconfig |
| Foundation with N16R8 fix | Built successfully before adding UART |
| Phase 2 firmware | Built successfully; no compiler warnings; app size `0x4b8d0`, 85% of smallest app slot free |
| UART framing host tests | Passed with AddressSanitizer and UndefinedBehaviorSanitizer |
| Framing coverage | Fragmented input, raw CRLF preservation, consecutive frames, exact 64-byte boundary, overlong/malformed rejection, all byte values, each split of an RT-01 example, partial timeout and resynchronization |
| Memory migration tests | 2 tests passed, including idempotence and preservation of unrelated settings |
| Python fixture/migration scripts | Syntax checks passed; fixture command-line help checked |
| Board flashing / boot / PSRAM memory test | Not run: no ESP serial port available |
| External UART 9600/115200 tests | Not run: no attached adapter/board fixture |

The reproducible build ran in `/private/tmp/protoscope-n16r8` because the
authoring path contains spaces. It started with a copy of the existing build
directory's `sdkconfig` to exercise migration rather than assuming defaults
would replace saved settings. A separate configuration pass used a new
`sdkconfig.fresh` file to check first-time setup.

The initial sandboxed SDK component-manager step could not inspect host
processes (`psutil`/`sysctl`). Automatic escalation review timed out. The
successful verification used `IDF_COMPONENT_MANAGER=0`; there are no managed
external component dependencies, and project CMake still checks ESP-IDF
`>=5.5,<6.0`. This is a local verification environment workaround, not a
project-default change. The SDK emitted its existing Bash-3.2 shell
autocompletion warning; compilation succeeded.

The firmware build log is saved in
[`2026-11-10-phase2-build.log`](2026-11-10-phase2-build.log).
Run `./tools/test_host.sh` to repeat the host tests, then
`./tools/verify_phase2.sh` from the no-space checkout for the normal build.

Hardware exit criteria remain open. Record the physical boot and external
UART test logs before marking M1 or M2 complete.

The source workspace contains the completed changes. Syncing them into the
existing `~/esp/ProtoScope` flashing copy was not executed: automatic
filesystem approval review failed under service load. Its existing saved
configuration is therefore still unchanged. The recovery guide includes the
source-copy and configuration-migration commands; approval is needed for the
agent to update that directory outside the writable workspace.
