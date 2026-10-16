# Phase 3 design: physical CAN with the existing polling adapter

Date: 2026-10-15. **Software design approved and implemented; build/runtime verification pending. Physical CAN testing is not authorized.**

## Implementation status and compatibility review

The original approved design below is retained as its design-stage record. Current implementation details and UI steps are in `CAN_Phase3_Implementation_Report.md`; current API/state contracts are in the Phase 3 update to `CAN_Driver_Design.md`.

Actual RTD source confirmed the documented mode/init behavior and all three diagnostics getter signatures. No RTD API contradiction required replacing the architecture. Two explicit integration details were resolved before coding: `CanDriver_PollDiagnostics()` defines the once-per-foreground-round sampling boundary (independent TX/RX polls cannot infer it), and `CAN_TEST_HARDWARE_VERIFIED=0` blocks normal-profile pin/CAN initialization until verification and physical-test authorization. The normal profile is implemented but not selected by default. No generated/vendor edits or build/debug commands were made.

The scope/evidence and proposal text below describe the original design stage, not new validation claims. Physical loopback PASS remains the user's Phase 2 result; the changed Phase 3 software has not yet run. Confirmed wiring does not establish VP230 electrical compatibility, onboard RX isolation or termination.

## Scope and evidence

Only this report is added. No source, generated configuration, vendor source, project settings or build outputs are intentionally changed. No build, flash or debug command was run. All future build/debug steps are performed by the user through S32DS UI.

The user reports that Phase 2 passed internal loopback on the physical S32K144 board, including independent TX/RX completion and matching ID, DLC and payload. This supersedes the earlier reports' hardware-unverified status for **internal loopback only**. External pins, transceiver, bus timing and two-node communication remain unverified.

Reviewed: CAN_Project_Analysis.md, CAN_Hardware_Analysis.md, CAN_Driver_Design.md, CAN_Driver_Phase1_Report.md, CAN_Phase2_Loopback_Report.md, current main/wrapper, generated FlexCAN/clock inputs, project settings and actual project-local RTD implementation. References below use current pre-implementation line numbers; repository paths are relative to this report.

## Current implementation and required changes

| Verified evidence | Current behavior | Phase 3 proposal |
|---|---|---|
| `drivers/can/inc/can_types.h:16` | Config contains only `rx_id` | Append a public mode enum field; keep RTD types private |
| `drivers/can/src/can_driver.c:66`, `:70`, `:108` | Validates generated loopback and passes original Config0 into RTD | Retain validation of the generated baseline; pass a private copy with explicitly selected mode |
| `generate/src/FlexCAN_Ip_Sa_BOARD_InitPeripherals_PBcfg.c:114`, `:138` | Generated Config0 selects loopback | Keep generated Config0 unchanged |
| `FlexCAN_Example_S32K144.mex:259` | Generator input selects `FLEXCAN_LOOPBACK_MODE` | Keep generator input unchanged |
| `drivers/can/src/can_driver.c:51`, `:116`, `:132`, `:168` | CAN0 NVIC lines disabled; RTD interrupts disabled; polling requested | Preserve all these safeguards |
| `drivers/can/src/can_driver.c:212`, `:241` | Foreground services RTD TX and RX | Preserve both service calls and RX re-registration |
| `src/main.c:162`, `:292`, `:304` | One-shot loopback test, hold-results function, existing Clock RTD initialization | Add explicit test profile and peer-response expectation; keep clock sequence and result hold |
| `generate/src/Clock_Ip_Cfg.c:776`, `:958` | FLEXCAN0 and PORTE gates configured enabled | Reuse Clock RTD; verify actual gates before board pin access |
| `FlexCAN_Example_S32K144.mex:19-36`, `generate/include/modules.h:200` | Pins have no configured entries, pin generation disabled, Port module off | Add narrowly scoped board pin initialization rather than regenerate platform configuration |
| `.cproject:43`, `:180` | Existing board include path and source entry | Put proposed board files there; verify inclusion in the user's selected build configuration |

### Exact RTD mode behavior

`RTD/include/FlexCAN_Ip_Types.h:164-169` defines NORMAL, LISTEN_ONLY and LOOPBACK modes; `:460` supplies `Flexcan_Ip_ConfigType.flexcanMode`. The wrapper will expose only LOOPBACK and NORMAL.

