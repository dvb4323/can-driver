# Phase 3 implementation report

**Application update:** The normal-profile frames and wait policy in this original report are superseded by `CAN_Phase3_Manual_GUI_Report.md`: TX 0x123 / `11 22 33 44 55 66 77 88`, manual RX 0x321 / `A1 B2 C3 D4 E5 F6 07 18`, and no normal-profile application iteration limit. Driver/RTD/board architecture and the default loopback/hardware gate are unchanged. Build and board testing of the update remain pending.

Date: 2026-10-15

## Result and authorization boundary

Implemented the approved software architecture over the existing FlexCAN RTD 1.0.1. Loopback remains the default. Normal-mode software is present behind an explicit test-profile choice and a separate default-disabled hardware-verification gate. **Physical CAN testing is not authorized until VP230 logic-voltage compatibility and onboard SBC RX isolation are verified and testing is authorized.** Termination and STM32 configuration still need verification as well.

No target build, host-test build/execution, flash or debug session was run in this phase. Existing build outputs are not evidence for this revision. The earlier Phase 2 loopback PASS was reported by the user on the physical board; this changed revision still requires regression testing.

## Compatibility review completed before edits

Read the final Phase 3 design, Phase 1 design/report and current wrapper/main. Verified actual RTD init/mode and diagnostics implementations rather than introducing SDK FLEXCAN_DRV or private hardware helper calls:

| Actual interface/behavior | Evidence in unchanged RTD |
|---|---|
| `Flexcan_Ip_ConfigType.flexcanMode` and mode enum | `RTD/include/FlexCAN_Ip_Types.h:164`, `:460` |
| Init applies configuration mode | `RTD/src/FlexCAN_Ip.c:1914` |
| NORMAL disables self reception; LOOPBACK enables it | `RTD/src/FlexCAN_Ip.c:382`; `RTD/src/FlexCAN_Ip_HwAccess.c:1640` |
| Nonblocking Send signature: instance, MB, data-info pointer, ID, payload pointer | `RTD/include/FlexCAN_Ip.h:226` |
| Send polling metadata suppresses transfer IRQ enabling | `RTD/src/FlexCAN_Ip.c:1997` |
| TX completion needs MainFunctionWrite, then software status | `RTD/src/FlexCAN_Ip.c:3615`, `:2546` |
| RX service and explicit Receive re-registration | `RTD/src/FlexCAN_Ip.c:3513`; existing wrapper sequence retained |
| `uint32 FlexCAN_Ip_GetErrorStatus(uint8 instance)` reads ESR1 | `RTD/include/FlexCAN_Ip.h:578`; `RTD/src/FlexCAN_Ip.c:2850` |
| `uint8 FlexCAN_Ip_GetControllerTxErrorCounter(uint8 instance)` reads ECR TX | Header `:589`; implementation `:2870` |
| `uint8 FlexCAN_Ip_GetControllerRxErrorCounter(uint8 instance)` reads ECR RX | Header `:600`; implementation `:2890` |

No actual RTD contradiction required an architecture change. Two implementation details were reported before coding: independent TX/RX calls cannot infer a once-per-round diagnostic sampling boundary, so an explicit `CanDriver_PollDiagnostics()` service was added; and a separate build gate was added to prevent an accidentally selected normal profile from reaching pins/CAN while hardware verification is unresolved. These preserve the approved adapter/polling architecture.

## Changed files

| File | Change |
|---|---|
| `drivers/can/inc/can_types.h` | Public mode/config extension; plain-C diagnostics/sample/fault-state types |
| `drivers/can/inc/can_cfg.h` | Explicit LOOPBACK default initializer |
| `drivers/can/inc/can_driver.h` | Mode contract and diagnostics poll/cache-getter APIs |
| `drivers/can/src/can_driver.c` | Static generated-config copy, mode-only override; RTD diagnostics capture/cache/fault preservation; register-access guards around failed Init and Stop/Deinit |
| `board/board_can.h`, `board/board_can.c` (new) | PORTE clock/pin-lock checks, ALT5 mux, controlled IRQC/pulls/ISF write value, readback and cached board results |
| `src/main.c` | Explicit profiles, hardware gate, normal-profile board setup, separate request/expected reply, independent completion/error results and cached diagnostics |
| `tests/can/test_can_driver.c`, existing fake headers | Mode/config-copy and diagnostic read/event/fault/ownership test coverage; real RTD mode enum values corrected in fake |
| `tests/can/test_board_can.c`, device fake headers (new) | Clock-disabled/locked-pin/no-unrelated-write and PCR write-value coverage |
| `tests/can/test_can_application.c`, Mcal/Clock fake headers (new) | Real application logic with mocked driver/board: default loopback, blocked normal, simulated enabled normal, ordering, budgets, mismatch, bus-off and API fault |
| `tests/can/run_tests.ps1` | Extend the existing optional host runner to all five executable cases; not executed |
| `CAN_Driver_Design.md`, `CAN_Phase3_Design.md` | Current Phase 3 contract/status; historical design retained and explicitly qualified |
| This report | Change/evidence record and user-operated validation procedure |

