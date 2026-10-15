# CAN Driver Phase 1 Report

Date: 2026-10-15

## Result

Implemented and built an application-facing CAN0 adapter over the existing NXP RTD FlexCAN IP 1.0.1. No register-level driver was implemented. Original application, RTD, generated configuration, clocks and platform source remain unchanged. The full project compiled/linked with the adapter included; host adapter-contract tests passed. No target execution, flashing, runtime interrupt observation or hardware test was performed.

The new adapter execution path uses polling for both TX and RX. **The whole application is not polling-only:** unchanged `main.c` still runs the original interrupt-TX/polled-RX example and does not invoke this adapter.

## Changed files

| File | Change |
|---|---|
| `drivers/can/inc/can_types.h` | Standard-C frame/config/status/lifecycle types, no RTD types |
| `drivers/can/inc/can_cfg.h` | Fixed CAN0/MB0/MB1 baseline, default RX ID and descriptive clock/bitrate constants |
| `drivers/can/inc/can_driver.h` | Public lifecycle, request and polling API contracts |
| `drivers/can/src/can_driver.c` | RTD-only adapter backend, private transfer buffers, validation, repeated RX registration and ownership/fault handling |
| `.cproject` | C include path `${ProjDirPath}/drivers/can/inc`; source entry `drivers/can/src` |
| `CAN_Driver_Design.md` | HLD, API/state-machine, storage and ownership contracts |
| `CAN_Driver_Phase1_Report.md` | This report |
| `tests/can/test_can_driver.c` | Host lifecycle/polling-contract tests |
| `tests/can/fakes/*.h` | Host-only RTD/platform doubles, never on target include paths |
| `tests/can/run_tests.ps1` | Optional host compile/run helper |
| `Debug_FLASH/` | S32DS regenerated make/response/dependency files and rebuilt output; not hand-edited |

S32DS also rewrote scanner/debug-launch metadata during import. Those incidental changes were restored to their exact pre-implementation SHA-256 values. Final hash checks cover `RTD/`, `generate/`, `src/main.c`, existing clock/platform inputs and the earlier reports. Only the intended `.cproject` source/include additions remain outside new files and build outputs.

The repository currently ignores `.cproject`, so a later commit/export must explicitly include that build configuration change or reproduce the two settings in S32DS. No Git commit or ignore-rule change was made.

## RTD source inspected before implementation

Project-local sources/headers were inspected for actual executable behavior, not just API comments. The following project-local files were also SHA-256 compared with the installed package and matched exactly:

- `RTD/src/FlexCAN_Ip.c`
- `RTD/src/FlexCAN_Ip_HwAccess.c`
- `RTD/include/FlexCAN_Ip.h`
- `RTD/include/FlexCAN_Ip_Types.h`
- `RTD/include/FlexCAN_Ip_HwAccess.h`

Installed counterpart root:

`C:/NXP/S32DS.3.4/S32DS/software/PlatformSDK_S32K1_2022_02/SW32K1_RTD_4_4_1_0_1_D2202/Can_TS_T40D2M10I1R0/`

### Exact key signatures present in this checkout

```c
Flexcan_Ip_StatusType FlexCAN_Ip_Send(
    uint8 instance, uint8 mb_idx,
    const Flexcan_Ip_DataInfoType *tx_info,
    uint32 msg_id, const uint8 *mb_data);
void FlexCAN_Ip_MainFunctionWrite(uint8 instance, uint8 mb_idx);
Flexcan_Ip_StatusType FlexCAN_Ip_GetTransferStatus(uint8 instance, uint8 mb_idx);
Flexcan_Ip_StatusType FlexCAN_Ip_Receive(
    uint8 instance, uint8 mb_idx,
    Flexcan_Ip_MsgBuffType *data, boolean isPolling);
void FlexCAN_Ip_MainFunctionRead(uint8 instance, uint8 mb_idx);
```

### API mapping and state evidence

All line references below are to unchanged project-local RTD files.

