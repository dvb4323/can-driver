# Phase 4A implementation report

## Delivery and validation status

Implemented the two-ID software protocol, bounded foreground application, deterministic mock and host test sources. The saved Phase 3 application is the default build path. **Phase 4A has not been compiled, executed, flashed or hardware-tested in this task.** The user's previously verified Phase 3 physical CAN result is baseline evidence only.

The seven-ID Phase 4 draft is not the wire contract for this implementation. See `CAN_Phase4A_Protocol_Spec.md` for the implemented 0x100/0x180 contract, result-code extensions, golden vectors and replay limitations.

Target Phase 4A execution is gated on an explicitly reviewed timebase. No established application millisecond service was found. The optional polling SysTick implementation is a mock bench candidate, not a qualified physical motor safety clock. The default leaves it disabled. Host tests inject time independently and do not acquire a timer.

## File-by-file changes

| File | Responsibility |
|---|---|
| `src/main.c` | Compile-time selector. Default includes the exact saved Phase 3 fixture. Opt-in Phase 4A initializes existing clocks, reviewed time source, board pins, wrapper NORMAL mode with RX 0x100, then calls one application step per loop |
| `regression/phase3_smoke_main.inc` | Byte-preserved pre-change `main.c`; its own original `CAN_TEST_LOOPBACK` selection remains available. The `.inc` suffix prevents accidental independent compilation/duplicate main |
| `protocol/can_protocol.h`, `.c` | Pure wire types, ID/opcode/value/length validation and explicit byte encoding. No RTD, CAN driver or motor dependencies |
| `interfaces/motor_if.h` | Bounded motor-domain operations and snapshots; no SPI/PWM types. Interface copied by application, context must remain alive. START confirmation is asynchronous in this mock contract |
| `backends/motor_mock.h`, `.c` | Simulated safe-off, async START/STOP confirmation, duty, reverse capability and injected fault/delay controls. No motor hardware writes |
| `application/motor_app.h`, `.c` | State machine, lease, STOP priority, deduplication, bounded ACK queue, async completion reservation, TX ownership, CAN fault policy, coalesced status and `g_motor_app` diagnostics |
| `platform/app_time.h`, `.c` | Callback-based millisecond source; optional, explicitly gated polling SysTick source for mock bench work |
| `tests/phase4a/test_phase4a.c` | Actual codec/application/mock/time abstraction linked to a fake CAN wrapper and injected time, without RTD or peripherals |
| `tests/phase4a/run_tests.ps1` | User-operated native Windows host compile/test runner; separate from the ARM firmware build |
| `CAN_Phase4A_Protocol_Spec.md` | Complete implemented software wire/state/sequence/timing contract and golden vectors |
| `CAN_Phase4A_Implementation_Report.md` | This report and manual integration/validation procedure |

No changes to `RTD/`, `generate/`, `drivers/can/`, `board/`, `.mex`, `.cproject`, startup/linker files or SPI/motor/GUI implementations are required or made. New source/include paths must be added manually through S32DS before opting into Phase 4A. Old Phase 1–4 reports remain historical; their application descriptions may differ from the current saved Phase 3 fixture.

## Verified checkout facts and integration decisions

