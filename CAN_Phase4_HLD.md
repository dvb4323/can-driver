# Phase 4 HLD — CAN protocol and motor application

Date: 2026-10-19. **Design only; no implementation authorized by this document.** Normative proposed wire layout: `CAN_Protocol_Specification_Draft.md`. Requirements/stages/tests: `CAN_Phase4_Implementation_Plan.md`.

## 1. Baseline and evidence boundaries

The user reports successful physical CAN TX/RX with an ESP32-S3 test board in Phase 3. Preserve that working baseline. It does not establish SPI/PWM/motor operation, protection settings, sensors, multiple-RX-MB behavior or bounded scheduler latency. No new build/debug/hardware test was performed during this design task.

The current `src/main.c` is a **compact polling smoke test**, not the longer historical profile-based ESP32/LED application described in previous reports. It defines `CAN_TEST_LOOPBACK` default 0, transmits 0x123, accepts 0x321, increments the last payload byte, publishes `g_can`, waits for TX completion while polling RX, and uses `DELAY_LOOPS=400000`. Header wording about approximately 100 ms is not a calibrated timing guarantee. The current application has no motor state machine or CAN protocol parser.

Earlier Phase 1/2/3 documents were reviewed as historical design/build evidence. Their statements that physical communication is unverified or not authorized are superseded by the user's current verification. Their claims about prior `main.c`, `g_can_test`, `g_can_app`, profile symbols and LED/SysTick setup must not be mistaken for current code. The original register-driver proposal was superseded by the approved RTD adapter; Phase 4 continues that adapter.

### File-by-file analysis

All source line references below are to the inspected checkout before Phase 4 implementation. Paths are relative to the project root. No file in this table was modified.