`RTD/src/FlexCAN_Ip.c:382-384` disables self reception for non-loopback initialization. Init applies the configured mode at `:1914`, after controller/bitrate initialization. `RTD/src/FlexCAN_Ip_HwAccess.c:1640-1655` implements:

| Effective init mode | CTRL1.LPB | CTRL1.LOM | MCR.SRXDIS | Meaning |
|---|---:|---:|---:|---|
| LOOPBACK | 1 | 0 | 0 | Internally receive own transmission |
| NORMAL | 0 | 0 | 1 | Physical bus participation; own TX is not the expected RX |

These are RTD effects, not new wrapper register writes. There is no need to call the private `FlexCAN_SetOperationMode` helper. `FlexCAN_Ip_SetStartMode` starts the configured controller; it does not select normal versus loopback.

## Proposed configuration contract

Add to public `can_types.h`:

```c
typedef enum {
    CAN_MODE_LOOPBACK = 0,
    CAN_MODE_NORMAL = 1
} Can_Mode;

typedef struct {
    uint16_t rx_id;
    Can_Mode mode;
} Can_Config;
```

Update `CAN_CONFIG_DEFAULT` to explicitly initialize ID 0x14 and LOOPBACK. Update callers/tests to use explicit designated members. Zero remains loopback, so an old aggregate `{0x14}` still initializes loopback, but a full rebuild is required after the structure change. Reject unsupported enum values before touching hardware. Preserve existing identifier/DLC/pointer/lifecycle validation.

Add private, static `Flexcan_Ip_ConfigType s_rtd_config` in `can_driver.c`. After validating Config0 and the public request, copy the complete `FlexCAN_Config0`, then change **only** `s_rtd_config.flexcanMode` to the mapped RTD enum. Pass `&s_rtd_config` and the existing `&FlexCAN_State0` to `FlexCAN_Ip_Init`. Static lifetime avoids configuration-pointer lifetime ambiguity. The generated object remains the reproducible loopback template. All other copied fields remain unchanged.

Keep the existing baseline assertion that the generated template itself is loopback. Do not change that assertion to reject an explicitly requested normal-mode private copy. Validate the template first; mode selection belongs to the copy afterward. There is one adapter and one RTD state, not separate normal/loopback drivers.

Select the application test profile explicitly at compile time, defaulting to LOOPBACK. Proposed values: `CAN_TEST_PROFILE_LOOPBACK` and `CAN_TEST_PROFILE_NORMAL_TWO_NODE`. Reject other values at compile time. Store the selected profile and requested mode in `g_can_test`. Never choose normal mode from pin detection, debugger memory edits or an implicit generated change.

Mode is fixed for one initialization lifecycle. No hot switching or direct CTRL1 writes. A deliberate future change requires completed/consumed TX, safe Stop/Deinit and a fresh Init; Phase 3 test runs use reset/relaunch instead. Do not initialize CAN0 outside the wrapper.

### Generated configuration changes identified before implementation

**Recommended path: zero edits to `generate/`, RTD source, clock inputs or `.mex`.** The private RTD copy is the only effective FlexCAN configuration difference: `.flexcanMode = FLEXCAN_NORMAL_MODE` in the explicitly selected normal profile.

Retain Config0 max MBs=16 (`:116`), FIFO off (`:120`), payload allocation eight bytes (`:146`), FD off (`:149`), PE clock selected (`:152`), enhanced timing off (`:155`), BRS off (`:157`), raw timing 7/4/1/5/1 (`:160-168`), null callbacks (`:188-190`) and existing ctrlOptions (`:141`). FIFO transfer enum `FLEXCAN_RXFIFO_USING_INTERRUPTS` at `:183` is inactive with FIFO disabled; it does not require enabling CAN interrupts for MB polling.

If a generated-normal configuration is later mandated, the precise alternative is to change `.mex:259` through the S32DS configuration UI and regenerate Config0's mode at PBcfg `:138`, or generate a second named configuration. That is **not this proposal**, requires separate review of generated diffs and baseline checks, and must retain a saved/generated loopback configuration. Do not hand-edit generated output alone.

### Timing and clocks

