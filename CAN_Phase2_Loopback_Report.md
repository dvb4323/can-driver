# CAN Phase 2 - Polling Loopback Integration

Date: 2026-10-15

## Result and verification boundary

`src/main.c` now owns CAN0 exclusively through the existing custom CAN wrapper. It initializes/starts the wrapper, submits exactly one frame, polls TX and RX independently, verifies the received ID/DLC/eight bytes, and retains debugger-visible results. CAN interrupt enable/handler installation and all direct FlexCAN_Ip calls have been removed from main.

The complete target project was rebuilt successfully with S32DS 3.4/NXP GCC 9.2, and application-control-flow tests passed on the host with wrapper doubles. **No target execution, flashing or hardware loopback success is claimed.** The PASS values below are acceptance criteria for the user's S32DS debug session.

The earlier Phase 1 documents describe the previous application integration state. Their statement that main still runs the original example is historical; this report records the Phase 2 change.

## Scope and preserved files

| File or area | Phase 2 result |
|---|---|
| `src/main.c` | Replaced example CAN sequence with wrapper-only loopback test; retained clock initialization |
| `CAN_Phase2_Loopback_Report.md` | Build results, test contracts, debugger expectations and hardware procedure |
| `CAN_Phase2_Build.log` | Saved successful forced full-project build output |
| `tests/can_phase2/test_loopback_main.c` | Exercises actual application test logic using wrapper doubles |
| `tests/can_phase2/fakes/Mcal.h`, `Clock_Ip.h` | Host-only compile surface, not embedded include paths |
| `Debug_FLASH/` | Regenerated build artifacts and rebuilt executable/map |
| `drivers/can/` | **Unchanged**; no blocker required a driver change |
| `RTD/`, `generate/` | **Unchanged** |
| `.cproject`, debugger launch, platform/startup/linker files | **Unchanged** after restoring incidental IDE scanner metadata |
| Existing analysis/design/reports and clock tool inputs | **Unchanged** |

No SPI, GUI, motor/protocol logic, CAN pin mux, SBC setup, physical bus mode or configuration regeneration was introduced. Existing `Clock_Ip_Init(&Clock_Ip_aClockConfig[0])` and its conditional PLL wait/distribution block are retained. That inherited clock block is outside the new CAN polling budget; a clock-startup stall does not become a fake CAN timeout.

The wrapper has a real RTD TX polling service (`MainFunctionWrite`), automatic RX re-registration and safe duplicate-TX refusal. It was sufficient for integration, so `can_driver.c` was not changed. CAN interrupts/DMA remain unused by the test execution path. Vendor IRQ functions may still be linked, but main installs/enables none; the wrapper disables/clears CAN0 NVIC sources and uses polling metadata. RTD critical sections can temporarily mask/restore CPU interrupts without making CAN interrupt-driven.

## Application test sequence

Key locations in the current main:

| Source location | Behavior |
|---|---|
| `src/main.c:22` | `CAN_TEST_POLL_BUDGET`, default 1,000,000 polling rounds |
| `src/main.c:100` | Global volatile result structure `g_can_test` |
| `src/main.c:103` | Reset result snapshot and expected frame |
| `src/main.c:162` | Run wrapper initialization and the test |
| `src/main.c:194` | The sole `CanDriver_Transmit` call site, with no retry |
| `src/main.c:208` | Bounded foreground loop; service TX first and then RX each eligible round |
| `src/main.c:292` | `CanTest_HoldResults`, permanent terminal breakpoint location |
| `src/main.c:304` | Preserved clock initialization call |

The frame is fixed in application code:

```text
CAN0, internal loopback, standard data frame
ID  = 0x14 (20 decimal)
DLC = 8
Data = 01 02 03 04 05 06 07 00
TX MB0, RX MB1
```

1. Clear the result structure and enter clock stage with status NOT_STARTED.
2. After clock initialization, mark RUNNING; call `CanDriver_Init` with RX ID0x14, then `CanDriver_Start` only if Init succeeded.
3. Call `CanDriver_Transmit` once. OK means accepted, not completed. Record the call count and `tx_accepted` separately.
4. Each foreground round polls TX until its first completion/error, and RX until its first delivered frame/error. RX can complete before TX, or TX before RX; neither alone permits PASS.
5. Compare the delivered ID, DLC and all eight payload bytes. Retain the delivered frame and mismatch bits. RX_OVERRUN is a test failure even when the retained frame matches.
6. Finish on the first failure, both successful completions, or exhaustion of the iteration budget. First-failure reason/API status are latched and never replaced by subsequent errors.
7. Enter `CanTest_HoldResults` without stopping/deinitializing/reinitializing CAN, retrying TX or clearing the results. PASS leaves the wrapper RUNNING with its next RX already registered. FAIL/TIMEOUT also leave hardware/state intact for inspection.

