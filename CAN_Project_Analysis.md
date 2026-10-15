# CAN Project Analysis — S32K144

Date: 2026-10-15  
Project: `FlexCAN_Ip_example_S32K144`  
Scope: static inspection and architecture proposal; no source, generated configuration, IDE settings, or build artifacts changed.

## 1. Main findings and evidence limits

**The supplied project is an NXP RTD FlexCAN IP loopback example, rather than an application-owned register-level, polling-only driver.** The register accesses exist inside the supplied RTD implementation. The application calls `FlexCAN_Ip_*`, enables a CAN interrupt, transmits using interrupt completion, and receives using polling. Classic CAN and no DMA are verified.

Evidence labels used throughout:

- **Verified:** directly observable in the inspected files. This does not imply execution on hardware.
- **Derived:** calculated or inferred from verified source/configuration, with prerequisites stated.
- **Proposed:** recommended future implementation; none of this implementation has been added.
- **Unverified:** requires hardware observation, board revision information, or additional documentation review.

No build, regeneration, debugger session, flashing, or bus test was performed. Existing ELF/map/object/dependency files are historical evidence, not proof that the current source builds or runs. The exact installed IDE version was not queried; the saved dependency files reference S32DS 3.4. No `AGENTS.md` was found under the project.

| Requested context | Verified state in this checkout |
|---|---|
| S32K144 | `.mex` selects S32K144 LQFP100; `.cproject` defines `S32K144`, `S32K1XX`, `CPU_S32K144HFT0VLLT` |
| S32K144EVB | `description.txt` names S32K144EVB-Q100 PCB 29248 RevA SCH RevC; attached physical board/revision unverified |
| Classic CAN | `FlexCAN_Config0.fd_enable = FALSE`, BRS false, eight-byte payload allocation |
| Register-level application driver | Application uses RTD APIs; hardware register code is in `RTD/` |
| Polling only | RX is polled; TX requests interrupt completion and the NVIC CAN0 MB IRQ is enabled |
| No DMA | `FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE = STD_OFF` |
| Physical CAN traffic | Internal loopback is configured; external communication is not established by this example |

## 2. Folder structure and project configuration

```text
FlexCAN_Ip_example_S32K144/
  .project                         Eclipse project name, builders, C nature
  .cproject                        Managed build, compiler, symbols, paths, linker
  .settings/                       IDE preferences
  FlexCAN_Example_S32K144.mex       S32 Configuration Tools input
  ClockYaml.txt                    Saved clock tool settings/frequency summary
  ClockConfigurationMappings.txt   Clock tool mapping data
  description.txt                 Original example instructions
  src/main.c                      Complete application sequence
  include/check_example.h         Example result-marker helper, unused by main
  generate/include/               Clock, CAN, interrupt, OsIf, platform settings
  generate/src/                   Generated configuration objects
  RTD/include/                    Copied NXP RTD interface/internal headers
  RTD/src/                        Copied NXP driver and support implementation
  Project_Settings/
    Startup_Code/                 Reset, vector table, runtime/system setup
    Linker_Files/                 Flash and RAM linker scripts
    Debugger/                     P&E debug launch configuration
  Debug_FLASH/                    Existing generated makefiles and build outputs
```

**Verified build details:** `.cproject` contains a `Debug_FLASH` configuration using NXP GCC 9.2 for Arm, Cortex-M4, Thumb, hard floating point (`fpv4-sp-d16`), C99, and `linker_flash_s32k144.ld`. A RAM linker script exists but is not evidence of an active RAM build configuration. C compiler settings include size optimization and `-ggdb3`; the configuration name alone does not describe optimization/debug details accurately.

Source entries include `Project_Settings`, `RTD`, `board`, `generate/include`, `generate/src`, and `src`. **There is no `board/` directory in this checkout**, despite its build/include entries. Existing `Debug_FLASH` makefiles list the RTD CAN implementation and generated sources. Do not hand-edit those generated makefiles.

`description.txt` is partly stale: it mentions a RAM build, a different MB IRQ grouping, `IntCtrl_Ip_Init`, and PLL system-clock use. The application and generated C are the stronger evidence for current behavior.

## 3. CAN code locations

Line numbers refer to the inspected snapshot.

