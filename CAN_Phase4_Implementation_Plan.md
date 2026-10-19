# Phase 4 requirements, LLD work and implementation plan

Date: 2026-10-19. **Planning only.** This task creates three Markdown documents and implements no driver, application, motor backend or GUI code. Proposed wire contract is in `CAN_Protocol_Specification_Draft.md`; source evidence and architecture are in `CAN_Phase4_HLD.md`.

## 1. Verification baseline and change boundary

- User confirms Phase 3 physical CAN TX/RX with ESP32-S3. That result is accepted as user evidence, not an assistant-run test or motor verification.
- Current `main.c` is the compact `g_can` smoke test, default normal mode through `CAN_TEST_LOOPBACK=0`, 0x123 TX/0x321 RX, TX completion wait and rough delay loop. Older profile/main/test reports are historical. The SPI/PWM motor integration interfaces are absent from source and supplied ZIPs; do not invent or copy a presumed implementation.
- Preserve current `main.c`, `drivers/can/`, `board/`, `RTD/`, `generate/`, `.mex`, clocks, startup/linker and IDE settings during this design task. No build/debug/flash commands. Builds and board debugging remain user-operated through S32DS UI.
- Future multi-RX and thin-main changes require a reviewed implementation stage; document them without applying them. Vendor source/generated configuration should remain unchanged unless an independently reviewed timer/motor integration proves a new component is needed.
- Do not treat existing ELF/map/host-test outputs as proof of the proposed Phase 4 software. No new project tests were run here. Document-vector arithmetic/source consistency checks are not target validation.

## 2. Requirements and traceability

All new behavioral requirements below are proposed; hardware-specific release criteria remain gated by evidence.

| ID | Requirement | Design location | Acceptance cases |
|---|---|---|---|
| R01 | Reuse the RTD adapter as sole CAN0 owner; no application direct CAN registers/RTD or copied vendor source | HLD §§2–3 | T01–T03 |
| R02 | Preserve Classic standard data frames, polling TX/RX, stable buffers, MB0 and original one-RX regressions | HLD §3 | T02–T07 |
| R03 | Seven explicit IDs and complete DLC/offset/unit/endian/value/reserved rules | Spec §§2–9 | T08–T13 |
| R04 | Freshness, duplicate/collision handling and shared command sequence must not repeat motion or refresh lease on retries | Spec §3 | T14–T17 |
| R05 | Session change/reset rejects stale START/DEMAND and clears staging/caches without automatic rearm | Spec §§2,10 | T18–T20 |
| R06 | Staged complete configuration, canonical CRC, atomic apply and backend failure handling | Spec §§4–5 | T21–T27 |
| R07 | Startup/config/START default to drive inhibited/zero demand; START requires approved configuration/interlocks | Spec §10; HLD §§4–5 | T28–T31 |
| R08 | STOP preempts queues/backend operations; no CAN Stop/Deinit or pending-TX cancellation needed | Spec §3; HLD §§4–6 | T30–T33 |
| R09 | Enforce a real monotonic communication lease independently of CAN TX progress | Spec §10; HLD §6 | T34–T37 |
| R10 | Bus-off, RX loss, queue overflow, time faults and backend faults cause an independently effective safe action | HLD §§3,5–6 | T32–T39 |
| R11 | Explicit backend request/copy/completion generations, truthful capability/feedback validity, mock first | HLD §4 | T27,T31,T40–T42 |
| R12 | No unverified MC33HB2000 capability, protection threshold, braking/reverse or feedback assumption | HLD §§1,4,9 | T41–T43 |
| R13 | Bounded foreground work, fixed queues and telemetry scheduling; no TX BUSY/delay loops | HLD §6 | T04,T35,T38,T44 |
| R14 | ACK admission/result semantics distinct from adapter acceptance, CAN ACK and physical motion | Spec §9 | T26,T32,T45–T47 |
| R15 | Cache diagnostics and first failure; no polling debugger reads consuming ESR1 evidence | HLD §§1,3,6 | T07,T36,T39 |
| R16 | GUI codec/session independent of PCAN/ESP32; backend error/echo/timestamp normalization | Spec §12; HLD §8 | T48–T51 |
| R17 | Explicit safe disconnect/reconnect and host queue expiry; no automatic START or stale replay | Spec §§3,10,12 | T19,T20,T37,T50 |
| R18 | Unit/malformed/timeout/load/backend-fault/regression/hardware acceptance staged separately | This plan | All cases |
| R19 | No source/config/GUI/motor implementation during design; retain working baseline | This section | T01/document audit |
| R20 | Physical motor release blocked until SPI-team contracts, independent inhibit, timebase and operating envelope approved | HLD §9 | T43,T52 |