### Independent progress and fault handling

`tx_completed` and `rx_completed` represent separate events. `rx_completed=1` means a frame was delivered to the application, including a mismatching/overrun frame; it is not proof that comparison passed. `tx_error`/`rx_error` are separate from those completion flags.

If TX polling reports an error while the wrapper still permits RX calls, the same round also services RX. If the wrapper enters FAULT, main does not call RX on that invalid state; `rx_blocked_by_driver_fault` records why its progress stopped without fabricating an RX return code. The reverse condition is reflected by `tx_blocked_by_driver_fault` when TX is still unconfirmed after an RX-induced wrapper fault.

An unexpected RX BUSY is an error, not NO_DATA: the wrapper can return BUSY while entering FAULT if RTD re-registration fails. Init/start/transmit failures skip subsequent unsafe operations. Call counts distinguish an operation that was never called from its initialized NO_TRANSFER status sentinel.

### Timeout and ownership contract

The default budget is **1,000,000 foreground rounds**, not milliseconds or microseconds. Each pending direction is serviced at most once per round. `iterations` never exceeds the compiled budget. `iteration_budget` is informational; editing that RAM field does not alter the loop bound. To change the budget, change the macro or supply a compiler definition and rebuild.

On budget exhaustion, the test sets TIMEOUT and records which direction was incomplete. A vendor `CAN_STATUS_TIMEOUT` from lifecycle/service calls also yields TIMEOUT, but with its API-specific first-failure reason. Existing RTD dummy-counter waits are not calibrated elapsed time, and the application does not pretend otherwise.

**No timeout releases MB0 ownership.** The test never calls Stop, Deinit or a TX-abort operation on any outcome. `tx_completion_unconfirmed` is set before the single submit attempt and cleared only after PollTx returns OK. It is conservative software evidence, **not a live hardware BUSY bit**: hardware may complete after polling ends. A submit failure can also leave this flag1 because main does not assume whether the failed operation launched anything. Inspect `tx_accepted`, `transmit_status` and `transmit_calls` with it.

After a mismatch/failure, polling stops to preserve the first result even if another direction remains pending. There is no second frame or automatic recovery. Reset and run from startup to repeat the test; do not restart main at its current PC or modify wrapper internals to reuse MB0.

## Debugger-visible results

Add `g_can_test` and `exit_code` in the S32DS Expressions view. The entire result structure is volatile RAM, so optimized builds retain it. It is a stable snapshot once execution reaches the hold function. Terminal `status` is written after the other result fields.

### Status, stage and reason encodings

| Field | Encoding |
|---|---|
| `status` | NOT_STARTED0, RUNNING1, PASS2, FAIL3, TIMEOUT4 |
| `stage` | RESET0, CLOCK1, INIT2, START3, TRANSMIT4, POLL5; retains last attempted stage |
| `first_failure` | NONE0, INIT1, START2, TRANSMIT3, TX_POLL4, RX_POLL5, FRAME_MISMATCH6, RX_OVERRUN7, DRIVER_STATE8, BUDGET9 |
| `driver_state` | Wrapper UNINITIALIZED0, READY1, RUNNING2, FAULT3 |
| Public API status | OK0, BUSY1, NO_DATA2, NO_TRANSFER3, INVALID_ARGUMENT4, BAD_STATE5, CONFIG_ERROR6, TIMEOUT7, HW_ERROR8, INVALID_FRAME9, RX_OVERRUN10 |
| `exit_code` | 0 for PASS (also initial value), 1 for failure/API timeout, 2 for polling-budget timeout; always inspect `status` too |

### Expected values for a successful board run

