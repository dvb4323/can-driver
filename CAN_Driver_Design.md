# CAN Driver Design - RTD Adapter (Phase 3 current contract)

Date: 2026-10-15

## Phase 3 contract update (takes precedence over the historical Phase 1 sections below)

Software implementation is approved. Physical testing remains unauthorized until VP230 logic-level compatibility and onboard SBC RX isolation are verified. The user reported Phase 2 internal loopback PASS on the board; the Phase 3 changes have not been built or executed. All builds/debugging are user-operated in S32DS UI.

The architecture remains a foreground singleton adapter over unmodified RTD. CAN0, TX MB0, RX MB1, standard Classic data frames, static aligned TX storage, stable RX storage, busy handling, RTD polling services, immediate RX re-registration, null callbacks, disabled CAN IRQs and DMA-off guard are retained. Board pin setup is a separate module; application and SPI do not acquire RTD types.

### Configuration and mode

`Can_Config` now contains `rx_id` and `Can_Mode mode` (`CAN_MODE_LOOPBACK=0`, `CAN_MODE_NORMAL=1`). Invalid modes/IDs are rejected before hardware access. `CAN_CONFIG_DEFAULT` initializes loopback explicitly. Rebuild every caller after this public structure change.

`CanDriver_Init` still validates the complete generated loopback baseline. It copies `FlexCAN_Config0` into private static `s_rtd_config`, changes only `flexcanMode`, and passes that copy plus generated `FlexCAN_State0` to RTD. The generated constant/state and every file in `generate/`, `RTD/` and `.mex` remain unchanged. Mode is fixed until a deliberate safe Stop/Deinit/new Init or reset. No runtime mode setter is added.

RTD sets normal LPB=0, LOM=0 and SRXDIS=1; loopback sets LPB=1, LOM=0 and SRXDIS=0. Normal-mode RX therefore requires another node's frame. Clock initialization and raw timing remain unchanged: 500 kbit/s is derived from an expected 48 MHz PE clock, not measured by this implementation.

### Diagnostics API and sampling

`CanDriver_PollDiagnostics()` is an explicit foreground service, RUNNING only. Call once per polling round, whether TX/RX return BUSY/NO_DATA or complete. Separate independent TX/RX calls do not define a round, so neither implicitly performs a regular ESR1 sample. `main.c` calls this service once before TX/RX each iteration. `Can_Fail` also attempts a final sample before publishing a wrapper fault; Stop/Deinit capture before possibly disabling the controller. No sampling occurs after failed RTD Init or once register access may have been disabled.

`CanDriver_GetDiagnostics(Can_Diagnostics *out)` validates its pointer and copies RAM only in any state, including UNINITIALIZED/FAULT. It overlays current driver state and TX-pending ownership. It returns OK for a valid output pointer even when `valid=0`; callers must examine validity. It never invokes RTD or clears evidence.

The cached structure includes the session mode, latest `Can_DiagnosticSample`, saturating sample count, event-only accumulation, first observed hardware-error sample, first wrapper-fault/bus-off sample/status, validity flags and a sticky `bus_off_seen`. Samples contain raw ESR1, sequentially read TX/RX counters, decoded fault state, warnings and ACK/bit/CRC/form/stuff indicators. FLTCONF 00=active, 01=passive, 1x=bus-off. FLTCONF/current state is never ORed into accumulated events.

ESR1 is read once per capture using actual `FlexCAN_Ip_GetErrorStatus`; ECR counters use the verified public RTD getters. Read-sensitive protocol events are retained in the first-error snapshot/event accumulation. No ClearErrorStatus, error-IRQ enable or private hardware helper is called. These snapshots are sequential observations, not atomic. Do not watch live ESR1 in the debugger.

Snapshots survive Stop/Deinit and failed reinitialization. Successful Init, including IRQ suppression, begins a fresh diagnostic session with `valid=0`. A failed reinitialization can leave a last-valid sample/mode from the previous session; the validity flag means a cached sample exists, not that it was sampled during the failed call. With no prior sample, `first_fault_sample_valid=0`. The first fault remains immutable for the session. Current application failure status is separately retained in `g_can_test`.

Bus-off is latched diagnostically and causes the application one-shot test to FAIL; it does not cancel or overwrite TX or automatically change the wrapper to FAULT. RTD API failures retain the existing FAULT transition. Transient bus errors alone do not masquerade as transfer failures. The generated controller recovery policy is retained. Pending TX may keep retrying after the application holds results.