## 3. Implementation stages and release gates

### S0 — Review and freeze the draft

Agree the seven IDs, DUTY-only scope, zero-target START, direction latch, ACK result meanings, transaction bitmap/CRC, session uniqueness/reconnect procedure, STOP exception, heartbeat/lease and queue policy. Assign a protocol version owner and SPI/platform/GUI reviewers. Keep unresolved hardware values visibly TBD; do not substitute component headline ratings.

Deliver approved spec/HLD plus decision log and traceable byte vectors. No code required. **Exit:** no contradictory field/transaction/state meanings, explicit mock-versus-physical limits, and hardware gates owned by named teams. Changes to wire layout after this gate require version/vector updates on MCU and Python sides.

### S1 — Preserve regression and implement a pure codec/transaction core

After implementation authorization, archive/tag the current user-verified CAN smoke baseline without rewriting it. Build pure encode/decode, validators, command-sequence/session model, config canonicalization/CRC and transaction staging with injected time. Add independent Python codec vectors for parity; no GUI, CAN hardware or motor pins required.

Future files: `protocol/can_motor/inc/*`, `protocol/can_motor/src/*`, `tests/protocol/*`, later `gui/domain/protocol_codec.py` and tests. No new driver yet. Correct stale application-test fixtures that currently include old `main.c` only in a reviewed test-maintenance change; never silently count them as passing tests of the new smoke main.

**Exit:** byte-for-byte vectors, malformed input matrix, sequence/duplicate rules, atomic configuration tests and deterministic timeout tests pass. No structure-casting wire ABI or transport imports in codec/domain.

### S2 — Reviewed multi-RX extension inside the existing wrapper

Add proposed multi-channel API and private three-slot storage in the existing `drivers/can/` module. Preserve existing signatures/defaults for one-RX regression; do not copy it to another CAN driver directory. Generated max 16 and FIFO disabled already accommodate MB0..3, so no generated edit is expected.

Implement checked enable/freeze/filter/config/register/start sequence using actual RTD APIs identified in HLD, per-slot polling/rearm, overrun return and partial-start fault teardown. Extend Stop/Deinit handling to all registered RX slots and keep the one pending TX contract. Define maximum count/duplicate channel/ID validation before hardware access. No interrupt/DMA path or RTD internal patch.

**Exit:** wrapper host tests with realistic per-slot RTD fakes, API/header compatibility through user UI build, unchanged one-RX loopback and ESP32 physical regression, then three-ID polling/filter/rearm/overrun tests with motor disconnected/mock only. Source reasoning alone is not enough to pass this stage.

### S3 — Motor Interface, deterministic mock and state machine

Freeze interface types/contracts with SPI team before implementing a physical adapter. Implement mock capabilities/feedback/delays/failures, operation IDs and safe-off override. Implement state machine, zero-target START, direction policy, lease/session handling, bounded queues and scheduler using injected time. Expose a Phase 4 RAM diagnostic structure independent of historical `g_can` layout.

The mock must advertise itself and never write SPI/PWM/motor enable pins. Physical operating envelope stays absent. Proposed test-only completion delays may be configured in fake time (for example configure 10 ms, start 20 ms, safe-off 1 ms), explicitly labeled **simulation**, not physical guarantees. Add negative tests with longer/never-completing backend operations.

**Exit:** fault/STOP/late completion tests prove no restart or stale config publication, timeout checks work while TX is indefinitely BUSY, and no queue overflow allows motion. Software simulation is the acceptance scope.

### S4 — CAN + mock application integration on S32K144

Design a separate selectable entry/fixture before replacing smoke main in an approved integration. The final future `main` should only initialize platform safe state, clocks/time/motor mock/CAN/app and repeatedly call one bounded application step. Preserve smoke/loopback regression as an explicit separate build or source fixture with exactly one `main`/CAN owner selected; do not copy RTD sources.