| Expression / field | Required value at `CanTest_HoldResults` |
|---|---|
| `g_can_test.magic` | `0x43414E32` ("CAN2") |
| `g_can_test.status` | CAN_TEST_PASS / 2 |
| `g_can_test.stage` | CAN_TEST_STAGE_POLL / 5 |
| `g_can_test.first_failure` | CAN_TEST_FAILURE_NONE / 0 |
| `g_can_test.driver_state` | CAN_DRIVER_RUNNING / 2 |
| `init_status`, `start_status`, `transmit_status` | CAN_STATUS_OK / 0 |
| `tx_poll_status`, `rx_poll_status` | CAN_STATUS_OK / 0 |
| `init_calls`, `start_calls`, `transmit_calls` | Exactly 1 each |
| `tx_accepted`, `tx_completed`, `rx_completed` | 1 each |
| `id_match`, `dlc_match`, `payload_match` | 1 each |
| `mismatch_mask` | 0 |
| `tx_error`, `rx_error` | 0 each |
| `tx_budget_exhausted`, `rx_budget_exhausted` | 0 each |
| `tx_blocked_by_driver_fault`, `rx_blocked_by_driver_fault` | 0 each |
| `tx_completion_unconfirmed` | 0 |
| `received.id`, `received.dlc`, `received.data[0..7]` | `0x14`, 8, `{1,2,3,4,5,6,7,0}` |
| `iteration_budget` | 1000000 |
| `iterations`, `tx_poll_calls`, `rx_poll_calls` | Each between1 and budget; exact counts are not predetermined |
| `first_failure_api_status` | NO_TRANSFER / 3 sentinel when there was no failure |
| `exit_code` | 0 |

Fields in the table without the object prefix are members of `g_can_test`. Before a valid RX delivery, `received` remains zero-initialized and comparison flags0 mean **not checked**, not necessarily a mismatch. The mismatch mask uses bit0 for ID, bit1 for DLC and bits2..9 for payload bytes0..7; for example `0x200` identifies byte7 mismatch.

For a pure budget timeout, status4, reason9, iterations1000000 and exit_code2 are expected, with one or both direction-specific budget flags1. Completed directions keep their completion/match information. For an API failure/timeout, the stage, call counts and first API status identify the failing operation; no cleanup overwrites them.

## S32DS 3.4 build and hardware procedure

1. Open the existing project in your normal S32DS workspace. Refresh Project Explorer. Select **Debug_FLASH**. Do not regenerate peripherals or switch generated FlexCAN to normal mode.
2. Confirm the existing build includes `drivers/can/src/can_driver.c` and `${ProjDirPath}/drivers/can/inc`. Build the project using the UI. Confirm the console compiles the updated `src/main.c`, links the project and finishes without errors. If headers such as Mcal.h are missing, resolve the existing PlatformSDK path in the workspace; do not modify RTD sources to hide the error.
3. Connect/power the S32K144EVB and select the correct probe in the existing `FlexCAN_Ip_example_S32K144_Debug_FLASH_PNE` debug configuration. Ensure its program/symbol file is the newly built `Debug_FLASH/FlexCAN_Ip_example_S32K144.elf`, not a previous executable. Launch debugging and stop at main/startup as configured.
4. Set a **function breakpoint** at `CanTest_HoldResults` before resuming. A GDB console alternative is `break CanTest_HoldResults`. Function breakpoints avoid unreliable line stepping through the size-optimized build.
5. Add `g_can_test` and `exit_code` to Expressions. Resume normally rather than single-stepping each polling round. The test should reach the hold function with a terminal result; do not assume it will PASS.
6. Inspect all expected values above, especially both completions, exact payload, error flags and `transmit_calls==1`. Save those values with the build identity. If the breakpoint is not reached, suspend and inspect PC/call stack: CLOCK stage with NOT_STARTED points to inherited clock setup; INIT/START stage can identify an RTD handshake/fault.
7. To confirm the polling hardware configuration, inspect only nonintrusive control registers **after the test**: CAN0 IMASK1 at `0x40024028` should be zero, and CAN0 IRQ enable bits78..82 should be clear. They are NVIC ISER[2] bits14..18 (`ISER[2] & 0x0007C000 == 0`). These are expected checks, not observations from this implementation session.
8. Avoid automatic peripheral-window refresh of message-buffer CS or TIMER during the run: reads can lock/unlock an RX MB and perturb the test. Use the application RAM results first. Do not write flags/registers or invoke wrapper functions from the debugger as a diagnostic probe.
9. Repeat by a reset/debug restart that executes startup RAM initialization and main from the beginning. No recovery or second-transmit path exists in the terminal loop.

Internal loopback requires no second CAN node, external CAN wiring or SBC/pin-mux implementation for this software test. It does not validate physical CAN pins, transceiver, termination or external acknowledgment. A matching RAM result also does not measure bitrate: intended SYS_CLK/PE48MHz and calculated500kbit/s remain hardware-unverified. Expect no physical CAN TX frame as proof of internal loopback.