`RTD/`, `generate/`, `.mex`, `.cproject`, existing clocks/platform, SPI/motor/GUI code and target build outputs are unchanged. Existing `.cproject` board source/include entries already cover the new board module; no new project-setting edit was required. Tests/fakes remain outside target source/include paths. Do not add their include directory to the embedded project.

## Behavior and state contracts

### Mode selection and configuration isolation

`Can_Config.mode` accepts only LOOPBACK=0 or NORMAL=1. Default config is `{0x14, CAN_MODE_LOOPBACK}`. All previous ID/DLC/pointer/lifecycle checks remain. Each successful Init uses private static `s_rtd_config = FlexCAN_Config0`, with only `.flexcanMode` changed. Generated `FlexCAN_State0` remains RTD state. No generated constant or vendor source is written, and no RTD source is copied.

The existing generated-template loopback assertion remains: accidentally regenerated normal mode is rejected rather than silently changing the baseline. The effective private normal copy passes through the same RTD initialization path. Raw timing, expected 48 MHz PE clock and derived 500 kbit/s are retained; no physical bitrate measurement is claimed.

### Cached diagnostics

`CanDriver_PollDiagnostics()` requires RUNNING and performs one ESR1 read and separate ECR-counter reads using public RTD getters. Main calls it once each foreground round, independently of TX/RX completion. `CanDriver_GetDiagnostics(out)` is cache-only and available even in UNINITIALIZED/FAULT; null is INVALID_ARGUMENT. Its OK result means the copy succeeded, not that a hardware sample exists. Check `valid`.

Each sample decodes FLTCONF (00 active, 01 passive, 1x bus-off), TX/RX warnings and ACK/bit/CRC/form/stuff errors. `accumulated_error_events` ORs only selected ESR1 event masks, never FLTCONF/current activity bits. `first_error` records the first sampled event, warning or non-active confinement state. `first_fault` records the first wrapper failure or sampled bus-off/BOFFINT. `sample_count` saturates. `bus_off_seen` remains set after controller recovery.

Protocol error bits can be affected by an ESR1 read. The cache getter, result hold loop and application first-failure capture perform no hardware reads. Wrapper API failures attempt an extra snapshot before FAULT; no RTD diagnostics/transfer calls follow failed RTD Init. Stop/Deinit sample before potentially disabling the module, and access guards prevent sampling it afterward. No automatic ClearErrorStatus call was added. Do not enable live ESR1 peripheral/register watches in S32DS.

Snapshots persist through Stop/Deinit and failed reinitialization. A complete successful Init resets them. Retained `valid=1` after failed reinitialization can refer to the last successful session; it does not prove a new sample was possible. If there has never been a sample, the first-fault sample-valid flag is zero. The getter overlays current software state/TX pending even when its hardware snapshot is older. ESR1/ECR samples are not atomic and observations can miss events already consumed by another reader.

Bus-off fails the application test without cancelling TX or changing ownership. Ordinary transient error indications are retained diagnostically while polling continues. Generated bus-off recovery/retry behavior remains unchanged. On TIMEOUT/FAIL, pending TX storage is not overwritten, stopped, deinitialized or retried by the application. The controller can still retry on the wire while software holds results. This is a bounded foreground test, not a hard real-time transmit deadline.

### Board setup

Only NORMAL profile, after the hardware gate, calls `BoardCan_InitPins`. It checks PORTE PCC PR/CGC, reads both PCRs, and rejects incompatible locked pins before changing either. Compatible locked ALT5/no-pull/no-IRQ settings are accepted without writes. Unlocked pins receive masked MUX=5, IRQC=0, PE=0, PS=0; unrelated pad settings are retained and ISF=0 is written to avoid replaying a W1C flag. PCR readback verifies the requested fields. No PCC clock write, GPIO direction, CAN register write, SBC SPI initialization or other pin access is introduced.

Cached board diagnostics expose gate, before/after PCR4/PCR5 and validity. A readback failure records the resulting pin state without automatic rollback; do not start CAN. These software checks do not establish electrical isolation or logic voltage compatibility.

### Profiles and debugger results

| Build symbols | Behavior |
|---|---|
| None, or `CAN_TEST_PROFILE=0` | LOOPBACK; board setup skipped; unchanged request and expected Phase 2 payload |
| `CAN_TEST_PROFILE=1`, hardware symbol absent/0 | NORMAL_TWO_NODE selected but blocked before pins/CAN; intentional FAIL |
| `CAN_TEST_PROFILE=1`, `CAN_TEST_HARDWARE_VERIFIED=1` | Normal two-node execution; **reserved for later verified and authorized physical testing** |