Add source/include folders through S32DS UI as needed. Establish a reviewed CAN+mock time backend; measure loop latency and RX queue depth under admitted traffic. Keep motor disconnected/inhibited throughout. Exercise protocol with ESP32-S3, including stale traffic, wrong IDs and simulated backend faults. Record console, flashed ELF identity, raw CAN logs and RAM snapshots.

**Exit:** full protocol state/transaction/lease behavior verified on MCU using mock, CAN IRQs unused, poll loop deadline target measured under load, no motor operation claimed. A polled SysTick wrap workaround is not a physical motor safety qualification.

### S5 — Backend-independent Python GUI and PCAN-Basic adapter

Implement GUI domain/session controller on the same pure codec, then mock transport, planned PCAN-Basic adapter, and optional ESP32 bridge backend only after its serial/network framing contract exists. Preserve existing ESP32 GUI as an independent raw-frame test tool.

Prove heartbeat scheduling is independent of plot/widget work, STOP invalidates queued motion, ACK/NACK state/transaction correlation includes session, transport TX success is not a motor ACK, and disconnected/stale/simulated feedback is obvious. Confirm vendor DLL/channel/driver/bitness and Classic CAN API at installation time; do not force a transport library into protocol classes.

**Exit:** domain tests pass against fake/recorded frames; on-board motor-mock interoperability through ESP32/PCAN tested when equipment is available. PCAN hardware absence blocks only the PCAN hardware gate, not backend-neutral codec work.

### S6 — Physical motor backend integration (separate authorization)

Require the evidence/agreements below before touching motor control hardware. SPI/PWM team supplies the actual API/code package and schematic. Implement only the thin approved Motor Interface adapter, bounded service and independent inhibit path; do not move SPI internals into CAN/protocol/main.

Verify at safe controlled conditions with an approved bench procedure: startup/reset safe defaults, enable at zero demand, STOP despite CAN/SPI faults, sensors/scales/validity, permitted reverse policy and protection behavior. Choose numeric physical settings only from approved motor/board/component analysis. Scope/logic analyzer/independent cutoff evidence may be necessary; CAN logs alone cannot prove safe motor inhibition or current/shaft state.

**Exit:** verified hardware inhibit timing/confirmation, approved operating/protection envelope, trustworthy timer/watchdog, fault-clear policy and nonblocking SPI contract. Any missing item keeps physical START disabled. Mock and CAN success are not substitutes.

## 4. LLD deliverables before coding each component

| Component | Required LLD details |
|---|---|
| Wire codec | All seven eight-byte layouts, signed conversion, reserved checks, CRC canonical stream/reference vectors, malformed/truncated-frame order, error classification |
| Command/session manager | Sequence delta/collision window, duplicate cache, fresh lease source, STOP exception, session renewal timing, host-ledger loss and recovery procedure |
| Config manager | One OPEN staging object, six-bit required bitmap, immutable write values, OPEN deadline, pre-commit NACK/retry behavior, async COMMIT/cache/generation, abort/late completion rules |
| CAN wrapper extension | Logical channel → MB mapping, three independent persistent buffers, freeze/disable checks, per-slot errors/overrun/rearm, rollback ownership, legacy compatibility, masks/IRQs/DMA |
| Motor coordinator | Full transition table/guards, zero-demand START, target range/direction, STOP/FAULT precedence, operation generation, active configuration invalidation/revalidation |
| Motor Interface/mock | Exact prototypes/result enum and copied-buffer lifetime; capability/feedback freshness, safe-off confirmation, cancellation and fault injection; no physical defaults |
| Scheduler/transport | Turn WCET budget, poll order/fairness, queue sizes, reply reservation, high-priority STOP latch, pending TX lifecycle and expiry of unsent work |
| Time/platform | Monotonic source, resolution/range/wrap/clock/debug behavior, health checks, bounded deadlines and independent safety action on CPU stall |
| GUI | UI/domain/transport ownership, sequence/transaction ledger, connect/reconnect/staleness, heartbeat worker, transport normalization/errors/echo/timestamps and STOP queue priority |
| Diagnostics | Current/first-fault samples, per-channel loss/queue counts, session/config/operation counters, clock/loop latency measurements; no debugger register-read side effects |