| File / evidence | Verified finding | Design recommendation |
|---|---|---|
| `src/main.c:19-26` | Normal by default; 0x123/0x321; rough loop delay | Preserve as regression fixture before a later approved thin-main integration; never reuse loop counts as a motor watchdog clock |
| `src/main.c:58-99` | Clock RTD init/PLL wait, CAN pins, wrapper Init/Start | Reuse clock/platform ownership; motor safe inhibit must precede waits and peripheral init |
| `src/main.c:79-83` | Application enables PORTE gate if board setup reports clock disabled | This differs from older no-clock-write board design. Preserve now; reconcile board/clock ownership during approved integration, not silently in this design |
| `src/main.c:101-125` | TX request → `do/while BUSY`; RX/diagnostics serviced during TX wait | Works as smoke test but future motor/time services would be starved by this wait; replace wait with one service step per event-loop turn |
| `src/main.c:128-135` | RX repeatedly polled during gap; no motor/time service or actual time reference | Replace gap with scheduled deadlines; error statuses/overrun must be handled explicitly, not counted as ordinary control frames |
| `src/main.c:42-55` | `g_can` diagnostics; Fail loops forever | Preserve visibility; a motor application must inhibit drive before a fault hold, and ideally continue CAN/time/error services |
| `drivers/can/inc/can_types.h` | Plain C frames, one `rx_id`, mode, status/state enums and cached diagnostics | Keep RTD types private; add proposed multi-channel types only after review |
| `drivers/can/inc/can_cfg.h` | CAN0/MB0/MB1, max ID/DLC, descriptive 48 MHz/500k constants | Retain default one-RX regression and fixed generated timing; proposed multi-RX mapping stays private |
| `drivers/can/inc/can_driver.h` | Singleton lifecycle, nonblocking submit, foreground polls, cached getters | Remains the sole CAN interface used by application/transport; no direct application RTD calls |
| `drivers/can/src/can_driver.c:117-203` | Disables CAN0 NVIC sources; validates generated loopback baseline; static private mode copy; original State0 | Preserve ownership, validation, IRQ suppression and mode mapping |
| `drivers/can/src/can_driver.c:207-242` | Start enables module, configures MB1 exact ID, registers static RX destination with polling TRUE | Extend this existing backend to three independent RX slots; do not introduce a second driver/controller owner |
| `drivers/can/src/can_driver.c:244-307` | Aligned static TX copy; one pending request; MainFunctionWrite then software status | Keep TX MB0 and busy ownership; application adds queues above it, not by overwriting pending MB0 |
| `drivers/can/src/can_driver.c:309-372` | RX service/status, metadata validation, CODE full/overrun decoding, copy then re-register | Generalize per slot; overrun is valid latest data plus evidence of lost earlier frames, not lossless delivery |
| `drivers/can/src/can_driver.c:375-461` | Stop rejects pending TX; Deinit explicit discard; diagnostic capture/getter | Motor STOP must not call CAN Stop. Preserve pending TX and diagnostics during motor safety action |
| `board/board_can.c:12-67`, `.h` | Checks PORTE gate/locks; ALT5 PCR4/5; cache-only board results | Keep CAN pin setup independent of motor pins/driver |
| `generate/src/FlexCAN_Ip_Sa_BOARD_InitPeripherals_PBcfg.c:114-190` | 16 configured MBs, FIFO/FD/BRS off, loopback template, PE selected, timing 7/4/1/5/1, callbacks null | Three RX MBs fit without changing this object; preserve generated State0/Config0 |
| `generate/include/FlexCAN_Ip_Cfg.h:139,142,160` | RTD lifecycle uses OSIF_COUNTER_DUMMY; compile-time MB array maximum 32; DMA off | 32 compiled slots do not mean 32 configured MBs. Do not interpret dummy timeouts as calibrated milliseconds |
| `generate/src/Clock_Ip_Cfg.c:227`, `:776`, `:853-867`, `:958` | FIRC RUN selection, CAN0/PORTE gates and LPSPI gates represented | Enabled LPSPI clock gates are not a configured SPI driver or motor interface |
| `generate/include/modules.h:205,215` | PWM and SPI module flags STD_OFF | Motor backend integration is missing, not something to infer from RTD clock entries |
| `generate/src/IntCtrl_Ip_Cfg.c:89-91,185-187` | LPSPI IRQ entries disabled/default handlers | Not evidence of a usable SPI API or motor control path |
| `.cproject:60-61` | Historical `CAN_TEST_PROFILE=2`, hardware-verified symbol remain in build settings | Current smoke main uses `CAN_TEST_LOOPBACK` instead. Record this mismatch; do not alter project settings without approved integration |
| `.cproject:180-188` | Existing source entries cover board, RTD, CAN driver, generated files and src | Proposed new app/protocol/motor/time source directories require later explicit UI build integration |
| `tests/can/`, `tests/can_phase2/` | Existing wrapper/fake/application tests refer to older mains | Preserve driver tests; assess source compatibility before rerunning app harnesses. Their previous PASS is not proof of current-main compatibility |
| `CAN_Project_Analysis.md`, `CAN_Hardware_Analysis.md` | Original register-level analysis; later hardware report resolves CLKSRC survival | Retain hardware/RTD evidence, not the superseded register-driver architecture |
| `CAN_Driver_Design.md`, `CAN_Driver_Phase1_Report.md` | RTD adapter contracts and historical validation | Reuse ownership/lifetime semantics; update only in an approved implementation phase |
| `CAN_Phase2_Loopback_Report.md` | Historical one-shot polling integration/test contract | Preserve its regression vectors separately from a motor application's behavior |
| `CAN_Phase3_Design.md`, `CAN_Phase3_Implementation_Report.md` | Private-copy normal mode, pin and diagnostic design | Reuse architecture; latest hardware verification is user-reported |
| `CAN_Phase3_Manual_GUI_Report.md`, `CAN_ESP32_Continuous_Report.md` | Historical manual/continuous test variants | Keep as history; current source and current task are authoritative |
| `binh_can.zip`, `drivers.zip` | Read archive inventories only: CAN/RTD/startup/board/main content; no motor/SPI headers | Archives are reference/export copies, not duplicate sources to build or evidence of a hidden motor implementation |
| `include/check_example.h` | Example support, no motor abstraction | Do not repurpose as the Motor Interface |

### Available motor/SPI integration interface conclusion

No callable motor, SPI or PWM application-facing API was found in `src/`, `include/`, `board/`, `drivers/` or the supplied archives. No motor-specific generated configuration was found. There is no verified existing function name, pin map, sensor scale or SPI transaction format to wire into Phase 4. Do not invent them or modify SPI-team code.