### Application and board integration

`board/board_can.c` owns PORTE PCR4/5 setup only. It verifies PCC present/gate bits, checks both pin locks before mutation, accepts an already compatible locked pin, replaces MUX with ALT5, clears IRQC/PE/PS, preserves other pad settings, writes ISF=0 to avoid unintended W1C clearing, and verifies fields by readback. It does not initialize clocks, CAN, SBC, GPIO direction or another pin. `BoardCan_GetDiagnostics` is cache-only. Board failure can leave a partial pin change on readback failure; there is no unsafe automatic rollback.

`src/main.c` uses `CAN_TEST_PROFILE=0` (LOOPBACK) by default; `1` selects NORMAL_TWO_NODE. Normal profile additionally requires `CAN_TEST_HARDWARE_VERIFIED=1`. This is a deliberate build-time assertion after human verification/authorization, not a hardware probe. With its default zero, normal profile records failure HARDWARE_NOT_VERIFIED before pin/CAN initialization. Loopback skips board setup entirely.

Both profiles transmit once: ID 0x14, DLC 8, `01 02 03 04 05 06 07 00`. Loopback expects itself. Normal expects STM32 ID 0x14, DLC 8, `A1 A2 A3 A4 A5 A6 A7 A8`. RX and TX completion are independent. Iteration-budget TIMEOUT, first failure and diagnostics are preserved without automatic Stop/Deinit/resend. `CanTest_HoldResults` is the breakpoint; `g_can_test` is the RAM object to inspect. The updated code requires a fresh user-operated build/regression; the old Phase 2 PASS is not verification of this revision.

See `CAN_Phase3_Implementation_Report.md` for changed files, verification limits and the exact UI procedure. Host tests and fakes cover mode/config isolation, read-sensitive diagnostics, lifecycle/ownership, board checks and application profiles/budgets; they are updated but not executed in this phase.

## Historical Phase 1 design

The remaining sections record the original Phase 1 scope and contracts. The current Phase 3 update above supersedes statements that limit operation to loopback, pass the original configuration pointer, defer diagnostics/physical-mode software, or leave main interrupt-based. Earlier reports are retained as historical evidence.

## Scope and architecture decision

This implementation follows the instructor-approved **RTD adapter architecture**. It supersedes the independent register-driver proposals in `CAN_Project_Analysis.md` and `CAN_Hardware_Analysis.md` for this phase. Both reports were read before implementation. NXP RTD 1.0.1 remains responsible for all FlexCAN register access, message-buffer sequencing, byte packing, handshakes and silicon workarounds. No RTD source was copied or edited.

```text
Future Application / protocol logic
    -> drivers/can/inc/can_driver.h + can_types.h + can_cfg.h
        -> drivers/can/src/can_driver.c
            -> existing FlexCAN_Ip API + IntCtrl_Ip API
                -> existing RTD/ + generated FlexCAN_Config0 / FlexCAN_State0

SPI driver: separate module; no dependency in either direction with CAN adapter
Clock / platform / future board setup: caller-owned prerequisites
```

The module is a singleton, single-caller, foreground-only CAN0 owner. It has no heap, ISR callbacks, DMA, blocking transmit/receive API, application protocol or SPI code. Private use of `FlexCAN_Ip_HwAccess.h` is limited to **decoding metadata already returned by RTD**; the adapter calls no hardware-access helper or register pointer.

## Fixed Phase 1 baseline

| Property | Contract |
|---|---|
| Controller / MBs | CAN0, TX MB0, RX MB1 |
| Transport | Classic CAN, standard 11-bit data frames, DLC 0..8 |
| Operating mode | Generated internal loopback only; incompatible generated settings are rejected |
| Controller data | Pass the original `&FlexCAN_Config0`, `&FlexCAN_State0` to RTD |
| Timing | Existing raw values retained: 7/4/1/5/1; expected SYS_CLK/PE 48 MHz, derived 500 kbit/s |
| Receive filter | One exact standard RX ID supplied by `Can_Config.rx_id`; default 0x14 |
| Platform | Caller establishes clocks/access before Init; no clock or platform initialization is performed |
| Pins/transceiver | Outside Phase 1; no normal-mode or physical-bus implementation |