| Responsibility | File / symbol | Evidence |
|---|---|---|
| Application constants/payload | `src/main.c:21–27` | ID 20 decimal, RX MB1, TX MB0, eight-byte global array |
| Clock initialization | `src/main.c:42–50` | `Clock_Ip_Init`, conditional PLL wait/distribution |
| CAN IRQ enable/install | `src/main.c:51–57` | S32K144 uses `CAN0_ORed_0_15_MB_IRQn` |
| Controller init/start | `src/main.c:66–67` | Instance 0, `FlexCAN_State0`, `FlexCAN_Config0` |
| RX MB configuration | `src/main.c:68` | Standard ID, MB1 |
| TX request | `src/main.c:69–70` | `is_polling` changed to false before send |
| RX registration/poll loop | `src/main.c:71–73` | Receive polling argument true; `MainFunctionRead` services MB1 |
| Shutdown | `src/main.c:74–75` | Stop then deinitialize |
| Generated CAN settings | `generate/src/FlexCAN_Ip_Sa_BOARD_InitPeripherals_PBcfg.c:114` | `FlexCAN_Config0` initializer |
| Instance/state declarations | `generate/include/FlexCAN_Ip_Sa_BOARD_InitPeripherals_PBcfg.h:74–78` | Instance 0 and external state/config |
| Controller setup | `RTD/src/FlexCAN_Ip.c:424`, `:1876` | `FlexCAN_InitController`, `FlexCAN_Ip_Init_Privileged` |
| TX/RX public implementation | `RTD/src/FlexCAN_Ip.c:1974`, `:2021`, `:2077` | Send, configure RX MB, receive |
| RX polling dispatch | `RTD/src/FlexCAN_Ip.c:3513` | Checks MB flag and RX busy software state |
| RX extraction | `RTD/src/FlexCAN_Ip.c:1254` | `FlexCAN_IRQHandlerRxMB`, also called from polling |
| Register operations | `RTD/src/FlexCAN_Ip_HwAccess.c`, `RTD/include/FlexCAN_Ip_HwAccess.h` | Reset, freeze, RAM, masks, timing, MB packing |
| CAN0 IRQ entry | `RTD/src/FlexCAN_Ip_Irq.c:212` | `CAN0_ORED_0_15_MB_IRQHandler` dispatches instance 0, MB0–15 |

Some internal RTD comments still say `FlexCAN_DRV_*`; this is not evidence of application calls to the legacy SDK API. The actual application calls are `FlexCAN_Ip_*`.

## 4. Clock configuration and pin multiplexing

### 4.1 Clock facts

`generate/src/Clock_Ip_Cfg.c:126` defines `Clock_Ip_aClockConfig[0]`:

- SIRC and FIRC enabled; SOSC configured for 8 MHz and enabled.
- SPLL enabled from SOSC, predivider 1, multiplication factor 28; PLL output dividers set to 8.
- RUN, VLPR, and HSRUN source selections in the saved configuration are FIRC. Their presence does not mean the application enters all those power modes.
- RUN core divider 1, bus divider 1, slow divider 2.
- FlexCAN0/1/2 and PORTA–PORTE clock gates enabled. Only CAN0 is used by the application.

`ClockYaml.txt` reports FIRC/core/bus/FlexCAN0 at 48 MHz, SOSCDIV1/2 at 8 MHz, and SPLLDIV1/2 at 14 MHz. **Derived intended RUN frequencies:** core 48 MHz, bus 48 MHz, slow 24 MHz. These are configured values, not measured frequencies.

`main` conditionally waits for PLL lock and calls `Clock_Ip_DistributePll`; this alone does not prove that RUN uses PLL. The generated RUN selector explicitly names FIRC. `RTD/src/Clock_Ip_Data.c:923` maps FlexCAN0 gating to PCC index 36; this is a gate entry, not a generic selectable PCC peripheral-clock source entry. Do not assume that changing PCC PCS selects CAN protocol-engine timing.

### 4.2 CAN timing and clock-source uncertainty

The generated config requests `is_pe_clock = TRUE`. `FlexCAN_SetClkSrc` in `RTD/include/FlexCAN_Ip_HwAccess.h:1347` maps this to `CTRL1[CLKSRC] = 1`. Nominal timing values are **raw register encodings**, as confirmed by `FlexCAN_SetTimeSegments` at line 962:

| Field | Stored value | Effective value |
|---|---:|---:|
| PRESDIV | 5 | Divide by 6 |
| PROPSEG | 7 | 8 time quanta |
| PSEG1 | 4 | 5 time quanta |
| PSEG2 | 1 | 2 time quanta |
| RJW | 1 | 2 time quanta |

**Derived, conditional on a 48 MHz protocol-engine clock:** total bit length is `1 + 8 + 5 + 2 = 16 tq`; bitrate is `48,000,000 / (6 * 16) = 500,000 bit/s`; sample point is `14/16 = 87.5%`. FD timing values also exist in the initializer but FD is disabled.

**Important source-level uncertainty:** `FlexCAN_InitController` selects CLKSRC before calling `FlexCAN_Init`, while `FlexCAN_Init` later writes `base->CTRL1 = 0` (`RTD/src/FlexCAN_Ip_HwAccess.c:779`). No subsequent explicit `FlexCAN_SetClkSrc` call was found in the initialization path. Whether the later write changes CLKSRC depends on hardware write restrictions and the module state at that point. Consequently, **500 kbit/s must not be reported as hardware-verified**. If the final source were 8 MHz with the same timing, the result would be approximately 83.33 kbit/s. Read back `CTRL1[CLKSRC]`, SCG clock status/dividers, and PCC gate state after initialization, then confirm timing externally. Loopback completion alone cannot establish bus bitrate.

### 4.3 Pins and board interface

**Verified absence:** no application call to `BOARD_InitPins`, Port initialization, or CAN `PORTx->PCR[]` programming was found. The `.mex` Pins tool has `update_project_code="false"`; its `BOARD_InitPins` function contains an empty `<pins/>`. It references prospective `board/Port_Ci_Port_Ip_Cfg.*` files that are absent. Clocking PORTE does not configure its multiplexers.