- Existing `CanDriver_Init(const Can_Config *)`, `Start`, `Transmit`, `PollTx`, `PollRx`, `GetState`, `PollDiagnostics` and cached `GetDiagnostics` are reused without API changes. Application code never invokes FlexCAN RTD or accesses CAN registers.
- CAN0 remains owned once by the wrapper, with TX MB0 and exact-ID RX MB1. Phase 4A needs only one command RX ID. No RX MB expansion, generated configuration change, CAN IRQ or DMA addition was made.
- `Can_Frame` contains ID/DLC/eight data bytes. The wrapper rejects unsupported metadata; the standalone codec also accepts explicit extended/remote flags for independent testing.
- `CAN_STATUS_RX_OVERRUN` supplies a valid frame and indicates lost history. The application honors a fully valid STOP in that frame, then inhibits/faults; it never executes an overrun motion command. Wrapper INVALID_FRAME and other API failures also inhibit/fault.
- TX success from `Transmit` is submission, not completion. A private frame remains unchanged while PollTx returns BUSY. PollTx OK releases it. Unexpected NO_TRANSFER or another failure latches CAN_API fault and preserves the pending record; no unsafe cancel/reinitialization is attempted. Permanent BUSY cannot starve RX/lease/backend service.
- PollDiagnostics is the only application request to sample hardware diagnostics; GetDiagnostics copies the wrapper cache. Existing ESR1 side-effect handling stays inside the unmodified wrapper. First fault diagnostics and the wrapper's sticky bus-off history are preserved. Bus-off or CAN API faults require reset, even if the hardware later recovers.
- Current `generate/include/OsIf_Cfg.h` sets OSIF_USE_SYSTEM_TIMER to STD_OFF. No active application SysTick owner was found in the inspected checkout. This is a source observation, not a guarantee about future linked modules or debugger behavior.
- The installed RTD has `Clock_Ip_GetClockFrequency(CORE_CLK)` and the installed device header exposes `S32_SysTick->CSRr/RVR/CVR`. The optional implementation uses these actual names. It refuses an already enabled SysTick or TICKINT owner and compile-fails if OSIF system-timer support is enabled.
- Existing generated PORTE clock gate entry is enabled in `generate/src/Clock_Ip_Cfg.c`. New main relies on that existing initialization and board verification; on failure it holds at stage 91. The saved Phase 3 fixture retains its original PORTE gate fallback unchanged. New main does not add clock writes to work around a failed board check.

## Application contracts and review limits

Each Process turn checks time/lease and CAN diagnostics, handles at most one RX, services the backend, then polls/submits at most one TX. A received STOP cancels a pending mock START before backend completion is serviced. Safety actions never wait for a response to transmit. Startup failure holds are diagnostic holds with an inhibited mock, not runtime CAN polling waits.

The ACK ring has eight slots. One async operation reserves its terminal response slot; accepted START has PENDING followed by matching COMPLETED, cancellation or failure. Status is coalesced at 100 ms, and fault events have priority over ACK then status. Error repeats use a 1000 ms schedule. Current pending MB0 cannot be preempted. Queue overflow inhibits and latches a fault.

Eight dedup records store exact validated requests and current result/fault. Terminal records expire at 1000 ms and may be evicted earlier under capacity pressure; pending records are pinned. Retries replay without action or lease renewal. A shared modulo-u8 sequence stream accepts forward deltas 1..127. STOP bypasses stale/conflict rejection after full validation. Result 0x88 CAN_ERROR, 0x89 INTERNAL_TIMEOUT and 0x8A STALE_SEQUENCE are explicit documented extensions. See the protocol spec for cache/reset/replay assumptions; no session/replay protection is claimed.

The optional target clock extends a polling 24-bit core-clock SysTick downcounter. At 48 MHz its wrap interval is approximately 349.525 ms; **every sample gap must be shorter than one wrap**. It cannot detect missed whole wraps or qualify elapsed time across debugger halts. Clock changes while active are unsupported. Frequency must be an integer number of ticks/ms, 1 kHz..100 MHz. Register ownership/readback failures return invalid time and latch a reset-required time fault. Those checks do not prove that a long scheduling stall did not occur. Keep all timing and backend claims limited to a reviewed mock bench. A production watchdog/timebase remains unresolved.

The compiled default is `APP_PHASE4A=0`. With `APP_PHASE4A=1` but no reviewed source, boot stage 90 and TIME fault are expected **before CAN initialization**. `APP_TIME_SYSTICK_REVIEWED=1` is an explicit opt-in only after review of exclusivity, service gaps and debug behavior. A hardware-independent time callback is also available for future approved integration. Physical interfaces with `is_mock=false` are rejected; no physical backend is delivered.

## Tests and checks: actual versus pending