Preserve the entire existing `main.c` Clock RTD initialization/PLL conditional sequence. CAN0 host access depends on its PCC gate and system clock; the configured PE selection uses SYS_CLK rather than the alternate SOSCDIV2 source. PORTE PCR access additionally requires its gate. No new oscillator, clock source or divider changes are proposed.

With actual PE=48 MHz and the retained raw fields:

```text
prescaler = 5 + 1 = 6
TQ/bit = 1 + (7 + 1) + (4 + 1) + (1 + 1) = 16
bitrate = 48 MHz / (6 * 16) = 500 kbit/s
sample point = (1 + 8 + 5) / 16 = 87.5%; RJW = 2 TQ
```

This is a derivation, not a frequency measurement. Loopback PASS does not establish physical bitrate accuracy or oscillator tolerance. Confirm the active clock tree in S32DS and, where available, measure approximately 2 microseconds per nominal bit on a suitable frame. STM32 must independently use 500 kbit/s with compatible timing and oscillator accuracy.

## Board pins and external transceiver

NXP's [AN5413 cookbook, FlexCAN pin initialization](https://www.nxp.com/docs/en/application-note/AN5413.pdf), printed pp. 50/61, explicitly maps PTE4 ALT5 to CAN0_RX and PTE5 ALT5 to CAN0_TX. This verifies the peripheral mux assignment; it does not verify the user's connector or wiring.

### Pin implementation choice

The installed Port RTD package provides `Port_Ci_Port_Ip_Init` and `Port_Ci_Port_Ip_SetMuxModeSel`, verified in its `include/Port_Ci_Port_Ip.h` and source. However, this project has no copied Port implementation or generated pin configuration. Importing that component requires configuration headers, sources, memory-map/build integration and regeneration review.

**Recommend minimal board-specific setup** in new `board/board_can.h` and `board/board_can.c`, called from main after clocks and before CAN Init/Start, only for the normal profile. Proposed API: `BoardCan_Status BoardCan_InitPins(void)` with explicit OK, CLOCK_DISABLED, PIN_LOCKED and VERIFY_FAILED results. No board types leak into CAN public headers; the wrapper remains unaware of pins/transceiver/SPI.

Implementation to review later:

1. Verify PORTE clock is enabled by the existing Clock RTD configuration. Return failure before PCR access if not; do not silently reconfigure clocks.
2. Check PCR4/PCR5 lock state and reject an incompatible locked configuration.
3. Set PCR4 and PCR5 MUX fields to 5 using masked replacement, not OR-only programming. Disable pin interrupt request configuration (`IRQC=0`) and internal pulls (`PE=0`, deterministic `PS=0`). Preserve unrelated pad settings. Handle the write-one-to-clear ISF bit deliberately rather than writing a read-back 1 accidentally. No GPIO PDDR programming is needed for peripheral mux mode.
4. Read back the programmed fields. Store board status, PORTE gate and PCR4/PCR5 snapshots in debugger-visible application results. Leave other pins and all peripheral clock sources unchanged.

These are board PORT register accesses only. CAN configuration and transfers remain entirely RTD-owned. Loopback profile skips board setup, preserving its existing regression path. Do not enable external CAN pins until the electrical prerequisites below have been checked.

### Hardware prerequisites and unresolved facts