| Adapter concern | Verified RTD source/header and behavior |
|---|---|
| Initialization | `FlexCAN_Ip.h:212`, wrapper header `:143`, `FlexCAN_Ip.c:1876–1962`: generated config/state passed to Init; state pointer published only on success at 1960. |
| TX request | `FlexCAN_Ip.h:226`; `FlexCAN_Ip.c:1974–2003,1054–1087`: `is_polling=TRUE` bypasses the interrupt-enable branch; successful request sets TX_BUSY, writes frame. |
| TX lifetime | `FlexCAN_Ip.c:1068–1087`, `FlexCAN_Ip_HwAccess.c:1447–1553`: metadata/payload copied synchronously. Payload not retained as an async pointer. Adapter uses aligned static TX storage anyway. |
| TX polling service | `FlexCAN_Ip.h:456`; `FlexCAN_Ip.c:3615–3657`: tests raw flag, processes timestamp/unlock, clears flag, changes TX_BUSY -> IDLE; callback would run if configured, but generated callbacks are null. |
| Transfer status | `FlexCAN_Ip.h:567`; `FlexCAN_Ip.c:2546–2598`: reads software state only, returns SUCCESS for IDLE and BUSY otherwise on this DMA-off MB path. It does not call MainFunctionWrite or clear completion. Unsafe before initialized state pointer exists. |
| RX request | `FlexCAN_Ip.h:266`; `FlexCAN_Ip.c:975–1007,2077–2101`: stores destination pointer, RX_BUSY and polling flag; true bypasses MB IRQ enabling. |
| RX polling service | `FlexCAN_Ip.c:3513–3568,1258–1361`: raw-flag service reads metadata/data, clears flag/unlocks and sets software IDLE. Does not automatically register another operation. |
| Repeated receive | `FlexCAN_Ip.c:1005–1007,1334`: adapter calls Receive(...,TRUE) after each completed service. ConfigRxMb is not repeated per frame. |
| RX stop cleanup | `FlexCAN_Ip.c:4460–4492,1810–1871`: AbortTransfer on RX MB1 uses RTD RX abort, sets software IDLE, reconfigures RX CODE and clears flag. No TX abort is used. |
| Start/restart | `FlexCAN_Ip.c:3695–3710,3718–3738`: Start enables module/exits Freeze; Stop disables it. Adapter calls Start before RX mailbox access, including restart after Stop. |
| MB interrupt suppression | `FlexCAN_Ip.c:4331–4356`; `FlexCAN_Ip_HwAccess.c:1191–1210`: DisableInterrupts clears MB masks and sets isIntActive=false; Send/Receive polling avoid mask-enable calls at IP:2000/2098. |
| NVIC handling | `IntCtrl_Ip.c:262,360`; `IntCtrl_Ip.h:193`: use DisableIrq/ClearPending only for CAN0 IRQ78..82; no ISR installation or enable. |
| Deinit limitation | `FlexCAN_Ip.c:3472–3502`: performs hardware cleanup even if Init did not publish state; internal Disable return is discarded at 3488. Adapter additionally checks GetStopMode (IP:3669–3673) before declaring UNINITIALIZED. |
| Timeout semantics | `generate/include/FlexCAN_Ip_Cfg.h:136–139`, `RTD/src/OsIf_Timer.c:198–220`: configured duration10000 uses dummy counter (counter0, elapsed1, conversion identity), not elapsed microseconds. |

**Safe RTD TX polling completion exists; no workaround or RTD-internal modification was necessary.** GetTransferStatus alone is insufficient. MainFunctionWrite must service completion before status becomes SUCCESS. The new adapter does exactly that.

`FlexCAN_Ip_HwAccess.h` is used privately for RTD CS bit masks and RX CODE enum values, solely to interpret the returned message structure. There are no direct CAN/PCC/SCG/PORT/NVIC register writes or vendor hardware-helper calls in the adapter.

### Return codes and failed operations

`RTD/include/FlexCAN_Ip_Types.h:296–304` defines SUCCESS, ERROR, BUSY, TIMEOUT, BUFF_OUT_OF_RANGE and NO_TRANSFER_IN_PROGRESS. These map respectively to public OK, HW_ERROR, BUSY, TIMEOUT, CONFIG_ERROR and NO_TRANSFER. Expected polling BUSY remains normal. Unexpected RTD failures enter wrapper FAULT, including BUSY during Send or receive re-registration. The public state query disambiguates that from ordinary pending TX.

Failed Init enters FAULT without transfer/status calls. Explicit Deinit attempts recovery; failure retains FAULT. Stop rejects pending TX instead of aborting it. This avoids the inspected TX abort path (`FlexCAN_Ip.c:1797–1798`), which marks software idle even after an abort timeout. No busy mailbox is released merely because an application deadline might have elapsed.

## Build evidence

Available toolchain: installed NXP GCC 9.2 Arm compiler and S32DS 3.4 headless managed builder. ARM compilation uses the existing C99/Cortex-M4/Thumb/hard-float/newlib-nano flags; no clock, linker, MCU, optimization or generated peripheral setting was changed.

S32DS headless import/build used an isolated workspace outside the project:

```text
C:/Users/Admin/.codex/visualizations/2026/10/15/01a13e70-42a2-7401-b318-cf0569af9b96/s32ds-phase1-workspace
```

The importer requires a `file:/C:/...` URI. It regenerated `Debug_FLASH/drivers/can/src/subdir.mk` and `can_driver.args` automatically, confirming source/include discovery. The isolated workspace did not resolve the existing `${PLATFORMSDK_S32K1_2022_02_S32K144_M4F_1.0.1_PATH}` substitution. An initial managed compile failed on Mcal.h/StandardTypes.h; setting an OS environment variable of that name alone did not resolve this Eclipse substitution. These failures were environment failures, not successful build evidence.

The final **full project build succeeded** by running the S32DS-regenerated makefiles with GCC's `C_INCLUDE_PATH` supplying the installed SDK directories, without altering project include settings or source:

```powershell
$sdkBase = 'C:/NXP/S32DS.3.4/S32DS/software/PlatformSDK_S32K1_2022_02/SW32K1_RTD_4_4_1_0_1_D2202'
$env:C_INCLUDE_PATH = "$sdkBase/Base_TS_T40D2M10I1R0/header;$sdkBase/Base_TS_T40D2M10I1R0/include;$sdkBase/Platform_TS_T40D2M10I1R0/include;$sdkBase/Platform_TS_T40D2M10I1R0/startup/include"
$env:Path = 'C:/NXP/S32DS.3.4/S32DS/build_tools/gcc_v9.2/gcc-9.2-arm32-eabi/bin;C:/NXP/S32DS.3.4/S32DS/build_tools/msys32/usr/bin;' + $env:Path
# From project/Debug_FLASH:
& 'C:/NXP/S32DS.3.4/S32DS/build_tools/msys32/usr/bin/make.exe' -j4 all
```

Actual final result: exit code **0**, target ELF linked, size output:

```text
text    data    bss    dec    hex
32216   56      6752   39024  9870
```

Artifacts: `Debug_FLASH/FlexCAN_Ip_example_S32K144.elf` and `.map`, plus `Debug_FLASH/drivers/can/src/can_driver.o`. Map symbols include all eight CanDriver public functions (for example Init at 0x73C, PollTx at 0x8D8, PollRx at 0x91C). This proves adapter code is compiled and linked, not called by main.

One linker warning remains: newlib-nano `memset` uses variable-size enums while the project output uses 32-bit enums. No warning was emitted for adapter C compilation. This report does not claim a warning-free build. S32DS startup also printed missing GHS toolchain manifest messages for unrelated toolchain definitions.

The first sandboxed ARM compiler launch failed with Windows DLL relocation; the same installed compiler ran successfully outside the sandbox. Initial launcher/SDK-path attempts were retried after diagnosing their errors. No build validation is blocked at delivery. The isolated workspace still lacks the Eclipse SDK substitution, so repeat builds need the environment workaround above or the user's normally configured workspace. No source-only success or stale ELF is substituted for the final full build.

## Host validation

Host GCC: `C:/msys64/ucrt64/bin/gcc.exe`, using `-std=c99 -Wall -Wextra -Werror -pedantic`. Compiled the actual adapter source against explicit host-only RTD/platform doubles and ran the resulting executable; exit code0:

```text
PASS: lifecycle, validation, TX ownership/completion, RX re-registration, rejection/overrun, fault recovery
```

Tests cover invalid/null arguments, no transfer before request, no repeated Send while pending, Stop refusing pending TX, completion consumed once via TX service, caller payload reuse, DLC0/7/8, standard-ID upper boundary, repeated RX re-registration, no-data output preservation, overrun output, unsupported frame/raw DLC rejection, restart ordering, failed-init recovery, receive re-registration failure, unexpected RTD Send BUSY, TX status failure and failed deinit acknowledgment.

The optional `tests/can/run_tests.ps1` helper was not used successfully on this host because PowerShell script execution policy blocked it. Equivalent compile and execute commands were run directly without changing policy. For reproduction from project root:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Itests/can/fakes -Idrivers/can/inc drivers/can/src/can_driver.c tests/can/test_can_driver.c -o can_driver_tests.exe
./can_driver_tests.exe
```

Actual host test executable was placed in the task's visualization workspace, not an embedded source directory. The target build separately checked compatibility with the **real** RTD headers/objects. The doubles validate wrapper contracts only; they do not emulate FlexCAN, verify actual interrupt masks, reproduce silicon races/errata or prove real RTD runtime execution.

## Assumptions, limitations and Phase 2 work

- Exclusive CAN0 ownership is a caller precondition. The RTD exposes no safe public ownership query; its GetTransferStatus dereferences the state pointer and cannot be used as a pre-init probe. Phase 1 does not call the adapter anywhere, so it cannot initialize simultaneously with the original example.
- Timing remains generated and hardware-unverified: intended SYS_CLK/PE48MHz, derived500kbit/s. Adapter constants do not set clocks or validate FIRC tolerance. No pin mux, SBC or physical bus changes were made.
- Repeated foreground RX is implemented, with one hardware mailbox and no software queue. Poll regularly; lossless continuous traffic is not guaranteed. Re-registration failure discards the just-completed frame and enters FAULT.
- No calibrated deadline, public TX cancellation, bus-off recovery/diagnostic policy, runtime filtering changes, extended/remote/FD support, protocol or motor-control change is provided. A missing TX completion can remain BUSY indefinitely; do not issue another request or assume a timed-out transmission did not occur.
- No RTD internals were changed to add a BUSY retry or modify low-level RX sequencing. Vendor hardware behavior and applicable silicon errata remain dependencies of the approved reuse architecture.
- Next integration must replace the original main CAN IRQ/direct-RTD ownership sequence, then demonstrate checked loopback TX/RX through this API with CAN interrupts disabled. Until then, claims are limited to build/link and host adapter-contract validation.