`CAN_TEST_HARDWARE_VERIFIED` is a human build assertion, not an automatic probe. It remains default 0 in the delivered code. Do not enable it now. The low-level wrapper is reusable and does not itself enforce this application gate; another caller must meet the documented clock/pin/electrical ownership preconditions before using NORMAL.

Both running profiles submit exactly once: standard ID 0x14, DLC 8, `01 02 03 04 05 06 07 00`. Normal expects STM32 response ID 0x14, DLC 8, `A1 A2 A3 A4 A5 A6 A7 A8`; loopback expects the original payload. `g_can_test.request`, `.expected`, `.received`, `.profile`, `.mode`, `.board`, `.diagnostics` and `.failure_diagnostics` make the distinction visible.

Original status enum numbers remain NOT_STARTED=0, RUNNING=1, PASS=2, FAIL=3, TIMEOUT=4. Existing stage/failure values are preserved; appended values are BOARD stage=6, BOARD failure=10, HARDWARE_NOT_VERIFIED=11, DIAGNOSTICS=12, BUS_OFF=13. Failure capture is first-only. Error/budget/completion fields remain independent for TX and RX; a delivered mismatching RX still sets `rx_completed=1` while failing comparison. No clean-up runs at `CanTest_HoldResults`.

## Validation performed and limits

Performed static source checks only:

- Compared 24 host-fake ESR1/PORT/PCC numeric constants with the installed S32K144 device headers: matched.
- Compared the current Clock RTD initialization/PLL block with the original source: unchanged.
- Confirmed main has no direct FlexCAN RTD/interrupt-control calls and public CAN headers expose no RTD types.
- Checked `.cproject` includes the board source entry and excludes test source paths.
- Parsed the updated PowerShell host runner without executing it: no parser errors.
- Reviewed actual RTD API signatures, mode effects, polling completion services and diagnostics implementations.

Host tests are **written but uncompiled/unexecuted**. They use fakes, not an MCU model. The board fake checks the PCR write value; it does not emulate physical W1C semantics, pin locks, clock bus faults or electrical behavior. Application simulated normal mode is host-only and establishes no physical-test authorization. The strict host runner remains available for a separately agreed user-operated workflow; it has not been launched.

No Phase 3 executable exists from this work. A previously present ELF is stale for this revision until the user completes a new build. A SHA-256 audit against the pre-edit workspace snapshot found only the source/test/document changes listed above, with no removed files. All pre-existing files in `RTD/`, `generate/`, target build outputs, `.mex` and `.cproject` retained their hashes. `git diff --check` found no whitespace errors (Git emitted line-ending conversion notices only).

## S32DS UI build: next permitted step

Perform together with the user; no shell build command is needed.

1. In S32DS, select the project and press **F5** (Refresh). Confirm `board/board_can.c`, `board/board_can.h`, CAN wrapper files and updated `src/main.c` appear.
2. Right-click project > **Build Configurations > Set Active > Debug_FLASH**. Open **Properties > C/C++ Build > Settings > Tool Settings**. Under Standard S32DS C Compiler, confirm include paths contain `${ProjDirPath}/board` and `${ProjDirPath}/drivers/can/inc`. Board is already a source entry; ensure its source is not excluded from this configuration. Do not add `tests/can/fakes`.
3. In compiler **Preprocessor / Defined symbols**, leave `CAN_TEST_PROFILE` absent (default 0) or set it explicitly to `0`. Leave `CAN_TEST_HARDWARE_VERIFIED` absent/default 0. Do not change `.mex` or run configuration generation.
4. For debugger inspection, check compiler **Debugging** and **Optimization** settings. The stored `.cproject` currently specifies no debug information and size optimization, despite the Debug_FLASH name. Through UI, select debug information such as `-g3` and no optimization `-O0` if needed for reliable source breakpoints/struct inspection. This is a user UI action; this implementation did not change project settings.
5. Use **Project > Clean...** for this project (disable any unwanted workspace-wide selection), then **Project > Build Project**. A clean rebuild is appropriate because the public config layout changed. Let S32DS regenerate its managed makefiles; do not edit Debug_FLASH makefiles manually.
6. Inspect Problems and the Build Console. Verify the fresh build compiled `board_can.c`, `can_driver.c` and `main.c`, linked the project ELF and ended with no errors. Share the console summary or first error with the assistant. Until then, build status is pending.

## S32DS debugger procedure: loopback regression plan

These are instructions for a later user-operated session, not evidence that a session ran. Do not power an unresolved connected transceiver/SBC-output arrangement merely to run loopback; use an electrically safe setup. Normal physical communication remains unauthorized.