## 5. Acceptance test catalogue

These are **planned cases, not performed results**. Initial execution uses deterministic host mocks; target-specific cases require user-operated build/debug and recorded board evidence.

| Case | Stimulus / setup | Required result |
|---|---|---|
| T01 | Compare pre/post source/config manifest | Only approved stage files change; design task adds only the three Markdown deliverables; no source/vendor/generated edits |
| T02 | Existing one-RX wrapper/loopback and user Phase 3 smoke regression | Same ownership/polling/ID/data behavior; no competing CAN0 initialization or NVIC CAN dependency |
| T03 | Static dependency/API review | App/protocol/motor public headers expose no RTD/SPI types; all RTD CAN calls remain private to existing wrapper |
| T04 | Configure MB1=0x100, MB2=0x110, MB3=0x111 and send interleaved IDs | Each slot returns correct ID/data with independent state; MB0 TX continues; no single-buffer overwrite |
| T05 | Register same MB twice; inject repeated completion | Busy rejected; copy/rearm precisely once per completion; next valid frame works |
| T06 | Fail second/third registration, Stop/Deinit/restart and missing hardware acknowledgment | Fault ownership/lifetimes preserved; motor inhibited; recovery never accesses disabled MBs or enables IRQs |
| T07 | Full/overrun raw CS, rearm failure and repeated cache reads | Correct status/drop policy, no publish after failed rearm, first evidence retained; getters do not read ESR1 |
| T08 | All seven golden frames and boundary values | Exact eight bytes, correct IDs/endian/signed interpretation; no struct-layout dependency |
| T09 | CRC reference 123456789 and full 25-byte config vector | 0x29B1 and 0x3B34 respectively; Python/MCU-independent codecs agree |
| T10 | DLC 0..7 on known IDs | No absent-byte reads; BAD_DLC only when correlation bytes available; no actuation or lease refresh |
| T11 | Extended/RTR/FD, unknown ID, telemetry delivered toward node | Drop/count, no response loops, no actuation; hardware filters and software metadata checks agree |
| T12 | Reserved flags/parameters/version/opcodes, invalid START direction, demand > max or >10000 | Exact documented NACK, no active changes/lease refresh; no invented unsupported capability |
| T13 | Signed direction/current/rpm and invalid feedback fields | Negative values sign-correct; invalid/stale/out-of-range encodes zero with validity cleared; GUI shows unavailable |
| T14 | Duplicate START/SET_DEMAND with identical bytes | One backend action; cached/PENDING response; duplicate does not refresh lease |
| T15 | Same consumed command sequence with changed bytes | SEQUENCE_CONFLICT; no motion side effects |
| T16 | Command sequence 254,255,0,1 and delta 127/128/stale cases | Modular freshness exactly as spec; rejected values do not advance inappropriate state |
| T17 | Fresh well-formed command NACKed by state, then exact retry | Sequence consumed/result retained; no duplicate state transition; format-invalid input remains unconsumed |
| T18 | Boot/session renewal with previously captured START/DEMAND/COMMIT | Old requests rejected; demand zero; pending operation generations invalidated; new host discovers session |
| T19 | Host reconnect with unknown sequence/config ledger | Safe STOP first; no guessed START or automatic replay; require reviewed reset/session/config recovery |
| T20 | Session/generation exhaustion or mock constant-session accidental real build | Inhibit/review/service gate, no silent reuse or physical START |
| T21 | OPEN + all six writes in varied order + valid CRC COMMIT | Staging only until COMMIT; backend configure once; active config/generation published only on success |
| T22 | Missing param, bad CRC, conflicting duplicate write | Specific NACK, no partial apply; staged bitmap/values retained according to spec |
| T23 | COMMIT reaches MB3 before outstanding MB2 write | MISSING_PARAMETER, no partial configure; host ACK pacing prevents this in normal operation |
| T24 | Duplicate OPEN after several writes; staging reaches deadline | Duplicate does not reset/extend transaction; expired staging discarded; later COMMIT rejected |
| T25 | ABORT, zero/nonzero reserved value, unknown ID, new OPEN while active | Idempotent retained ABORT; correct BAD_FIELD/TRANSACTION_MISSING/BUSY responses |
| T26 | Lost COMMIT ACK, retry during pending and after completion | One backend configure; PENDING/terminal cached response; unchanged generation on retry |
| T27 | Backend partial configure failure or STOP during configure; late success arrives | Drive inhibited, active config invalid/cancelled; stale completion never publishes cancelled config or enables motor |
| T28 | Boot, no configuration, START | Safe default outputs; NOT_CONFIGURED; no enable |
| T29 | Valid config then fresh lease and START ± direction | Capability/interlocks checked; STARTING then RUNNING at zero demand; no old demand restored |
| T30 | SET_DEMAND before START, reverse request while running, STOP then restart | BAD_STATE/range policy; direction only changes through approved STOP/restart sequence; no automatic restart |
| T31 | Backend reports unknown sensor/unsafe-to-reverse/not-ready/no envelope | Unsupported/interlock rejection; no fabricated feedback and no physical START |
| T32 | STOP while MB0 permanently BUSY or ACK queue full | Inhibit requested independent of CAN; no pending TX overwrite/cancel; reply can be unavailable without blocking stop |
| T33 | STOP stale/wrong session/duplicate, malformed STOP, late START completion | Well-formed zero-argument STOP inhibits; malformed frame does not count as STOP; old completion cannot enable |
| T34 | Accepted fresh command at lease threshold; malformed/duplicate/config flood | Only qualifying commands refresh; check expiry before draining old queue; timeout remains effective |
| T35 | No CAN ACK, TX completion never occurs, ongoing motor/time events | One pending TX only; all scheduler services continue; lease/safe-off deadline met in fake time and measured target budget |
| T36 | Bus-off then hardware CAN recovery | Local motor inhibit first; fault/session/rearm requirements persist; error telemetry is best effort |
| T37 | GUI unplug/hang, old frames queued in PCAN/ESP bridge, reconnect | Host drops stale queued motion; node lease expires; traffic return never restarts motor |
| T38 | RX overrun on each channel / software RX or response queue overflow | Discard unreliable control/staging, safe-inhibit/fault policy, first loss counters; STOP priority never depends on queue availability |
| T39 | Time wrap/backwards/uncertain time, backend deadline and safe-off confirmation failure | No extended run from bad arithmetic; explicit fault; safe-off-unconfirmed flag, never report STOPPED falsely |
| T40 | Mock completion delays/failures and all feedback invalid | Simulated flag visible, deterministic results, no physical register access |
| T41 | Backend has no rpm/current sensing or only fault/quantized indication | Corresponding validity bits remain clear; no inference of current/rpm from duty or SPI success |
| T42 | Configure/START pending when new fault/STOP supersedes operation | Backend disables stale enable writes and application discards stale completion; counters track cancelled op |
| T43 | SPI/hardware contract incomplete | Physical enable path remains unavailable regardless of protocol/mock success |
| T44 | Maximum admitted request/telemetry load and NACK/error flood | Measured bounded turn/queue depth/fairness; STOP/time services not starved; report overload safe action |
| T45 | Adapter send accepted but CAN pending, application ACK absent | GUI shows pending/unknown, never successful motor execution |
| T46 | ACK session/origin/key/selector wrong or unknown result | GUI does not resolve another transaction; stale/unknown failure visible |
| T47 | Initial ACK retry exhaustion or backend PENDING beyond approved deadline | Explicit timeout/unknown, no uncontrolled START retry; node safe policy and current status govern recovery |
| T48 | Run identical codec/controller vectors through mock, PCAN stub and ESP bridge stub | Identical CAN IDs/bytes/domain results; no backend import dependency in protocol model |
| T49 | Backend status/error/echo frames and differing timestamp origins | Never decoded as motor frames; host time and adapter/acquisition time distinguished |
| T50 | Slow UI/plotting plus STOP/disconnect intent | Heartbeats isolated from renderer, bounded queue/drop-age handling; STOP cancels unsent START/DEMAND; no auto rearm |
| T51 | Existing ESP32 raw GUI used without periodic heartbeat | Valid raw parser tests possible; sustained RUNNING expires safely unless automated fresh KEEPALIVEs are supplied |
| T52 | Controlled physical integration with independently measured inhibit, sensor and protection behavior | Only after separate authorization/evidence; record actual motor/backend limits and test results; CAN PASS alone insufficient |