The [NXP MC33HB2000 datasheet](https://www.nxp.com/docs/en/data-sheet/MC33HB2000.pdf) is a future review input if that exact device is actually fitted. A component datasheet is not proof of the project's board wiring, sensing, dissipation or control topology. This HLD assigns no physical current/voltage/temperature/PWM limits and does not claim that SPI provides measured rpm, continuous current samples, mechanical stopping or every proposed capability.

## 2. Layering and ownership

```text
Python views / controller / protocol session
  -> backend-neutral CAN frames -> PCAN-Basic / ESP32 adapter / mock transport
  -> physical CAN
S32K application coordinator (one foreground owner)
  -> CAN transport queues -> existing custom CAN wrapper -> unchanged RTD
  -> pure protocol codec / transaction manager / command admission
  -> motor state machine -> hardware-independent Motor Interface
                            -> mock initially
                            -> later SPI/PWM backend owned with SPI team
  -> monotonic time / deadline service
  -> diagnostic snapshots
Board/platform owns clocks, CAN mux and future approved safe-inhibit pins.
```

Application chooses behavior; protocol validates/encodes wire data; CAN wrapper owns all CAN RTD state and MBs; motor backend owns motor hardware and SPI/PWM details; time service owns one reviewed timer. No dependency from SPI driver to CAN driver or GUI. No CAN ID decoding inside the motor backend, and no RTD types in motor/protocol/public headers. One component initializes CAN0 exactly once. Motor STOP is a motor operation, not CAN controller Stop/Deinit.

## 3. Actual RTD multiple RX support

### Verified source behavior

| Actual file/line | Behavior and implication |
|---|---|
| `RTD/include/FlexCAN_Ip_Types.h:362-378` | State contains an MB handle array. Each MB has its own state, destination pointer and isPolling flag; simultaneous independent registrations are supported by the implementation |
| `RTD/src/FlexCAN_Ip.c:975-1015` | Receive registration indexes `state->mbs[mb_idx]`; IDLE → RX_BUSY, stores caller buffer pointer, records polling. A second registration on the same busy MB returns BUSY |
| `RTD/src/FlexCAN_Ip.c:2021-2070` | ConfigRxMb accepts a separate mb_idx/ID and writes NOT_USED → INACTIVE → EMPTY; clears old MB completion flag. Do not call it per received frame or on an active slot, which could discard pending data |
| `RTD/src/FlexCAN_Ip.c:2077-2102` | Receive(..., TRUE) registers polling; IRQ enabling is conditional on FALSE. It does not copy a caller stack buffer for future use |
| `RTD/src/FlexCAN_Ip.c:3513-3567` | MainFunctionRead(instance, mb_idx) checks that MB's flag and RX_BUSY state, then calls internal RX service synchronously. Must call for each MB; it does not scan all MBs |
| `RTD/src/FlexCAN_Ip.c:1258-1360` | Internal RX service locks, copies raw MB into persistent destination, clears flag/unlocks, then sets that MB software IDLE. It preserves vendor workarounds. An internal function named IRQHandler can be called by polling without NVIC interrupts |
| `RTD/src/FlexCAN_Ip_HwAccess.c:1340` | Copies CS/ID/data/timestamp. CODE full versus overrun remains metadata; GetTransferStatus success alone does not certify no RX loss |
| `RTD/src/FlexCAN_Ip.c:2546` | GetTransferStatus checks software state; not a completion-service replacement |
| `RTD/src/FlexCAN_Ip_HwAccess.c:746-750` | RTD init programs all-ones global/special RX masks |
| `RTD/include/FlexCAN_Ip.h:409,423,623` | Public macros exist for SetRxIndividualMask, SetRxMbGlobalMask and SetRxMaskType; these are callable RTD interfaces, not invented wrapper functions |
| `RTD/src/FlexCAN_Ip.c:3290,3365,3750` | Mask APIs require enabled/frozen configuration state (checked with DET); may temporarily enable a disabled module but do not automatically make an active controller frozen. Check return values |
| `RTD/include/FlexCAN_Ip_HwAccess.h:1427,1464` | Individual mask writes RXIMR; selecting individual mode changes IRMQ. Cleared RXIMR RAM alone is not an exact-ID filter configuration |
| `RTD/include/FlexCAN_Ip.h:481`, `RTD/src/FlexCAN_Ip.c:3342,3695` | Public EnterFreezeMode and SetStartMode available; SetStartMode enables and exits freeze |
| `RTD/src/FlexCAN_Ip.c:3615` | TX polling remains MainFunctionWrite for MB0 followed by status; changing number of RX slots does not replace this service |

**Conclusion:** actual RTD can support three independent polled RX MBs, but the current wrapper only exposes one. This is a source-backed feasibility finding, not runtime verification of a multi-MB configuration.

### Proposed mapping, filtering and lifecycle

TX MB0 remains unchanged. RX MB1=Command 0x100, MB2=Config 0x110, MB3=Apply 0x111. MB4..15 unused; do not use special global-mask MB14/15 or enable FIFO. Generated max_num_mb=16 is sufficient. Do not allocate duplicate exact filters and assume they form an ordered queue.

Prefer **GLOBAL mask mode with exact all-ones global mask**, matching current init behavior: each MB still contains its own ID, so a common comparison mask does not force a common accepted ID. Apply the mask via available RTD calls in confirmed freeze state. Also validate returned IDE/RTR/EDL/ID/DLC in software. No direct CAN register writes in application or a new register-level CAN implementation.

Individual masks remain an available alternative if later ranges are needed: program each allocated RXIMR deliberately before selecting/starting individual mode; 0 means don't-care bits and can admit unintended IDs. Avoid broad filters for Phase 4. The mask API consumes the raw hardware-format 32-bit mask, not an unshifted 11-bit ID; use only verified private conversions if masks are generalized.

Proposed public extension names below are **new wrapper designs, not existing RTD APIs**:

- `CanDriver_InitMultiRx(const Can_MultiRxConfig *config)` with a fixed maximum of three plain-C `{channel, standard_id}` entries and mode. Validate duplicates, IDs, counts and reserved mappings before hardware changes.
- `CanDriver_PollRxChannel(Can_RxChannel channel, Can_Frame *out)` returns existing OK/NO_DATA/INVALID_FRAME/RX_OVERRUN semantics per slot. Channel is a logical enum, not a public RTD MB number.
- Keep existing `CanDriver_Init`, `Start`, `Transmit`, `PollTx`, `PollRx`, diagnostics and default config working for one-RX regressions. Legacy PollRx selects logical channel 0. No protocol IDs hardcoded inside reusable driver logic; mapping/config belongs above it, physical MB selection stays private.

Use one static `Flexcan_Ip_MsgBuffType` destination and registration flag per slot; that RTD structure contains 64 data bytes even for Classic CAN, so do not budget it as eight bytes. Never share one destination between outstanding registrations or use stack-local RX destinations. No heap/concurrent caller.

A concrete future multi-RX Start can reuse actual public APIs: enable with SetStartMode, enter acknowledged freeze, set global mask type/value, ConfigRxMb and Receive(TRUE) for each slot, then SetStartMode to exit freeze. All returns checked, CAN IRQs remain disabled. This covers restart after Stop left MDIS set without performing disabled MB access. The brief enable/freeze setup window is not a guaranteed receive window: motor must remain inhibited and host must wait for READY/UNCONFIGURED status. Confirm this sequence on target before accepting it; do not assume all APIs work in every freeze/disable state.

Registration failure partway through startup enters adapter FAULT; motor stays inhibited. Track which slots were registered and recover only through a reviewed teardown/reset. Keep all storage alive until successful Deinit/disabled confirmation. Stop must reject pending TX as today and unregister every active RX slot; Stop/Deinit/restart tests must cover all slots. Do not force cancellation of MB0 to report a motor fault.

Per-slot poll sequence: MainFunctionRead → GetTransferStatus → validate/copy a completed public frame → Receive(TRUE) re-register → publish the independent copy. If re-registration fails, preserve the existing policy: fault, do not publish that frame as reliable. Even a rejected/overrun frame requires re-registration. Hardware may accept the next frame during the copy/rearm interval; persistent storage and completion flags must be tested under burst load. One MB per ID is not a FIFO or a lossless guarantee.

### Overrun, ordering and admission

RTD can report a delivered latest frame whose raw CS CODE is RX_OVERRUN; software cannot reconstruct the lost frame or certify whether STOP was among lost traffic. Phase 4 therefore treats control RX overrun/queue overflow as a safe-inhibit event, discards affected control work and invalidates staging; do not execute an overrun START/DEMAND and then merely log it. Count loss per channel and preserve raw diagnostics.

Distinct MBs have no total application ordering. Polling MB3 does not prove its COMMIT followed a pending MB2 WRITE. Host waits for application ACK of each OPEN/WRITE before the next transaction step. The command sequence orders Command only; it does not replace transaction correlation. Poll Command first and again after Config/Apply work; STOP dispatch bypasses ordinary queues. Under the proposed bounded rate, at most one completed frame per slot per scan plus the second Command scan is serviced. Measure real drain latency and queue occupancy.

## 4. Proposed motor interface, mock and backend contract

The interface uses only fixed-width types and domain enums. Proposed operations (signatures/contracts to freeze during LLD):

| Operation | Contract |
|---|---|
| `Motor_InitSafe()` | Startup inhibited, report capabilities; never enable by initialization. Real pins/reset levels require board evidence |
| `Motor_GetCapabilities()` | Cached mode/direction/sensor/fast-inhibit support and approved-envelope availability; unsupported/unknown remains explicit |
| `Motor_RequestConfigure(config, operation_id)` | Copy validated domain config, asynchronous result, never enables outputs. No retained caller stack pointer |
| `Motor_RequestStart(direction, operation_id)` | Requires safe/ready/interlocks and approved config; asynchronous enable at zero demand |
| `Motor_RequestDemand(magnitude_bp)` | Store new target without waiting for CAN/SPI. Active direction fixed by START. Backend owns approved slew/control update |
| `Motor_ForceSafeOff(reason)` | Priority bounded inhibit request independent of CAN TX and ordinary SPI queue. Clears targets and invalidates old operation generations. Confirmation is separately observable |
| `Motor_RequestClearFault(operation_id)` | Only inhibited, cause absent and backend permits clearing; no automatic START |
| `Motor_Service(now, budget)` | Advance one bounded chunk of backend state/transaction/control work; no blocking waits/retry loops |
| `Motor_GetSnapshot()` | Cache-only state, safe-off confirmation, ready-for-start, fault/capability flags, feedback values with validity/age and completed operation ID |

All async completions carry operation ID/generation; a late START/config completion after STOP/session expiry is ignored by the coordinator. Backend itself must suppress stale enable writes, not merely hide stale completion from CAN. An accepted configure/START operation must never re-enable after a higher-priority inhibit. SPI-team contract must explicitly cover cancellation and queued writes.

The **mock** provides deterministic fake time, copied configs/targets, configurable completion delays/failures, direction capabilities and simulated feedback with mock/quality flags set. It must never touch motor pins/PWM/SPI. It can test lease loss, overrun, busy backend and late completions with no motor energized. A successful mock START proves a state transition only.

The **physical backend** may combine SPI configuration/diagnostics, PWM/enable GPIO and ADC/encoder sensing. It is not assumed to be SPI-only. If safe inhibition can only wait behind unbounded SPI, or no verified disable/enable default exists, physical motor integration is blocked. Do not choose coast versus dynamic brake, fault-reset writes, physical protection thresholds, control-loop rates or a reverse dwell without board/motor evidence. Applied duty is commanded/control effort; measured current and speed require actual sensors/calibration.

## 5. Motor state machine and safe behavior

State meanings/allowed wire transitions follow the specification. Startup: BOOT_SAFE → UNCONFIGURED; complete atomic configuration → STOPPED; authorized START → STARTING → RUNNING at zero demand; STOP → STOPPING → STOPPED. Fault anywhere → immediate inhibit request, then FAULT (STOPPING may represent the confirmation interval). A fault bit is latched even if the CAN controller later auto-recovers.

START is denied without valid config, fresh command lease, known direction capability, backend-ready/interlocks and approved operating envelope. Setting demand never implicitly starts. All STOP/fault transitions zero target and cancel prior operation generations. A power cycle/reconnection/status refresh does not restore motion. CLEAR_FAULT goes to an inhibited state only and is refused while the cause persists.

Lease freshness is based on **fresh accepted matching-session commands**, not received electrical activity, configuration writes, duplicate commands or GUI timestamps. Check expiration before draining queued traffic and after bounded work; an old queued heartbeat must not revive an already expired session. While stopped/unconfigured, stale communication inhibits START but need not create repeated fault transitions. While STARTING/RUNNING, 500 ms expiry is a proposed safe-action trigger. Timing/physical deceleration limits still need approval; do not claim the shaft is stationary after 500 ms.

Communication loss, bus-off, CAN driver fault, invalid timebase, control RX loss, queue overflow or motor fault request safe-off independently of reply availability. Preserve pending TX storage. Do not deinitialize CAN to stop the motor. A fresh session/rearm handshake is required after critical communication/control faults; old queued START/DEMAND cannot enable. Repeated faults during a latched fault episode must not rotate the session on every loop turn; create one new recovery session after inhibition/first-fault capture.

## 6. Nonblocking foreground event loop

The current TX wait/delay loops must not be copied into Phase 4. Proposed turn ordering:

1. Read monotonic time, check its health and expired command/backend deadlines; apply pending inhibit before any actuation request.
2. Service independent backend safety/stop completion and latch local faults. No CAN TX condition can block this step.
3. Capture CAN diagnostics once, poll pending TX completion once. A BUSY leaves ownership unchanged and returns control immediately.
4. Poll Command MB, dispatch recognized STOP immediately; otherwise validate admission/sequence into a bounded event queue.
5. Poll Config and Apply MBs once each, then Command again. Copy/rearm promptly; overrun/queue overflow triggers inhibition before executing queued motion events.
6. Process a bounded number of protocol events (proposed max 4/turn), with STOP/deadline precedence. Recheck lease before START/DEMAND. Never wait for ACK transmit or backend completion.
7. Advance motor/backend service once; evaluate operation completions and produce terminal responses. Recheck time/stop conditions after work.
8. Schedule due Status/Feedback/Error records; coalesce latest telemetry. If MB0 is free, submit at most one prioritized frame. Maintain pending ownership until PollTx consumes completion.
9. Record timing/counters and repeat without delay loops, Sleep, interrupt-driven CAN or DMA.

Targets for mock/initial integration (proposed): maximum healthy loop turn 2 ms; heartbeat 50 ms; lease 500 ms; default Status/Feedback 100 ms. These are not measured WCET. A real safe-action bound is `time-detection latency + bounded loop latency + verified backend inhibit latency`; it is not the CAN retransmit timeout. UI/PC timing cannot provide the MCU motor watchdog.

### Time service

Expose a hardware-independent monotonic time interface for the coordinator/mock tests. Do not use DELAY_LOOPS or OSIF_COUNTER_DUMMY. A polled SysTick-based service is a possible **CAN+mock** bring-up backend only if exclusive ownership is verified, Clock RTD supplies the clock frequency and polling proves no missed wraps. A 24-bit core counter wraps in about 350 ms at 48 MHz; debugger halts/stalls can break an extension based on observing every wrap. It is not accepted as the sole physical-motor safety mechanism.

SPI/platform team must reserve a reviewed real monotonic timer/watchdog strategy (timer width, overflow handling, halt behavior, clock changes and independent inhibit). A dedicated timer may require an installed RTD component/configuration not currently in the project; that is a future integration change requiring review, not permission to alter generated files now. Mock accepts injected time and can advance/reset it deterministically. Invalid/backward/uncertain time forces inhibition. Never label polling iteration counts as milliseconds.

### Queues, priorities and pending TX

Proposed fixed capacities: RX event queue 8 entries, ACK/result queue 16 plus one rejection reserve, command-result cache 32 records, telemetry one latest slot per type, Error Status one latched/latest slot. Terminal command cache lifetime is 500 ms; asynchronous records stay pinned until completion and then that retention interval. Limit fresh Command requests to 50/s; verify the spec's async-deadline/sequence-reuse constraint before physical integration. STOP is an out-of-band latch, not an RX queue entry. Capacity is a design starting point to verify, not proven sizing. Check/reserve response capacity before accepting mutating commands. Queue overflow cannot be handled by silently dropping a START/STOP and continuing.

TX priority: Error event → terminal control ACK → other ACK/NACK → Status → Feedback. Rate-limit repeated errors/NACKs and coalesce periodic telemetry so a sustained error report cannot indefinitely starve a terminal ACK. Proposed fairness: after four high-priority submissions, allow a due Status when no new critical event/terminal control response is waiting. Do not create a software priority claim that preempts a frame already in MB0: pending transmission can block all wire reports. Physical safe-off remains independent.

Example normal load: 20 heartbeats/s with responses and two 10 Hz telemetry streams is roughly 60 frames/s before commands. Using a planning allowance of 160 wire bits/frame gives about 2% at 500 kbit/s. This is an illustrative budget, not a bound under arbitration/error storms. At minimum 20 ms telemetry periods load rises; validate worst admitted request rates and disable motion if service deadlines cannot be met.

## 7. Proposed directory and LLD outline

No directories/code below are created in this task:

```text
drivers/can/                    existing RTD adapter, later reviewed multi-RX extension
protocol/can_motor/inc/         motor_protocol.h, motor_protocol_types.h, motor_protocol_cfg.h
protocol/can_motor/src/         codec, validation, config transaction/CRC, correlation
application/motor/inc/          motor_app.h, motor_app_types.h
application/motor/src/          state machine, admission/lease, event loop, TX scheduling
interfaces/motor/              motor_if.h (domain types/contracts only)
backends/motor_mock/            deterministic mock implementation
backends/motor_spi_pwm/         future SPI-team adapter; no vendor internals copied
platform/time/                 monotonic time interface + later reviewed hardware backend
gui/domain/                    protocol/session/config models; no PCAN imports
gui/transports/                pcan_basic, optional ESP32 bridge, mock
gui/ui/                        controls/views/plots; no wire packing
tests/protocol/, tests/motor_app/, tests/can/, tests/gui_domain/
```

LLD documents/work items should define: public structs/enums and memory ownership; encode/decode/validation order; CRC vectors; command freshness/cache/session state; configuration bitmap/lifetime/generation; state transition table with entry/exit actions; backend operation generation and safe-off override; per-slot RX lifecycle/partial-init cleanup; queue invariants/overflow/fairness; time wrap and deadline arithmetic; diagnostic snapshots/fault clearing; GUI worker/thread ownership and backend capability mapping. No packed-struct ABI or heap ownership is assumed.

## 8. GUI transport separation

Backend-neutral frame record: ID, is_extended, is_remote, DLC, copied data bytes, optional adapter timestamp, host monotonic receive time, and transport error/status metadata. Protocol accepts only defined standard data frames. Normalize PCAN status/error/echo frames separately; never decode them as Motor Status. PCAN send API acceptance is not wire completion or a motor ACK.

Domain/session worker owns sequence, staging CRC, pending-response ledger, lease/heartbeat schedule and stale telemetry model. UI thread renders snapshots and posts intents; it never calls a blocking adapter read or delays heartbeat behind plot updates. Bounded queues and STOP priority are required. Adapter disconnect/queue age limit cancels unsent motion work; reconnect discovers session and requires explicit rearm. Do not replay stale backend TX queues on reconnect.

Planned PCAN-Basic transport uses Classic initialize/read/write/uninitialize through the chosen vendor-supported Python binding. Exact wrapper import names, DLL version/channel/bitness remain implementation environment choices; protocol code must not depend on them. [PEAK documentation](https://www.peak-system.com/support/software-information/development-kits/pcan-basic/) is the primary backend reference. No PCAN hardware/API availability was verified on this PC.

ESP32-S3 remains a known working CAN test peer. Its existing GUI can craft/display v1 raw frames independently; sustained motor/mock RUNNING needs automatic valid KEEPALIVEs. A Python backend over ESP32 is optional and requires an explicit bridge framing/queue/error contract, not assumed from the fact its GUI can transmit CAN.

## 9. Integration decisions requiring SPI-team agreement

1. Exact motor-stage part/revision/schematic, power supply, wiring, pin ownership and CAN/SBC sharing; prove no conflict with PTE4/5 and startup/debug pins.
2. Safe reset/default output levels and independent fast inhibit path, its worst-case latency and readback/confirmation; braking/coasting behavior and mechanical implications.
3. SPI/PWM topology: actual API signatures, transfer lifetimes, bus sharing/locks, nonblocking completion/failure/cancellation semantics, polling budget and error recovery.
4. Approved motor electrical/mechanical envelope, PWM frequency/resolution, direction capability, reverse/standstill policy, duty and slew validation. Datasheet ratings alone are insufficient.
5. Current/speed/temperature feedback sources, ADC/encoder ownership, units/sign/calibration, update rate, saturation and validity/staleness. No current/rpm capability inferred from a CAN field.
6. Hardware fault mappings, which faults latch, permitted clear conditions, protection thresholds/independence, and watchdog behavior if the CPU/event loop/SPI stops.
7. Monotonic timer resource and worst-case scheduling/control deadlines; compatibility with any required PWM/ADC interrupts. CAN remains polling; motor control interrupt requirements need a separate explicit architectural decision.
8. Whether open-loop normalized duty suffices or a future closed-loop speed/torque feature is required; do not silently interpret duty as speed control.
9. Session uniqueness across reset, authorized host/reconnect policy, protocol timings and any persistent configuration requirement.

Physical motor START is blocked until these are evidenced and the operating envelope/fast inhibit/timebase are accepted. Protocol/mock work can proceed after protocol review without making those hardware assumptions.