**External board reference, not a checkout finding:** NXP's EVB getting-started guide maps CAN0 RX to PTE4 and CAN0 TX to PTE5. That guide identifies Rev B, whereas this project's description names Rev A. [NXP S32K144EVB getting-started guide](https://www.nxp.com/document/guide/getting-started-with-the-s32k144evb%3ANGS-S32K144EVB)

NXP's cookbook depicts these CAN0 pins connected to an onboard SBC/transceiver and discusses board-generation differences. The future board adapter must confirm the actual schematic, pin alternate-function value, SBC/transceiver enable procedure, power, connector routing, and termination. None is established by the loopback example. [NXP AN5413, CAN example](https://www.nxp.com/docs/en/application-note/AN5413.pdf)

## 5. FlexCAN registers and message buffers

The device register header is an **external installation dependency**, not a project-local header:

```text
C:/NXP/S32DS.3.4/S32DS/software/PlatformSDK_S32K1_2022_02/
  SW32K1_RTD_4_4_1_0_1_D2202/Base_TS_T40D2M10I1R0/header/S32K144_FLEXCAN.h
```

It was available and inspected. It defines `FLEXCAN_Type`, `IP_FLEXCAN0_BASE = 0x40024000`, 128 RAM words, and 32 individual mask registers. RTD aliases and bit definitions are provided through `FlexCAN_Ip_DeviceReg.h` and `FlexCAN_Ip_HwAccess.h`.

| Register / offset from CAN0 | Current role and independent-driver relevance |
|---|---|
| MCR / `0x00` | Module disable/reset, freeze/halt handshakes, MAXMB, FIFO enable, FD enable, self-reception, mask selection |
| CTRL1 / `0x04` | Nominal timing, CLKSRC, LPB/LOM, error interrupt controls and bus-off policy |
| TIMER / `0x08` | Free-running CAN timer; read unlocks RX MB after extraction; not an independent timeout clock |
| RXMGMASK / `0x10` | Global mailbox acceptance mask; RTD initializes to all ones |
| RX14MASK / `0x14`, RX15MASK / `0x18` | Dedicated acceptance masks for MB14/15, initialized even though application does not receive through them |
| ECR / `0x1C` | TX/RX error counters; useful for future polled diagnostics; not checked by main |
| ESR1 / `0x20` | Error/status and interrupt flags; RTD clears interrupt flags during init; main has no error handling |
| IMASK1 / `0x28` | RTD clears at init, enables TX MB0 notification for nonpolling send; future polling driver keeps CAN notifications disabled |
| IFLAG1 / `0x30` | MB completion flags: bit 0 TX, bit 1 RX; clear individual flags with write-one-to-clear semantics |
| CTRL2 / `0x34` | Controller defaults and FIFO/filter behavior |
| RAMn / `0x80` | Message buffer CS, ID, and data words |
| RXIMR[] / `0x880` | Individual acceptance-mask RAM; cleared during RAM initialization; main does not select/configure individual masking |
| CBT / `0x50`, FDCTRL, FDCBT | RTD initializes related registers, but active Classic CAN timing uses CTRL1 and FD is disabled |
| RXFGMASK / `0x48`, RXFIR / `0x4C` | Legacy FIFO-related registers; not the active application receive path |

The generated controller config requests **16 MBs**, so the programmed MAXMB is expected to be 15. CAN0 supports 32 eight-byte MBs according to the header/config feature definitions; configured count and hardware capacity are different quantities. FIFO is disabled, so it does not reserve MB0–5 or a filter table. The initializer's `FLEXCAN_RXFIFO_USING_INTERRUPTS` and eight-filter enum are inactive FIFO configuration fields, not evidence of FIFO reception.

For this eight-byte allocation, each MB occupies four 32-bit words / 16 bytes:

| Application use | CS | ID | DATA0 | DATA1 | Flag |
|---|---|---|---|---|---|
| TX MB0 | `RAMn[0]`, `0x40024080` | `RAMn[1]` | `RAMn[2]` | `RAMn[3]` | `IFLAG1[0]` |
| RX MB1 | `RAMn[4]`, `0x40024090` | `RAMn[5]` | `RAMn[6]` | `RAMn[7]` | `IFLAG1[1]` |

These addresses are **derived** from the inspected base/offsets and RTD MB-region calculation (`FlexCAN_GetMsgBuffRegion`, HwAccess line 435). MB2–15 are within the configured range but are not used by main.

CS contains CODE, DLC, IDE, RTR, and timestamp fields. Standard identifiers occupy ID bits 28:18: ID 20 becomes `20 << 18 = 0x00500000`. RX configuration passes through inactive states to `RX_EMPTY` (CODE 4); TX programming uses `TX_INACTIVE` (8) before activating `TX_DATA` (12). RX completion can indicate full or overrun and must be decoded. RTD performs byte-order conversion: for the current data, the data-word representation is `0x01020304`, `0x05060700`, rather than a native little-endian byte-array cast.

The application makes no mask API call. RTD initializes global masks to all ones; its default/global-mask path therefore intends exact ID matching at MB1. Do not infer a payload-length filter from `rx_info.data_length = 8`: `FlexCAN_SetRxMsgBuff` programs ID/format/CODE, and received DLC comes from hardware.

## 6. Dependencies and initialization ownership

| Dependency | Verified use | Consequence |
|---|---|---|
| NXP RTD CAN 1.0.1, AUTOSAR 4.4 | `FlexCAN_Ip.c`, HwAccess, IRQ; version banner identifies D2202 | Existing CAN behavior depends on vendor implementation/state machine |
| Generated CAN config | `FlexCAN_Config0`, `FlexCAN_State0`, `INST_FLEXCAN_0`, feature macros | Current init cannot operate without these types/objects |
| Clock RTD and generated clock data | Called directly from main | Can remain platform setup if only CAN independence is required |
| Interrupt-controller RTD | main enables/installs CAN0 MB handler | Must be removed from the future CAN execution path for polling-only behavior |
| OsIf | CAN reset/freeze waits and other support functions | Current timeout counter is `OSIF_COUNTER_DUMMY`, duration 10000; do not assume a measured 10 ms deadline |
| SchM_Can / SchM_Mcu | Exclusive-area support in copied implementation | Not required by a single-caller polling CAN core; retained platform code may still use them |
| DET / assertions, compiler/types, MemMap | RTD includes and build sources | Full SDK independence requires addressing these dependencies, not just replacing public send/receive calls |
| Device/CMSIS headers | Installed PlatformSDK include/header paths and saved `.d` dependencies | Register definitions can be reused while explicitly retaining the header dependency |
| Startup/linker/runtime | `Project_Settings/Startup_Code`, flash linker | Preserve for initial migration; not a CAN driver responsibility |
| Board initialization | No implemented CAN pin/transceiver setup | Must be implemented and verified for external CAN |

`.cproject` references `${PLATFORMSDK_S32K1_2022_02_S32K144_M4F_1.0.1_PATH}` for Base and Platform include directories. `Debug_FLASH/src/main.d` confirms historical use of headers under `C:/NXP/S32DS.3.4/...`, including `Mcal.h`, `StandardTypes.h`, `S32K144.h`, and peripheral headers. Merely copying `RTD/` does not make the project self-contained.

Startup assembly initializes RAM/runtime state, sets VTOR, handles watchdog setup, invokes `SystemInit`, enables interrupts, and calls main. `SystemInit` handles FPU/access protection/cache-related setup; it does not provide the missing CAN pin setup. Main directly initializes peripherals: generated names containing `BOARD_InitPeripherals` do not prove that a board-wide initialization function is called. Main does **not** call `IntCtrl_Ip_Init(&IntCtrlConfig_0)`; the generated IRQ priority entry (5) must not be claimed as applied solely because it exists.

`include/check_example.h` provides `Exit_Example`, but main does not call it. The example does not publish a checked pass/fail result.

## 7. Current TX/RX data flow

1. Reset/startup transfers control to main; main initializes the configured clock tree and conditionally waits for PLL lock without an application timeout.
2. Main enables and installs the CAN0 MB0–15 IRQ handler for the S32K144 branch.
3. `rx_info` is initialized as standard ID, eight-byte data frame, polling true, remote false. Unspecified aggregate members are zero-initialized.
4. `FlexCAN_Ip_Init(0, &FlexCAN_State0, &FlexCAN_Config0)` initializes register/RAM state and RTD per-MB software state. The config selects loopback with self-reception, FIFO off, FD off, 16 MBs, null callbacks, and a bus-off recovery option.
5. `SetStartMode` enables the module and exits freeze; MB1 is then configured for standard ID 20 / `0x14`.
6. Main changes `rx_info.is_polling` to false and passes that object as TX metadata. `Send` copies `dummyData` into MB0, activates TX, and requests MB0 interrupts. `dummyData[8] = {1,2,3,4,5,6,7}` contains an implicitly zero-initialized eighth byte.
7. The internal loopback path permits the frame to match MB1. This does not require an external bus, transceiver, second node, or external acknowledgment.
8. `Receive(..., &rxData, TRUE)` records the destination pointer and RX busy state for polled completion. Although the metadata was changed for TX, this separate true argument governs receive polling.
9. Main repeatedly queries RX transfer status and calls `MainFunctionRead(0,1)`. That function checks the hardware flag and invokes the internal RX service routine. A routine named `IRQHandler` can be called synchronously by polling; the name alone does not imply interrupt-driven RX.
10. The RX service locks the MB, reads CS/ID/data, clears its completion flag, and reads TIMER to unlock it. RTD updates software transfer state to completion. TX completion separately uses the installed IRQ path and its RTD state handling; callbacks are null.
11. Once RX transfer status is success, main stops/deinitializes CAN and returns. There is no continuous receive/rearm application loop, explicit TX completion wait, comparison of received ID/DLC/payload, or application bus-error recovery.

The actual frame is **standard ID `0x14`, DLC 8, data `01 02 03 04 05 06 07 00`**. Successful receipt is the intended flow; execution was not observed. API return values are ignored, so init/send/receive failures can lead to an indefinite polling loop. Receive hardware is configured before send, but software receive registration occurs after send; a new driver should define and test pending-frame behavior explicitly.

## 8. Proposed independent polling CAN architecture

### 8.1 Scope and layers

Implement an application-owned Classic CAN mailbox driver without `FLEXCAN_DRV_*`, `FlexCAN_Ip_*`, or RTD CAN helper calls. Wrapping RTD CAN APIs would not satisfy independence. Keep the existing project configuration intact during this analysis; future integration is a separate change.

```text
Application (frames, deadlines, recovery decisions)
    -> Public CAN API (standard C types, no vendor types)
        -> S32K144 FlexCAN register implementation
            -> Device register definitions
        -> Platform adapter (clock contract and monotonic time)
Board initialization (pins, physical transceiver, power) -> platform hardware
```

Start with one controller (CAN0), one TX MB and one RX MB, FIFO disabled, Classic CAN only, no heap, single foreground caller. Preserve MB0/MB1 numbering initially for comparison, subject to the device-mask errata review. Do not silently expand to multiple instances or CAN FD.

The CAN core owns module reset/freeze/start/stop, timing, acceptance masks, MB access, completion flags, and error snapshots. Platform setup owns SCG/PCC and a monotonic deadline source. Board setup owns mux/electrical/transceiver details. Retaining `Clock_Ip` initially is compatible with **CAN-driver independence**, but it is not complete SDK independence. A fully SDK-independent phase would also replace clock/types/support dependencies deliberately.

### 8.2 Required register-level behavior

- Validate configuration before touching hardware: controller, clock source/frequency, field ranges, bitrate, sample point, MB allocation, IDs, DLC, and masks.
- Establish a documented platform-clock contract. Select/verify the PE clock in the hardware-permitted state, preserve it across reset/configuration, and read back the final setting.
- Bound module-disable, soft-reset, freeze-entry, freeze-exit, and ready handshakes with a real time source. Propagate failures and leave the controller in a defined state.
- Initialize required MB RAM/masks; explicitly disable FIFO, FD, CAN DMA requests, MB interrupts, and CAN error/wakeup interrupts. The board/platform integration must leave corresponding NVIC CAN sources disabled and clear stale pending state when taking ownership.
- Program Classic timing and mask mode explicitly instead of relying on a vendor reset-default constant. Set self-reception policy separately for normal and loopback modes.
- Configure RX before starting traffic. Clear stale flags with direct writes of the owned W1C bit mask; avoid `IFLAG1 |= mask`, which can clear unrelated pending events.
- TX: reject invalid or busy requests, make MB inactive when safe, write ID and packed data, then write the activating CS/CODE last. Poll completion/error state; never treat queue acceptance as transmission success.
- RX: poll the flag, perform the documented CS/CODE lock/BUSY sequence with a bounded wait, read metadata/data into caller storage, clear the owned flag and unlock through TIMER in the documented order. Preserve overrun indication and verify the return-to-receive behavior; do not invent a blind CS rewrite as a generic rearm sequence.
- Keep an MB locked only for the short extraction sequence. Avoid debugger reads of MB CS during timing-sensitive tests.
- Poll ESR1/ECR, report bus-off/error-passive/warnings and counters, and document read/clear side effects. Latch useful error information so status queries do not silently discard it.
- Define TX timeout/cancellation ownership. A caller timeout must not allow a still-pending MB to be overwritten; late completion and safe cancellation must be reconciled before reuse.
- Review applicable silicon errata independently. `generate/include/SoC_Ips.h:219–223` enables `ERR_IPV_FLEXCAN_E050246`; RTD contains conditional handling involving FIFO/locked RX MBs. That macro alone does not prove the actual silicon mask or that all relevant workarounds have been identified. FIFO stays disabled in the initial design.

Polling means the CAN path does not depend on ISRs or DMA. A free-running hardware counter read directly by foreground code can provide timeouts without a timer ISR. Define maximum service latency: a single RX MB provides limited buffering and cannot guarantee lossless reception under sustained traffic if polling is delayed.

## 9. Proposed public API and configuration

The following declarations are design sketches in this report only. Use `stdint.h`/`stdbool.h`, not `uint8`, `boolean`, `Flexcan_Ip_StateType`, or AUTOSAR types in the public interface.

```c
typedef enum {
    CAN_OK, CAN_BUSY, CAN_NO_DATA, CAN_TIMEOUT, CAN_INVALID_ARG,
    CAN_NOT_INITIALIZED, CAN_BAD_STATE, CAN_BUS_OFF, CAN_HW_ERROR,
    CAN_RX_OVERRUN
} Can_Status;

typedef enum { CAN_ID_STANDARD, CAN_ID_EXTENDED } Can_IdFormat;
typedef enum { CAN_MODE_NORMAL, CAN_MODE_LOOPBACK, CAN_MODE_LISTEN_ONLY } Can_Mode;
typedef enum { CAN_CLOCK_OSCILLATOR, CAN_CLOCK_PERIPHERAL } Can_ClockSource;

typedef struct {
    uint32_t id;                 /* Unshifted 11-bit or 29-bit ID */
    Can_IdFormat format;
    bool remote;
    uint8_t dlc;                 /* Classic CAN: 0..8 */
    uint8_t data[8];
} Can_Frame;

typedef struct {
    uint16_t prescaler;          /* Actual divisor, not PRESDIV encoding */
    uint8_t prop_seg_tq;
    uint8_t phase_seg1_tq;
    uint8_t phase_seg2_tq;
    uint8_t sjw_tq;
} Can_BitTiming;

typedef struct {
    uint8_t mailbox;
    Can_IdFormat format;
    uint32_t id;
    uint32_t id_mask;           /* Unshifted: 1 means compare */
    bool match_remote;         /* Whether RTR participates in matching */
    bool remote;
} Can_RxFilter;

typedef struct {
    uint8_t instance;          /* Initially only 0 accepted */
    Can_ClockSource clock_source;
    uint32_t engine_clock_hz;  /* Must match established platform clock */
    Can_BitTiming timing;
    Can_Mode mode;
    uint8_t mailbox_count;
    uint8_t tx_mailbox;
    Can_RxFilter rx_filter;    /* Initially one RX mailbox */
    uint32_t init_timeout_us;
    bool automatic_bus_off_recovery;
} Can_Config;

typedef struct Can_Context Can_Context; /* Driver-owned static storage */

typedef struct {
    uint32_t raw_esr1;
    uint8_t tx_error_count;
    uint8_t rx_error_count;
    bool bus_off;
    bool error_passive;
    bool tx_pending;
    bool rx_overrun;
} Can_Diagnostics;

Can_Status Can_Init(Can_Context *ctx, const Can_Config *cfg);
Can_Status Can_Start(Can_Context *ctx);
Can_Status Can_Stop(Can_Context *ctx, uint32_t timeout_us);
Can_Status Can_Deinit(Can_Context *ctx, uint32_t timeout_us);
Can_Status Can_TryTransmit(Can_Context *ctx, const Can_Frame *frame);
Can_Status Can_PollTransmit(Can_Context *ctx);
Can_Status Can_TryReceive(Can_Context *ctx, Can_Frame *out_frame);
Can_Status Can_GetDiagnostics(Can_Context *ctx, Can_Diagnostics *out);
```

**Contracts:** `Init` leaves a configured stopped controller; `Start` enters operation. `TryTransmit` copies the frame before returning and `CAN_OK` means accepted; completion is obtained with `PollTransmit`. `CAN_BUSY` from polling means still pending. `TryReceive` returns `CAN_NO_DATA` without waiting for a future frame; `CAN_RX_OVERRUN` returns the retained frame plus an overrun indication. Its internal hardware-access handshake is still bounded. Listen-only mode rejects transmit calls. ID/DLC/pointer/state errors return a status without launching traffic.

An opaque context needs a concrete allocation solution: initially expose a `Can_GetContext(0)` accessor returning driver-owned static storage, or provide a documented public fixed-size context. Do not require applications to allocate an incomplete type. Copy the scalar configuration into that context and document single-caller/non-reentrant ownership. Avoid callbacks and background tasks.

Optional `Can_TransmitBlocking`/`Can_ReceiveBlocking` wrappers can repeatedly call the polling APIs with real deadlines. They must preserve pending TX ownership on timeout, return bus-off promptly, and specify what happens to received data and stale completion flags. Defer public cancellation/recovery APIs until their state transitions are fully specified. Runtime filter changes should initially require the stopped state.

Use physical timing quantities in configuration and encode minus-one fields internally. Validate the requested timing mathematically; optionally provide a pure timing calculator accepting clock Hz, target bitrate, and target sample point. Do not blindly reuse the current raw values with a new clock source.

### Proposed directory layout

To minimize future build-configuration changes, place driver implementation under the already configured `src/` source root and public headers under `include/`:

```text
include/can.h                     Public types/API
include/can_config.h              Application config declaration
include/board_can.h               Board initialization interface
src/can/can.c                     API state, validation, polling wrappers
src/can/can_flexcan_s32k144.c      CAN register access and MB operations
src/can/can_flexcan_s32k144.h      Private hardware interface
src/can/can_platform.h            Private clock/time contracts
src/platform/can_platform_s32k144.c
src/board/board_can_s32k144evb.c   Pin mux and actual transceiver setup
src/config/can_config.c           Plain-C configuration, not generated RTD CAN data
src/main.c                       Future application integration
tests/                           Future host tests; outside embedded source roots
docs/                            Optional implementation/bench notes
```

No proposed directories or source files were created. A future S32DS-managed build should discover source files beneath `src`, but verify the generated build list before relying on that. The current report remains at the project root as requested.

## 10. Reuse versus independent implementation

| Item | Recommendation |
|---|---|
| `.project`, `.cproject`, `.settings`, launch file, linker scripts | Preserve for the first implementation phase |
| Existing startup/runtime/SystemInit | Reuse; keep their non-CAN platform dependencies explicit |
| NXP device register/CMSIS definitions | Reuse as hardware declarations if permitted by the applicable license; this retains a declared SDK/header dependency |
| Existing Clock_Ip initialization and clock config | Reuse initially as a platform service; verify the final engine clock. Replace only if full SDK independence becomes a requirement |
| Current frame, ID, MB assignments | Reuse as an initial loopback test vector; add content validation |
| Timing values | Reuse as candidate values only after final-clock validation; convert raw encodings to the new physical-unit config |
| Existing RTD source | Use as evidence of current behavior; implement the new CAN state machine and register sequencing independently rather than depending on RTD helpers |
| Generated CAN state/config and feature macros | Keep existing files intact, but do not make the independent CAN core depend on them |
| FlexCAN_Ip APIs, legacy FLEXCAN_DRV APIs, IRQ service/state | Replace in the future application CAN path; none is acceptable as the independent backend |
| OsIf/SchM/DET in CAN path | Replace with standard types, explicit single-caller rules, status returns, and a platform monotonic timer |
| CAN pin mux and SBC/transceiver setup | Implement independently: no working board implementation was found to reuse |
| TX/RX completion, acceptance masks, byte packing, error recovery | Implement and verify independently |
| Build outputs | Preserve, but do not treat them as reusable source or verification results |

Retaining unused RTD source files/build entries initially is possible if the project remains otherwise unchanged, but **only one driver may own CAN0 at runtime**. Future integration must stop calling the old CAN init/IRQ setup and ensure old handlers cannot run against the new driver's hardware state. Removing RTD files or changing project configuration is outside this analysis and requires a separate implementation decision.

## 11. Proposed validation and unresolved questions

Before implementation, establish the real EVB revision/SBC, actual silicon mask, chosen normal-mode bitrate, intended standard/extended/RTR support, and whether independence applies only to CAN or to all SDK/platform code. The suggested initial target is CAN0, Classic data frames, polling, loopback first, then physical bus operation at a verified clock/bitrate.

Future validation should cover:

1. Read back clock source, timing, MAXMB, FD/FIFO state, masks, and disabled interrupt/DMA controls after init. Confirm all hardware handshakes terminate within deadlines.
2. Loopback the current test vector; compare ID, IDE/RTR, DLC, and every payload byte. Exercise DLC 0, 1, 7, 8 and identifier boundaries; reject DLC above 8 and invalid IDs.
3. Test flag clearing with unrelated flags pending, TX busy/late completion, empty RX, RX overrun, stop/start, and safe failed-init cleanup. Review byte ordering with nonsymmetric data.
4. On the actual board, configure verified pins/transceiver and test with an external CAN peer/analyzer at the confirmed bitrate, proper power/termination, and shared reference ground.
5. Test missing acknowledgment, bus-off, recovery policy, and polling delays under traffic. Demonstrate that CAN traffic completes without CAN NVIC interrupts or DMA.
6. Use focused host tests for pure timing/encoding logic and state transitions; use hardware tests for lock/unlock, write restrictions, errata, and bus behavior that mocks cannot establish.

**Current verification result:** static analysis completed; no hardware success claim, rebuild claim, or measured bitrate claim is made. Only this report was added.