`can_cfg.h` constants describe the fixed baseline, not runtime clock/bitrate programming. Init validates the generated mode, 16 MBs, eight-byte allocation, PE source request, timing fields, FD/BRS/enhanced timing disabled, FIFO disabled and callbacks null. DMA support must remain compiled off. None of these checks measures the hardware clock.

## Files and ownership

```text
drivers/can/
  inc/can_types.h    Frame, config, status and lifecycle enums; standard C types
  inc/can_cfg.h      Fixed baseline and default config initializer
  inc/can_driver.h   Public API and contracts
  src/can_driver.c   All RTD/platform dependencies and private transfer storage
```

Public headers expose no RTD/AUTOSAR/CMSIS types. `Can_Frame` has `uint16_t id`, `uint8_t dlc` and eight data bytes. There is no flag for extended/remote/FD frames because these are unsupported. Invalid IDs/DLCs/pointers are rejected before RTD calls.

RTD controller state remains the existing generated `FlexCAN_State0`. Private static storage consists of copied `Can_Config`, wrapper lifecycle/TX ownership flags, one `Flexcan_Ip_MsgBuffType` RX destination and an eight-byte TX union with uint32 alignment. The union prevents an unaligned application byte array from reaching the RTD's S32K144 uint32 TX load path.

The RTD Send implementation copies TX metadata/payload synchronously into its mailbox and stores no payload pointer for later completion. The public caller can reuse its frame immediately after Transmit returns. The private TX storage nevertheless remains static. RTD **does retain the RX destination pointer**: it always points at static adapter storage, never a stack frame or application receive output. Completed data is copied to public output before that buffer can be reused by the next foreground RX service.

## Public API contracts

| API | Valid state and behavior |
|---|---|
| `CanDriver_Init(const Can_Config *)` | UNINITIALIZED only. Validate config/baseline, disable/clear CAN0 NVIC sources, call RTD Init using generated objects, disable RTD MB interrupts. Success -> READY. Config copied; caller need not retain its pointer. |
| `CanDriver_Start(void)` | READY only. RTD SetStartMode first enables a previously stopped module; then configure MB1 and register persistent polling RX. Success -> RUNNING. Loopback TX starts only on a later Transmit. |
| `CanDriver_Transmit(const Can_Frame *)` | RUNNING only. Reject invalid frame or outstanding wrapper request. Submit standard data frame with `is_polling=TRUE`. OK means accepted; completion has not yet been reported. |
| `CanDriver_PollTx(void)` | RUNNING only. No request -> NO_TRANSFER. Call MainFunctionWrite then GetTransferStatus. BUSY keeps MB0 ownership; OK consumes one completion and allows another request. No implicit service/consumption during Transmit or Stop. |
| `CanDriver_PollRx(Can_Frame *)` | RUNNING only. Call MainFunctionRead then GetTransferStatus. BUSY -> NO_DATA. Completed valid frame copied and Receive(...,TRUE) re-registered before returning. OK/RX_OVERRUN have valid output; every other result leaves output untouched. |
| `CanDriver_Stop(void)` | RUNNING only. Outstanding TX -> BUSY without cancellation. Otherwise abort/unregister RX through RTD and stop CAN; unread RX may be discarded. Success -> READY. |
| `CanDriver_Deinit(void)` | READY or FAULT only. Explicit RTD teardown; verify GetStopMode because this RTD Deinit discards its internal Disable result. Success -> UNINITIALIZED. FAULT teardown discards outstanding work and may still fail. |
| `CanDriver_GetState(void)` | Any state. Return wrapper state only, without touching RTD/hardware. |

Lifecycle calls are **synchronous**, with RTD's current dummy-counter handshakes. Transmit/PollTx/PollRx do not wait for future CAN traffic. No calibrated real-time timeout is promised for any operation. CAN_STATUS_TIMEOUT means a vendor polling budget expired, not that a measured number of microseconds passed.

## State machines and failure policy

```text
UNINITIALIZED --Init success--> READY --Start success--> RUNNING
                                   <--Stop success----
READY --Deinit success--> UNINITIALIZED
Any attempted RTD operation failure --> FAULT --Deinit success--> UNINITIALIZED
```

Invalid arguments, invalid API order and preflight configuration rejection do not change state or initialize hardware. Expected polling BUSY and RX NO_DATA are normal. Stop BUSY and duplicate Transmit BUSY are also normal and leave RUNNING unchanged.