Additional target measurements: actual mask/MB readback and IRQ suppression after multi-RX start; simultaneous RX/TX bursts; maximum poll service gap and timestamp uncertainty; application queue high-water marks; bus load/errors; safe action while transmit is retrying. Hardware-specific faults must be injected only with an approved procedure, not by ad hoc shorting/disconnecting powered motor wiring.

## 6. SPI/platform agreement register

| Decision | Owner / evidence required | Default until agreed |
|---|---|---|
| Motor board/part/revision/control topology | SPI + hardware: schematic, exact fitted part/datasheet, power/control pin map | No physical backend assumed |
| Safe-off electrical action and reset defaults | Hardware + motor team: enable/PWM truth table, independent cutoff, timing/confirmation | Physical START inhibited; mock only |
| Coast/brake/reversal/standstill policy | Motor + hardware: mechanical hazard/load and measurement/validated dwell evidence | No reverse/brake assumption |
| Allowed duty/slew/PWM/control envelope and protection thresholds | Motor + hardware: system analysis and validated settings, not a component headline current rating | All physical operating settings unresolved |
| SPI public API and shared-bus ownership | SPI team: headers, types, nonblocking semantics, completion/error codes, lifetimes | Use domain interface/mock; no invented function names |
| Cancellation/late writes and prioritized inhibit | SPI + safety/platform: generation/abort semantics and bounded safe path | Physical adapter blocked |
| Feedback availability/scale/sign/age | SPI + sensing team: ADC/encoder circuits, calibration, rates and overflow/staleness behavior | Validity cleared for absent data |
| Fault mapping/clear permissions | SPI + hardware: raw bits, active/latched conditions, safe clear procedure | Generic backend fault only; no silent clear |
| Real timebase/independent watchdog/control deadlines | Platform + motor: timer reservation, wrap/halt/clock behavior, WCET, cutoff on CPU stall | Fake time for tests; board mock time separately reviewed |
| Protocol IDs, host ownership, lease and session uniqueness | CAN + GUI + platform reviewers | Draft proposals only; no actuation approval |
| SPI/PWM interrupt needs versus foreground integration | SPI + platform: control-rate/latency evidence | CAN stays polling; no assumption that a physical control loop can safely be polled |
| PCAN hardware/binding/channel and ESP32 bridge contract | GUI team: environment, framing, queues, bitrate/error/timestamp behavior | Backend-neutral domain; retain ESP32 raw GUI test peer |