1. After a successful loopback-profile build, use **Run > Debug Configurations...** and select the existing working S32K144 board launch/probe configuration used for Phase 2. Verify its application points to the freshly built Debug_FLASH ELF, not an old binary. Keep the existing probe/reset settings.
2. Launch Debug, set a code breakpoint at the first executable line inside **`CanTest_HoldResults`** (or use a function breakpoint by that name), reset/restart if needed and Resume. Do not set a data breakpoint on `g_can_test` to find completion.
3. Open **Window > Show View > Expressions** and add `g_can_test`; expand fields at the hold breakpoint. Avoid stepping through polling or repeatedly reading live CAN ESR1.
4. Expect the values below. Record the status, completion flags, received frame and diagnostics. If the breakpoint does not resolve or fields are unavailable, verify debug symbols and ELF selection before changing driver code.

| Field | Expected loopback PASS |
|---|---|
| `magic` | `0x43414E32` (retained Phase 2 marker) |
| `profile`, `mode`, `hardware_verified` | 0, LOOPBACK=0, 0 |
| `board_status`, `board.status` | NOT_RUN=4; no pin setup |
| `status`, `first_failure` | PASS=2, NONE=0 |
| `init_calls`, `start_calls`, `transmit_calls` | 1 each |
| `init_status`, `start_status`, `transmit_status` | OK=0 |
| `tx_accepted`, `tx_completed`, `rx_completed` | 1 each |
| `tx_completion_unconfirmed`, TX/RX errors, TX/RX budget flags | 0 |
| `id_match`, `dlc_match`, `payload_match`, `mismatch_mask` | 1, 1, 1, 0 |
| `request`, `expected`, `received` | ID 0x14, DLC 8, `01 02 03 04 05 06 07 00` |
| `diagnostics.valid`, `.sample_count`, `.tx_pending` | 1, at least 1, 0 |
| `diagnostics.bus_off_seen`, `.driver_state` | 0, RUNNING=2 |
| `exit_code` | 0 |

Poll-call counts and iteration count depend on execution; do not require a fixed count. Raw ESR1 can include normal activity/status bits; do not require ESR1=0. The getter does not reset diagnostics.

## Normal profile preparation and later physical test

**Current permission: software preparation/build only. Do not enable the hardware gate or run a physical CAN test now.** A selected normal profile with gate 0 is designed to stop before board/CAN initialization; if inspected in a later safe debug session it should show FAIL=3, first_failure=11, CONFIG_ERROR=6, board NOT_RUN and init/start/transmit calls all 0. That intentional block is not a driver or hardware failure.

Before any physical run, obtain evidence for MCU I/O voltage versus actual VP230 TX/RX ratings, onboard SBC RX output isolation, correct part/module/RS configuration, wiring/common GND and termination. Confirm the physical board revision against its matching schematic. See the Phase 3 design for source references and unresolved routing details. No software diagnostic establishes those electrical facts.

Only after verification and explicit physical-test authorization:

1. Configure STM32 for normal, non-silent standard Classic CAN at 500 kbit/s. It must receive ID 0x14/DLC 8/request bytes and send exactly one ID 0x14/DLC 8 response with bytes `A1..A8`. Hardware ACK alone is insufficient; do not respond to response frames or arbitrary traffic. Start the peer before the S32K request.
2. In the same S32DS compiler symbol UI, deliberately set `CAN_TEST_PROFILE=1` and `CAN_TEST_HARDWARE_VERIFIED=1`, rebuild and confirm the newly selected executable. These values have not been set by the assistant.
3. Use the established debug launch and hold-results breakpoint. Expect profile=1, mode=NORMAL=1, board_status=OK=0, PORTE gate/present set and PCR4/PCR5 MUX=5. IRQC/PE/PS must be zero; unrelated pad/flag bits may remain set. Optional one-time safe register inspection should show LPB=0, LOM=0, SRXDIS=1 and retained CLKSRC/timing. CAN NVIC enables/MB masks must remain disabled.
4. Resume without stepping through the exchange. PASS requires both TX and RX completed and the STM32 response matching all fields. Verify the peer's receive/response log independently. Record results before making any hardware-success claim.
5. On TIMEOUT/FAIL, retain the complete `g_can_test` object. TX unfinished plus ACK errors suggests missing acknowledgement; TX completed/RX unfinished suggests a missing/mismatching response/filter. These are hypotheses, not proven causes. Check first failure, latest/first error/fault samples and per-direction budget flags. Do not clear flags, resend or cancel pending work for diagnosis.
6. Terminate/reset deliberately before another attempt; the controller may continue retrying after the hold loop. Remove power before wiring changes. Restore profile=0 and hardware gate=0, rebuild via UI and repeat loopback regression after approved changes.

No STM32 firmware, SPI/motor logic, GUI or application protocol was modified. Silicon-mask-specific qualification, measured clock/bitrate, real-time deadlines, RX queues and automated bus-off recovery policy remain outside this phase.