| Check | Status in this task |
|---|---|
| Source/API/configuration inspection | Performed; existing wrapper and installed timer names reviewed |
| C grammar parsing of new modules/main/test harness with preprocessor removed and external types stubbed | Passed; syntax aid only, not compiler/type/link validation |
| Protected-file/fixture hashes | Compared all 132 baseline files: only `src/main.c` changed; saved Phase 3 fixture matches the original SHA-256 exactly |
| Whitespace and PowerShell runner syntax | Static checks passed; runner was parsed without launching its compiler/test commands |
| Host test compiler | Windows GCC found at `C:\msys64\ucrt64\bin\gcc.exe`; compiler was not invoked |
| Host test suite | Written, **not compiled or executed**; pending user UI invocation |
| Existing driver/Phase 3 tests | Not run. Driver unchanged; some historical application tests refer to earlier main variants and are not evidence for this delivery |
| S32DS complete firmware build/link | **Not run**, per user-operated UI workflow; source-path/symbol setup still required |
| Target timer accuracy, debug behavior, new protocol CAN exchange | **Not tested** |
| Physical motor/SPI/PWM operation | Not implemented or tested |

Host assertions cover golden byte vectors/LE/IDs/DLC/reserved/opcodes; all seven named transitions plus STOP; configuration/zero cap/reverse/max guards; duplicate PENDING/COMPLETED/conflict/wrap/stale and nonrenewing retries; STOP during pending START/TX and late completion; permanent BUSY with lease expiry; backend deadline; malformed frames; overrun/invalid wrapper frames; bus-off/API failures; reply queue overflow; fault-clear guards; injected time/wrap/backward time; reset and physical-backend rejection. These describe test source coverage, not successful test execution. The Phase 3 wrapper regression still requires its existing on-target/UI checks.

Original/saved fixture SHA-256: `2D6CFF219592EAC26F823A59E01FC9E4DDA07A671EC1DC92B0E383F247560FF5`. Fourteen new files were added, including this report; no other pre-existing file changed.

## User-operated S32DS and host validation

### 1. Preserve and build the regression first

1. Refresh the project in S32DS (select project, F5). Keep the existing active build configuration/toolchain/linker/startup.
2. In Project Properties → C/C++ Build → Settings → compiler preprocessor symbols, leave `APP_PHASE4A` absent or set to `0`. Avoid conflicting duplicate definitions.
3. Project → Build Project. The default main includes the saved smoke fixture; it does not require the new include/source roots. Inspect the Console for compile/link success, not just an old ELF remaining on disk.
4. For normal regression leave the fixture's `CAN_TEST_LOOPBACK` absent/0: TX 0x123, RX 0x321, `g_can` diagnostics. To regress loopback set `CAN_TEST_LOOPBACK=1`, rebuild, and use its 0x14 path. The fixture intentionally preserves its original payload-counter and polling/delay behavior.
5. Use the existing S32DS debug configuration only when ready to run together. Inspect `g_can.init_status`, `start_status`, `tx_accepted`, `tx_done`, `rx_count`, `last_rx` and diagnostics. This new task does not establish regression PASS until actually run.

### 2. Run host tests from the UI before Phase 4A target integration

Keep `tests/phase4a` out of embedded source paths. It contains a different host main and fake wrapper symbols.

Open Run → External Tools → External Tools Configurations → Program → New. This is a native Windows host build/test action, not the ARM firmware build:

- Location: `C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`
- Working directory: `${workspace_loc:/FlexCAN_Ip_example_S32K144}`
- Arguments:

```text
-NoProfile -ExecutionPolicy Bypass -File "${workspace_loc:/FlexCAN_Ip_example_S32K144/tests/phase4a/run_tests.ps1}" -Compiler "C:\msys64\ucrt64\bin\gcc.exe"
```

Enable console output and run it manually. The runner creates `tests/phase4a/build/phase4a_tests.exe`, compiling C99 with Wall/Wextra/Werror/pedantic before running assertions. Use native Windows GCC, not ARM GCC. If the local GCC runtime DLLs are unavailable, add `C:\msys64\ucrt64\bin` to this launch's PATH using the Environment tab; preserve the existing PATH. Capture compiler output and exit status. Expected final line only after all assertions really pass:

```text
PASS: Phase 4A codec, mock state/lease/dedup/STOP/queues/CAN faults/time/reset contracts
```

Do not infer PASS from the presence of that string in the source. Share failures before enabling target integration.

### 3. Add Phase 4A build paths through S32DS

After host validation and timer review, in Project Properties → C/C++ General → Paths and Symbols → Source Location, add these project folders without removing current entries: `protocol`, `application`, `backends`, `platform`. Equivalent Source Folder creation is acceptable if it results in those four managed-build source entries. Do not add the whole project root, `tests` or `regression` as source roots.

In C/C++ Build → Settings → Standard S32DS C Compiler → Includes (wording may vary with installed plugin), append:

```text
${ProjDirPath}/protocol
${ProjDirPath}/application
${ProjDirPath}/interfaces
${ProjDirPath}/backends
${ProjDirPath}/platform
```

Keep existing `drivers/can/inc`, `board`, RTD/generated paths and compiler settings. Confirm that the source list includes the four new `.c` modules once, plus `src/main.c` once. The interface directory has headers only. Set `APP_PHASE4A=1`; only after the timer dependency is explicitly reviewed set `APP_TIME_SYSTICK_REVIEWED=1`. Do not change CAN_TEST_LOOPBACK to select Phase 4A: the new app explicitly uses NORMAL; that symbol belongs to regression.

Apply, Refresh, then Project → Build Project. Check all four source compilations and final link, including flash/RAM sizes and absence of duplicate main/SysTick symbols. No generated configuration editor action is needed. If existing make dependency tracking misses the saved include/profile switch, use Project → Clean for this project followed by Build from the UI. Preserve the console output as build evidence.

### 4. Debug the mock protocol on the existing CAN bench

Use the already verified external CAN wiring/logic compatibility/isolation; another active node must acknowledge frames. Keep physical motor control disconnected/unavailable. Configure the ESP32 GUI for Classic CAN, 500 kbit/s, standard frames, DLC 8, TX ID **0x100** and RX ID **0x180**. Verify whether its ID/byte fields use hex or decimal (IDs are 256/384 decimal). Do not send old 0x123/0x321 packets as protocol commands.

Load only the newly successful ELF using the existing S32DS Debug configuration, then inspect:

| Expression | Expected initial/running observation |
|---|---|
| `g_phase4a_boot_stage` | 6 when startup succeeded; 90 unreviewed/unavailable timer, 91 board, 92 CAN Init, 93 Start, 94 time read failure |
| `g_phase4a_board_status`, `g_phase4a_can_status` | BOARD_CAN_OK and CAN_STATUS_OK after startup |
| `g_motor_app.state` | UNCONFIGURED (0), then STOPPED (1), STARTING (2), RUNNING (3) only through explicit commands |
| `mock_backend`, `safe_off_confirmed`, `requested_duty`, `configured` | Initially true, true, 0, false |
| `now_ms`, `time_valid`, `lease_valid` | Advancing/true; lease initially false until accepted refreshing command |
| `tx_accepted`, `tx_completed`, `tx_pending`, `pending_tx` | Submissions and completions distinct; stable pending frame during BUSY |
| `rx_count`, `last_command`, `protocol_errors`, `duplicates`, `sequence_rejections` | Increase for the corresponding received/test events |
| `fault`, `first_fault`, `fault_mask`, `first_fault_ms` | Zero initially; first failure retained after fault clear |
| `diagnostics`, `first_fault_diagnostics`, `queue_overflows` | Cached CAN evidence and preserved first-fault evidence; zero overflow in normal use |

Default startup status is `02 00 00 0E 00 00 00 01` (unconfigured, safe, mock, reverse capable, no lease). No board LED behavior is added by this phase. Avoid breakpoints/single-step while judging real-time lease behavior. A debug halt can invalidate timing assumptions; reset/rearm after halted experiments rather than treating resumed timing as safety evidence.