Unexpected RTD failures, including RTD BUSY during Send or RX registration, move to FAULT. The returned status retains the vendor category, so **BUSY can accompany FAULT for those unexpected failures**; inspect GetState and recover through Deinit. A failed Init might not have published RTD's internal state pointer. The adapter never calls transfer/status APIs on that path. This checkout's Deinit implementation operates on hardware, without dereferencing that state pointer; it is the explicit recovery attempt, not a guarantee of successful cleanup when clocks/hardware are broken.

TX substate:

```text
FREE --Transmit accepted--> PENDING
PENDING --PollTx BUSY--> PENDING
PENDING --PollTx OK--> FREE
```

A hardware completion that has not been consumed by PollTx still belongs to the outstanding request. Duplicate Transmit and Stop refuse it. There is deliberately no TX abort-on-timeout wrapper: the inspected RTD TX abort sets its software MB idle even after timeout, so Phase 1 does not use it to authorize reuse. A late transmission must be reconciled by PollTx or explicit FAULT teardown.

RX substate: Start registers RX_BUSY. MainFunctionRead services a received frame and RTD sets it IDLE. PollRx then validates/copies metadata and immediately calls Receive(...,TRUE), returning it to RX_BUSY. ConfigRxMb is not repeated for each frame, avoiding unnecessary mailbox reconfiguration/flag clearing. Stop uses RTD AbortTransfer on MB1 only, which clears its software receive state before restart.

Continuous reception means **automatic repeated foreground receive registration**, not background execution or a lossless queue. One call services at most one retained hardware frame. Data may overrun if polling is delayed; RX_OVERRUN reports the retained frame when RTD CS contains CODE=6. This cannot count all lost frames. Unsupported IDE/RTR/EDL, invalid ID/length/raw DLC or unexpected receive CODE is rejected as INVALID_FRAME and the receive is still re-registered. If re-registration fails, output is untouched and that just-completed frame is discarded while entering FAULT.

## Polling, interrupts and DMA

The actual RTD service pair is essential:

- TX: `FlexCAN_Ip_MainFunctionWrite(0,0)` processes raw completion, clears the flag and changes TX_BUSY to IDLE. `GetTransferStatus` then observes completion.
- RX: `FlexCAN_Ip_MainFunctionRead(0,1)` reads/unlocks the MB, clears the flag and sets RX software state IDLE. Another `Receive(0,1,&private_buffer,TRUE)` registers the next operation.

Both service functions work with IMASK1=0. GetTransferStatus alone performs no service and dereferences RTD state, so it is called only after successful initialization/registration.

Init calls `IntCtrl_Ip_DisableIrq` and `IntCtrl_Ip_ClearPending` for CAN0 sources 78..82. It installs/enables no ISR. `FlexCAN_Ip_DisableInterrupts` clears MB masks and sets RTD `isIntActive=FALSE`. Send metadata and every Receive registration request polling=true, so they do not request MB interrupts. RTD Init clears error masks; no error-enable API is called. Generated FIFO interrupt selection is inactive because FIFO is disabled; DMA is compiled off. RTD critical sections may temporarily mask/restore CPU interrupts internally: reusing those is permitted and is distinct from depending on a CAN ISR.

## Integration boundary and Phase 2 prerequisite

**Phase 1 only compiles/links the adapter. `src/main.c` does not call it.** The existing application remains the original interrupt-TX/polling-RX RTD loopback example. No module constructor or other automatic initialization was added.

Before Phase 2, replace the original main CAN ownership sequence (IRQ enable/handler install and direct CAN Init/Send/Receive/Stop/Deinit calls) with adapter calls. Initialize existing clocks once, then call Init/Start and service PollTx/PollRx from foreground. Do not append adapter Init to the original CAN sequence, call it from an ISR/SPI driver, or access generated RTD state from application logic.

The RTD has no safe public query identifying an existing CAN0 software owner. Exclusive ownership is therefore an explicit caller precondition, not an automatic handover feature. The adapter rejects its own double initialization; it cannot detect arbitrary external direct RTD calls. Another module must never change CAN interrupt enables, clocks or mailboxes while the adapter owns CAN0.

Phase 2 should add a checked loopback application and runtime evidence before claiming polling-only application behavior. Physical bus support, board mux/SBC, real-time deadlines, diagnostics/bus-off policy, queues, protocol/motor-control changes and concurrency are deferred.