| Item | Evidence/status | Required resolution before physical test |
|---|---|---|
| Board revision | User identifies Rev C | Match physical assembly/revision to its schematic; do not substitute Rev B/C1 instructions |
| Onboard SBC isolation | Project does not initialize it; this proves no electrical isolation | Confirm onboard RX output cannot drive PTE4 alongside external RXD; verify actual routing/population before any rework |
| Board routing evidence | NXP-hosted [SPF-29248_C schematic](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/S32K/8896/1/SPF-29248_C.pdf), sheets 3/5, lists SBC_CAN_TX/RX, PTE4/PTE5 and zero-ohm R136/R137 | Treat R136/R137 as candidate links for tracing, not approved removal instructions; confirm which link drives RX on the physical assembly. The PDF has a Rev C cover and a differing MCU-sheet revision caption |
| MCU I/O supply | Not established by project code or transceiver supply | Measure VDD_MCU; verify supply-selection jumper against matching board documentation before changing it |
| RX logic compatibility | [S32K1xx datasheet, Table 19](https://www.nxp.com/docs/en/data-sheet/S32K1xx.pdf?pspll=1) gives VIH minimum 0.65 x VDD in the 5 V range | At 5 V, 3.25 V is required; a nominal 3.3 V receiver output is not sufficient evidence of guaranteed compatibility |
| Exact transceiver | User calls module VP230; exact device/module schematic not supplied | Confirm part marking and board circuit; do not assume every similarly named module has identical behavior |
| Electrical path | User reports common GND, CAN_H/L and correct termination | Confirm wiring/continuity and approximately 60 ohms CAN_H-to-CAN_L with power removed for two 120-ohm endpoints; include module termination in the total |
| STM32 | Node details/configuration not provided | Confirm normal, non-silent Classic CAN mode, matching bitrate, receive filter and one-response application behavior |

For a genuine TI SN65HVD230, the [TI datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) identifies D/TXD pin 1, R/RXD pin 4, CANL pin 6, CANH pin 7 and RS pin 8. RS low selects high-speed operation; high selects standby with receiver active. Verify the module's RS wiring, supply/decoupling and TX input voltage ratings. Its specified RX high output minimum of 2.4 V does not guarantee the S32K input threshold at 5 V. Resolve supply compatibility or use a suitable level-interface/transceiver arrangement before testing; software mux changes cannot fix this. A 3.3 V MCU supply option must be reviewed against the complete board power/debug arrangement, not changed blindly.

No SBC SPI initialization is proposed. Ignoring SBC software does not establish a high-impedance onboard RX connection. Do not attach the external receiver to a net driven by another receiver. No hardware modification is performed by this design.

## Polling, lifetime and diagnostics contracts

Retain TX MB0 and RX MB1, Classic standard data frames, stable aligned TX storage, static RX storage, busy guard and explicit RX re-registration. Exact available interfaces remain:

| Adapter operation | Existing RTD mapping and evidence |
|---|---|
| Init | `FlexCAN_Ip_Init`, then `FlexCAN_Ip_DisableInterrupts`; wrapper `:108-116` |
| Start/filter/register | `FlexCAN_Ip_SetStartMode`, `FlexCAN_Ip_ConfigRxMb`, `FlexCAN_Ip_Receive(..., TRUE)`; wrapper `:125-157` |
| Submit | `FlexCAN_Ip_Send(instance, mb_idx, tx_info, msg_id, mb_data)`; `RTD/include/FlexCAN_Ip.h:226`; TX info `is_polling=TRUE` bypasses transfer IRQ enabling in `RTD/src/FlexCAN_Ip.c:1997` |
| TX poll | `FlexCAN_Ip_MainFunctionWrite` (`RTD/src/FlexCAN_Ip.c:3615`) then `FlexCAN_Ip_GetTransferStatus` (`:2546`); the latter alone does not service completion |
| RX poll | `FlexCAN_Ip_MainFunctionRead` (`:3513`), status, validate/copy frame, then `FlexCAN_Ip_Receive(..., TRUE)` to rearm |
| Diagnostics | Actual public APIs `FlexCAN_Ip_GetErrorStatus`, `FlexCAN_Ip_GetControllerTxErrorCounter`, `FlexCAN_Ip_GetControllerRxErrorCounter`; header `:578`, `:589`, `:600`; implementation `:2850`, `:2870`, `:2890` |

Do not invent polling-completion APIs, install handlers, enable CAN NVIC lines or modify RTD internals. Error flags can be polled without enabling error interrupts. Retain DMA-off build guard and null callbacks.

### Proposed wrapper change and justification, before any implementation

The current wrapper cannot select NORMAL and has no application-facing bus diagnostics. These are the two justified additions to `can_driver.c`; its transfer sequencing does not require replacement. No implementation is made in this phase.

Propose `CanDriver_GetDiagnostics(Can_Diagnostics *out)` returning `Can_Status`, with a public standard-C structure containing mode, driver state, TX pending, diagnostics-valid flag, latest raw ESR1, first error snapshot, accumulated selected error flags, TX/RX error counters and decoded fault state. RTD/device types and masks remain private. Add sample count and first-error-valid so zero is distinguishable from unsampled data.

Capture diagnostics centrally inside the wrapper once per foreground polling cycle, including BUSY/NO_DATA paths, plus before publishing a transfer fault. Getter copies cached data and does not reread hardware. Failed Init must not invoke RTD transfer/state APIs; mark unavailable or retain the last valid snapshot. Keep cached diagnostics readable after FAULT until an explicitly successful reinitialization/reset.

ESR1 contains event, current-state and write-one-to-clear fields; selected protocol-error fields describe events since the previous register read (installed S32K RM Rev 12.1, section 55.4.2.9, pp. 1828-1831). Preserve a latest complete snapshot separately from an accumulated **error-event-only** mask; never OR current FLTCONF state encodings together. Do not add automatic error clearing. Avoid live debugger register watches that repeatedly read ESR1 and consume diagnostic evidence. Sample ECR through the RTD APIs; ESR1/ECR observations are sequential, not atomic.

Decode ACK errors, bit/form/stuff/CRC errors, warning and fault confinement for diagnosis. Bus-off is terminal FAIL for this one-shot test. Do not report every transient bus error as an unrecoverable driver transfer failure; retain it as evidence while polling continues unless bus-off or a real API/state failure occurs. Keep generated bus-off recovery policy unchanged and record that the controller may recover/retry independently of the stopped application loop.

Normal-mode TX may remain pending when no peer acknowledges. The application iteration budget is not a CAN transmit cancellation mechanism or calibrated milliseconds. Preserve pending TX storage/ownership on TIMEOUT/FAIL; do not Stop, Deinit, overwrite, resubmit or clear completion flags automatically. Controller retries may continue after entering the result-hold loop. The UI test procedure must deliberately reset/terminate and remove power when changing wiring.

## Two-node test application proposal

Keep test logic in `src/main.c`, outside the CAN adapter. No protocol/motor/SPI/GUI additions.

| Profile | S32K transmit once | Expected S32K receive |
|---|---|---|
| LOOPBACK (default) | Standard ID 0x14, DLC 8, `01 02 03 04 05 06 07 00` | Same frame through internal loopback |
| NORMAL_TWO_NODE (explicit) | Standard ID 0x14, DLC 8, `01 02 03 04 05 06 07 00` | STM32 response: standard ID 0x14, DLC 8, `A1 A2 A3 A4 A5 A6 A7 A8` |

Distinct response bytes are a proposed test convention requiring STM32 agreement, not an existing protocol. STM32 receives only the matching request and sends one response per request; it must not reply to its own response or arbitrary ID 0x14 traffic. Automatic hardware ACK does not execute this response application.

Expected sequence:

1. Reset results, record profile, run existing clocks, then normal-profile board pin setup. On board failure, record it and hold without CAN initialization.
2. `CanDriver_Init({.rx_id=0x14, .mode=selected_mode})`, then Start. Start registers RX before the request is sent. Have STM32 running and idle before S32K starts; startup is not a lossless receive window.
3. Transmit the request once. OK means accepted only. Record request and expected response separately in `g_can_test`.
4. Foreground services TX and RX independently, plus cached bus diagnostics, within `CAN_TEST_POLL_BUDGET` iterations. An RX response can arrive before software observes TX completion. RX completion never substitutes for TX completion.
5. Compare received ID, DLC and all eight bytes. Preserve existing strict mismatch/overrun failure behavior for this controlled test. Poll frequently: one RX MB with immediate rearm is not a queue or lossless continuous receiver.
6. PASS requires confirmed TX completion and a matching received frame, with no recorded test failure. API/state failure, bus-off, mismatch or overrun gives FAIL. Exhausted budget with unfinished work gives TIMEOUT, setting independent TX/RX budget flags.
7. Preserve the first failure, both completion/error results, diagnostics and any valid received frame. Enter `CanTest_HoldResults`; perform no implicit cleanup or recovery.

Retain NOT_STARTED, RUNNING, PASS, FAIL and TIMEOUT and their existing numeric values (0..4). Add BOARD stage/failure without renumbering existing values, and preserve existing status fields. Record selected mode/profile, board status/PCR snapshots, request, expected response, cached CAN diagnostics and first error snapshot. Do not label an iteration budget as milliseconds; RTD lifecycle timeouts still use the existing OSIF dummy counter, not a calibrated real-time deadline.

## Exact proposed file changes after review

| File | Proposed edit |
|---|---|
| `drivers/can/inc/can_types.h` | Add `Can_Mode`, append config mode, define public plain-C diagnostics |
| `drivers/can/inc/can_cfg.h` | Explicit loopback default; retain fixed instance/MBs/timing constants |
| `drivers/can/inc/can_driver.h` | Document init-time mode and add cached diagnostics getter contract |
| `drivers/can/src/can_driver.c` | Validate requested mode, copy generated config, map only mode, add RTD diagnostics capture/cache; preserve polling/lifetimes/IRQ safeguards |
| `board/board_can.h`, `board/board_can.c` (new) | Two-pin setup/status, independent of CAN transfer implementation |
| `src/main.c` | Explicit default-loopback profile, conditional board setup, separate request/response expectations, diagnostics/results; retain clocks and one accepted TX |
| `tests/can/` | Adapt config initializers/fakes; cover mode mapping/default, invalid mode, diagnostics and preserved BUSY ownership where useful |
| `CAN_Driver_Design.md` | Update init/config/diagnostics contracts after approved implementation |
| Phase 3 implementation report (new) | Record actual changes, UI build evidence and subsequent hardware results |
| `.cproject` | No planned edit: board source/include entries already exist; correct through UI only if active configuration proves otherwise |
| `RTD/`, `generate/`, `.mex`, SPI/motor/GUI/protocol | No changes |

## User-operated S32DS validation procedure after implementation

No Phase 3 build or target test has occurred. Carry out these steps together through S32DS UI after design approval and implementation.

1. Save/import the changes, select the intended build configuration, verify board source and CAN include paths, then use **Project > Build Project**. Share the first errors or successful build summary; check that the new board source compiled and the correct executable was selected. Do not regenerate FlexCAN/pins for the recommended design.
2. Select LOOPBACK explicitly (also the default), rebuild through UI, launch the existing board debug configuration, set a code breakpoint inside `CanTest_HoldResults`, reset/restart and Resume. `g_can_test` is a variable to inspect in Expressions, not the code breakpoint location.
3. At the hold breakpoint expect PASS=2, mode LOOPBACK, TX accepted/completed=1, RX completed=1, ID/DLC/payload matches=1, mismatch mask=0 and no TX/RX budget exhaustion. Record evidence for regression.
4. Resolve board isolation, VDD/logic levels, transceiver identity/RS and wiring first, with power removed for wiring/termination changes. Configure STM32 with the request/response contract and start it before S32K. Do not proceed solely because loopback passed.
5. Explicitly select NORMAL_TWO_NODE, rebuild via UI and relaunch/reset; verify `g_can_test` reports NORMAL. Inspect board-init success and PCR MUX=5. Read controller mode once at a safe halt if needed: LPB=0, LOM=0, SRXDIS=1; confirm CLKSRC=1 and retained timing. CAN0 MB interrupt masks and NVIC enables should remain disabled.
6. Resume without stepping through the time-sensitive transaction. At `CanTest_HoldResults`, expect PASS=2, one accepted request, TX/RX completed=1, expected response `A1..A8`, matches=1, mismatch=0, no budget exhaustion and retained diagnostics. Verify STM32 independently logged the request and transmitted its response. This is the first point at which physical success can be reported.
7. If TIMEOUT, record the complete result before resetting. TX incomplete plus ACK errors suggests missing acknowledgement; check peer mode, clock/bitrate, wiring, transceiver and termination. TX complete/RX incomplete suggests no matching peer application response/filtering; ACK alone does not prove the intended application received it. Treat these as diagnostic hypotheses, not proven causes.
8. If FAIL, retain first failure/API status, frame/mismatch fields, first/latest ESR1, counters and board snapshots. Avoid live peripheral reads of ESR1. Debugger halts can alter timing and controller behavior; disconnect/reconnect or retry only after a deliberate reset. Do not deliberately disable the peer mid-transfer as an initial test.
9. Return to LOOPBACK, rebuild and repeat the regression after any approved pin/mode change. Save the selected profile, build log and debug observations with each result.

## Review outcome required

Review the private-copy mode selection, minimal board module, diagnostics API and distinct STM32 reply convention before implementation. Hardware release remains conditional on confirming onboard RX isolation and compatible I/O levels. No physical CAN success, measured bitrate, new build success or silicon-mask-specific validation is claimed by this report; retain vendor RTD workarounds and resolve any mask-specific errata against the actual fitted MCU before broader deployment.