Manual raw-frame exercises (all ID 0x100, DLC 8):

1. Fresh reset: query `01 05 00 00 00 00 00 00`; expect ACK OK plus UNCONFIGURED status. START `02 01 00 00 00 00 00 00` gives NOT_CONFIGURED and no enable.
2. Set cap `03 04 BC 02 00 00 00 00` (700 permille). Expect STOPPED and OK. Configuration alone does not enable.
3. For manual testing after any pause, send fresh heartbeat `04 06 00 00 00 00 00 00`, then START `05 01 00 00 00 00 00 00` within 500 ms. Expect PENDING followed by COMPLETED and mock RUNNING at zero duty. **Human typing alone is not a reliable way to meet this interval.** A GUI script/burst with controlled increasing sequence is needed for sustained motion simulation.
4. Use one shared increasing sequence across all commands, including an automatic fresh KEEPALIVE every 100 ms. The next sequence may send SET_DUTY argument 300 (`2C 01`); expect requested duty 300, no measured speed field. Do not cyclically retransmit an identical heartbeat: duplicates do not refresh.
5. Send a fully valid STOP using any sequence and zero argument/reserved bytes, for example `00 00 00 00 00 00 00 00`. Inhibit/duty zero takes priority; expect OK if already safe or PENDING then COMPLETED. FAULT remains latched if present. This stale STOP does not advance the normal sequence baseline.
6. Disable heartbeat while STARTING/RUNNING. At lease age >=500 ms expect demand zero, FAULT/COMMUNICATION (1), then safe-off confirmation. New heartbeat never restarts. To clear, use a sequence newer than the last consumed sequence for KEEPALIVE, then another newer sequence for CLEAR_FAULT within 500 ms; expect STOPPED with duty zero. START must be deliberate again.
7. Re-send identical retained START: cached result, no new enable action/lease renewal. Reuse the same sequence with different valid bytes: 0x86. Unknown opcode: 0x80; nonzero reserved: 0x81; SET_DUTY 701 above cap: 0x81. Inspect diagnostics and preserve first fault if bus-off/API failure occurs; reset-required faults do not clear by command.
8. Reboot/reconnect: verify UNCONFIGURED, zero demand and no automatic motion. GUI must discard queued motion, STOP/query, then deliberately configure/rearm. If the GUI cannot generate fresh sequenced periodic traffic, limit manual testing to stopped/configuration/NACK tests and the expected communication-timeout test.

## Remaining agreement and integration dependencies

- User-run native host test result, S32DS target compile/link evidence, then new mock-protocol bench evidence are all pending. Do not migrate the default from Phase 3 until those are reviewed.
- Timer ownership and worst-case service gap, debugger halt policy, clock changes, production monotonic source and independent watchdog need agreement. The polling SysTick candidate cannot qualify physical communication-loss deadlines.
- SPI/PWM team must supply bounded start/stop/inhibit/fault semantics, cancellation and late-completion rules, fault-clear policy and actual reverse capability. The Phase 4A mock interface is a starting contract, not proof that a real bridge can honor it.
- No MC33HB2000 features, braking/coasting behavior, current/thermal/voltage limits, approved duty cap, ramp, deadtime or physical stopping deadline are assumed. Require actual circuit/datasheet/measurement evidence and an independent inhibit/emergency-stop path.
- Real backend review must resolve scheduler ownership, SPI completion/errors, PWM timer ownership, startup/fault pins, resource conflicts and status/feedback provenance. Public protocol requested duty must never be relabeled measured speed.
- Before physical enable, agree a session/rearm/replay policy, stale RX treatment after reconnect, command source ownership, overload/queue policy and appropriate safety envelope. No physical backend is selectable merely by a build macro in this delivery.
- GUI transport remains external. ESP32 raw GUI is a test tool; future PCAN/Python mapping must preserve the same wire codec, sequence/lease behavior and explicit rearm rather than replaying START automatically.