## 7. User-operated build/debug evidence for later stages

Do not run builds automatically. When an implementation stage is approved and ready, provide the concrete changes first, then guide the user through S32DS Refresh, source/include selection and Build Project. Record the selected build profile, full compiler/linker outcome, fresh ELF identity, probe/reset settings and exact test stimulus. Confirm the main being built matches the intended application; stale `CAN_TEST_PROFILE` defines do not select features that current source does not implement.

Run legacy loopback/physical regression before multi-MB tests, then protocol with mock, and only later the separately authorized real motor procedure. Debugger halts can invalidate timer/control observations; measure deadlines in free-running runs with bounded instrumentation. Inspect cached diagnostics rather than live ESR1. Record attempted/accepted/completed TX separately, per-channel RX/overrun counts, active session/config/operation generation, state transitions, fault reason and motor inhibit confirmation.

No claim of Phase 4 build, test, GUI/PCAN availability, motor capability or hardware safety is made until its corresponding gate has actual evidence. The three design documents are ready for review; implementation remains a separate task.

## 8. Checks performed for this design delivery

Document consistency checks verified seven unique 11-bit IDs and their decimal equivalents, nine eight-byte example rows, the 25-byte canonical configuration CRC/reference vector, consecutive requirement IDs R01–R20, planned case IDs T01–T52 and balanced code fences. These checks validate the draft's internal arithmetic/structure only; no project compiler, host implementation tests or hardware was run.

A SHA-256 comparison of pre-existing source/configuration/document/archive files (excluding `.git` and build-output directories) found no modifications or removals. Exactly `CAN_Protocol_Specification_Draft.md`, `CAN_Phase4_HLD.md` and this plan were added. The existing working main/adapter, RTD/generated files, motor/SPI interfaces if later supplied, and IDE configuration were not edited.