## Build results and evidence

The initial build encountered pre-existing **all-zero Debug_FLASH artifacts**, including makefile/response files, objects and the old ELF. Their original content cannot substantiate an earlier build. The cause was not determined; source/configuration files inspected for this phase were readable.

S32DS headless regeneration produced valid make/response files, but its first attempt still encountered zero-filled dependencies/objects. Then 36 confirmed all-zero `.d` build dependency files were removed within the verified Debug_FLASH path, and a forced full rebuild (`make -B -j4 all`) recompiled the application, custom driver, all RTD/generated sources and startup code. No generated source file was changed. The final ELF has a valid ELF signature, and the linker actually completed.

**Final build: exit code0.** NXP GCC9.2 Arm, existing C99/Cortex-M4/Thumb/hard-float/newlib-nano configuration. Final size output:

```text
text    data    bss    dec    hex
32840   48      6888   39776  9b60
```

The linker map contains `CanTest_HoldResults` at0xC40 and `g_can_test` at0x1FFF9474 for this build. Use symbols rather than hardcoded addresses in future builds. The existing project optimization settings were not changed; `-ggdb3` provides the debugger symbols.

Two linker warnings remain: `libc_nano.a` memcpy-stub and memset objects use variable-size enums while the project uses32-bit enums. No C compile warnings/errors appear in the successful full rebuild log. The S32DS launcher also printed unresolved GHS toolchain manifests for unrelated toolchains during regeneration. These do not constitute a warning-free environment, and no claim is made that the linker ABI warnings have been fixed.

Successful build output is saved as `CAN_Phase2_Build.log`. The isolated headless workspace still does not resolve the Eclipse PlatformSDK include substitution by itself; as in Phase1, this build supplied the installed header paths through GCC's process-local `C_INCLUDE_PATH`, without changing `.cproject` or persistent settings. Reproduction after S32DS generates makefiles:

```powershell
$sdkBase = 'C:/NXP/S32DS.3.4/S32DS/software/PlatformSDK_S32K1_2022_02/SW32K1_RTD_4_4_1_0_1_D2202'
$env:C_INCLUDE_PATH = "$sdkBase/Base_TS_T40D2M10I1R0/header;$sdkBase/Base_TS_T40D2M10I1R0/include;$sdkBase/Platform_TS_T40D2M10I1R0/include;$sdkBase/Platform_TS_T40D2M10I1R0/startup/include"
$env:Path = 'C:/NXP/S32DS.3.4/S32DS/build_tools/gcc_v9.2/gcc-9.2-arm32-eabi/bin;C:/NXP/S32DS.3.4/S32DS/build_tools/msys32/usr/bin;' + $env:Path
# From project/Debug_FLASH, with valid S32DS-generated makefiles:
make -B -j4 all
```

## Host test evidence and remaining verification

`tests/can_phase2/test_loopback_main.c` includes the actual main source with its entry renamed, substitutes wrapper/clock dependencies and calls the actual test routine. Its budget is4 rounds for deterministic boundary testing. It tests:

- Matching frame, RX-before-TX and TX-before-RX completion.
- Both directions pending at budget exhaustion and RX-only budget exhaustion.
- Mismatching ID, DLC or last payload byte, including mismatch while TX remains pending.
- RX overrun rejection, failed Init/Start/Transmit, independent polling errors and first-error retention.
- TX-induced wrapper FAULT suppressing invalid RX calls, and RX BUSY with FAULT treated as failure.
- Exactly one TX submission and no Stop/Deinit cleanup on any exercised outcome.

Host GCC compiled with `-std=c99 -Wall -Wextra -Werror -pedantic`; the executable returned0 and printed:

```text
PASS: Phase 2 loopback application control flow and diagnostic snapshots (wrapper doubles only)
```

Reproduction from project root with a host compiler:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Itests/can_phase2/fakes -Idrivers/can/inc tests/can_phase2/test_loopback_main.c -o can_phase2_main_tests.exe
./can_phase2_main_tests.exe
```

The host executable was run in the task's visualization workspace. These tests verify application control flow and snapshots, not RTD runtime, NVIC state, FlexCAN register handshakes, actual loopback reception or silicon behavior. Those require the board procedure above. No new RTD workaround or physical CAN implementation is justified by host results alone.
