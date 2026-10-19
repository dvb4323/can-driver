# CAN Protocol Specification — Phase 4 draft v1.0

Date: 2026-10-19. Status: **proposed for agreement, design only**.

## 1. Evidence and scope

**Verified project facts:** the current application uses the existing custom RTD adapter, CAN0, TX MB0/RX MB1, standard Classic data frames, static transfer storage and foreground polling. The generated object remains loopback; the wrapper privately overrides its mode. Nominal 500 kbit/s derives from the existing expected 48 MHz PE clock and raw timing 7/4/1/5/1. **User-verified:** Phase 3 physical TX/RX with ESP32-S3 succeeded. This is not verification of motor operation or this new protocol. No new build or hardware test was performed for these documents.

**All IDs, formats, periods, ranges, session rules and policies below are proposals.** They do not describe the existing smoke-test bytes. No motor/SPI/PWM implementation or hardware schematic for the motor stage was supplied. A wire encoding range is not a permitted electrical operating envelope. SPI-team decisions and hardware acceptance gates appear in the companion HLD/plan.

This draft targets one controller and one motor, one authorized host at a time on a trusted bench network. It is not CANopen, J1939, ISO-TP or a safety-certified protocol. It has no authentication or protection against an active malicious/replaying bus participant. No broadcast START, automatic restart, persistent configuration or raw SPI register write is supported.

## 2. Wire conventions and ID allocation

Classic CAN; IDE=0, RTR=0, FD/BRS absent; **DLC exactly 8 for every defined message**. Offsets B0..B7 count wire data bytes from zero. All multibyte fields use little-endian; signed fields use two's complement. Never serialize a C structure by casting/copying its memory representation. Decode with shifts and explicit range checks. Application CRC below supplements CAN's link CRC for a multi-frame configuration transaction; no other application CRC is introduced.

| CAN ID (hex / decimal) | Message | Direction | Proposed MCU MB |
|---|---|---|---|
| 0x080 / 128 | Error Status | Motor node → host | TX MB0, through scheduler |
| 0x090 / 144 | ACK/NACK | Motor node → host | TX MB0, through scheduler |
| 0x100 / 256 | Motor Command, including STOP/heartbeat | Host → motor node | RX MB1 |
| 0x110 / 272 | Motor Configuration parameter write | Host → motor node | RX MB2 |
| 0x111 / 273 | Configuration Apply/OPEN/ABORT | Host → motor node | RX MB3 |
| 0x180 / 384 | Motor Status | Motor node → host | TX MB0, through scheduler |
| 0x181 / 385 | Motor Feedback | Motor node → host | TX MB0, through scheduler |

These are application arbitration priorities, not routing addresses. All remaining IDs, including Phase 3 0x123/0x321 and loopback 0x14, are outside this protocol and ignored. The Phase 3 regression must run as a separate build/application, not concurrently. Motor node ignores response/telemetry IDs on RX, preventing ACK loops. Multiple nodes require a separately reviewed ID/address allocation.

### Session and version

The node publishes a nonzero 32-bit **session_id** in Motor Status. All host messages carry it. Establish a new session on boot and after a communication-loss/bus-off/critical control-path fault; invalidate old request caches and staging. No host request can choose the node's session. Session renewal must not delay drive inhibition.

Session uniqueness across resets is an implementation gate: a monotonic boot/session authority or reviewed nonce strategy is needed. It is **not verified in this project**. A constant boot value is allowed only for explicitly labeled deterministic mock tests; physical motor operation must remain inhibited until the strategy is agreed. A 32-bit token is neither authentication nor a proof against wrap/collision. If the token space wraps, stay inhibited and require service, rather than reusing it silently.

The ID map denotes this protocol major version. OPEN explicitly requires version 1.0; Motor Status includes a v1 marker. Unsupported versions must not commit configuration. There is no unused byte in the command format for an additional version field. Future incompatible layouts require a revised major/ID allocation, not silent field reuse.

## 3. Motor Command — 0x100, DLC 8

| Offset | Field | Type / unit | Allowed values |
|---|---|---|---|
| B0 | command_sequence | u8 | 0..255, modulo sequence within session |
| B1 | opcode | u8 enum | Table below; all others UNSUPPORTED |
| B2..B3 | argument | u16 or i16 LE | Opcode-specific; no unspecified flag bits |
| B4..B7 | session_id | u32 LE | Current node session, except fail-safe STOP rule |

| Opcode | Name | Argument / valid states / meaning |
|---|---|---|
| 0x00 | STOP | Argument=0. Recognized in all states. Immediately request drive inhibit/zero demand. Never clears a fault or enables anything. See STOP exception below. |
| 0x01 | START | **i16 +1 or -1**, selecting positive/negative direction. Only STOPPED, active config valid, fresh lease, backend ready/interlocks satisfied. Enable with **zero demand**; never reuse a previous nonzero demand. |
| 0x02 | SET_DEMAND | **u16 0..10000 basis points**: 1 unit=0.01% normalized duty magnitude. Only RUNNING; additionally ≤ active max_demand. Not rpm, torque or current control. Backend applies the configured slew. Direction is fixed by START. |
| 0x03 | KEEPALIVE | Argument=0. Accepted for the current session in any initialized application state; updates lease only under freshness rules. Does not start or change demand. |
| 0x04 | CLEAR_FAULT | Argument=0. Only FAULT with drive confirmed inhibited and fault cause absent/recoverable. Backend may refuse. Ends STOPPED if active config still valid, otherwise UNCONFIGURED; never starts. |

Changing direction while enabled is unsupported in v1. Host must STOP, wait for confirmed safe-off/ready-for-restart under the backend's approved mechanical policy, then issue a fresh START with the other direction. Zero commanded duty alone does not prove the shaft stopped. Hardware reverse capability, dwell and stop detection are unresolved; mock behavior must be labeled simulated.

### Sequence, duplicates and liveness

For a matching session and well-formed supported command/argument, first check the retained command result cache. Same sequence/exact bytes returns its cached/current pending result; same retained sequence/different bytes gives SEQUENCE_CONFLICT. If no entry exists, the first sequence is accepted as the baseline. Subsequently `delta=(received-last_consumed) mod 256`: 1..127 is fresh; 128..255 is stale; 0 without a retained matching record is stale. A fresh request consumes its sequence even if state/interlock/backend validation returns NACK. Malformed envelope, unknown opcode, invalid argument or wrong session does not advance it. Correcting an unconsumed malformed request can therefore use its sequence again.

An exact retained duplicate returns its cached result/current pending result without repeating the action and **does not refresh the lease**; subsequent KEEPALIVEs do not erase an outstanding START's result. Proposed cache: 32 records, terminal retention 500 ms, outstanding asynchronous records pinned until completion then 500 ms. Limit fresh Command requests to 50/s, leaving capacity for the retry window; reject overload without evicting an outstanding operation. Proposed admission bucket allows five requests of burst capacity and refills at 50/s; excess requests get CAPACITY with no sequence/lease/actuation change, except the STOP safety exception. Unknown/evicted old sequences give STALE_SEQUENCE. Do not reuse a key while retained. For an asynchronous backend deadline D, require `D + 500 ms < (256 / 50) seconds = 5.12 s`; otherwise revise sequence width/rate before backend integration. This is a correlation constraint, not an approved physical safety deadline.

The GUI uses one sequence generator shared by all command opcodes, modulo 256, and never resets it merely because a widget reconnects. It retains its own next sequence/request ledger; Motor Status does not expose last sequence. If the host loses that ledger, v1 recovery is: send the fail-safe STOP, confirm inhibition, require a deliberate node reset to obtain a new unique session, then discover/configure again before START. There is no undocumented CAN reset/sequence-query command. Do not guess sequences or automatically power-cycle/restart a motor. Losing active-configuration knowledge similarly requires a complete safe reconfiguration before motion.

The 8-bit sequence assumes bounded stale transport queues and one host: host/bridge must discard stale unsent control frames, with proposed maximum queued age **200 ms**. Modulo ordering cannot detect arbitrarily old replays after wrap. Session renewal, a 500 ms lease and explicit rearming address ordinary disconnect/reboot cases, not adversarial replay. Agree this limitation before physical motor use; a wider counter requires a format revision.

**STOP exception:** a correctly framed command with opcode STOP and argument zero is honored regardless of session/sequence, including stale/duplicate STOP. It forces a stop again if currently enabled. It does not advance a foreign/stale sequence, refresh the lease, transfer ownership, or clear FAULT. Respond with current-session PENDING until safe-off confirmation, then COMPLETED; if already safely off, COMPLETED immediately. Invalid DLC/RTR/IDE or nonzero argument is not a recognized STOP. This prioritizes inhibit over replay rejection; an old STOP can interrupt operation intentionally. STOP is not a hardware emergency-stop replacement.

The host cancels all unsent START/DEMAND work when STOP is requested, prioritizes STOP ahead of queued writes/heartbeats, and waits for safe-off acknowledgment/status before issuing further control work. No later START is generated automatically. An ACK for a previously completed request is a historical transaction result, not proof the motor is still running: Motor Status is authoritative for current state.

## 4. Motor Configuration — 0x110, DLC 8

| Offset | Field | Type / rule |
|---|---|---|
| B0 | transaction_id | u8, 1..255; zero invalid |
| B1 | parameter_id | u8 enum below |
| B2..B3 | value | u16 LE, units below |
| B4..B7 | session_id | u32 LE, current session |

Writes affect staging only. No motor register, PWM output or active config changes on receipt. All six parameters must be supplied for each transaction, even when only one setting changes.

| Parameter | Name | Valid encoding | Proposed mock/reference value |
|---|---|---|---|
| 0x01 | control_mode | 0=DUTY; 1..65535 unsupported | 0 |
| 0x02 | max_demand | 0..10000 basis points; 0 inhibits START | **1000 only for mock examples**, not a motor-safe limit |
| 0x03 | demand_slew | 1..65535 basis points/second | 2000 only for mock examples |
| 0x04 | status_period | 20..1000 milliseconds | 100 ms |
| 0x05 | feedback_period | 20..1000 milliseconds | 100 ms |
| 0x06 | permitted_directions | 1=positive, 2=negative, 3=both; bits 2..15 reserved zero | 1 for mock example; must be subset of backend capabilities |

Other parameter IDs are UNSUPPORTED, including attempts to set raw H-bridge registers, physical current limits, PWM frequency, temperature thresholds, speed-loop gains or brake modes. Do not map the representational range 10000 bp to an approved physical maximum. Physical backend validates every proposed setting against a board/motor-specific approved envelope; without it, START remains inhibited. This draft chooses no current/thermal/voltage protection limit.

Writes are allowed only in an OPEN transaction while application is UNCONFIGURED or STOPPED, not during enable/stop/configure transitions. Exact duplicate parameter/value returns ACCEPTED without further effect. Different value for an already-written parameter in the same transaction gives TRANSACTION_CONFLICT and retains the first value; open a new transaction to revise it. Parameter order may vary; request/ACK pacing prevents RX MB2 overrun. Replies correlate with origin=CONFIG, transaction and parameter.

## 5. Configuration Apply — 0x111, DLC 8

| Offset | Field | Type / rule |
|---|---|---|
| B0 | transaction_id | u8, 1..255 |
| B1 | action | 0=OPEN, 1=COMMIT, 2=ABORT; other values unsupported |
| B2..B3 | action_value | OPEN: B2 major=1, B3 minor=0. COMMIT: CRC16 LE. ABORT: both bytes zero. |
| B4..B7 | session_id | u32 LE, current session |

### Transaction state contract

1. **OPEN:** only UNCONFIGURED/STOPPED with safe-off confirmed. Allocates one staging transaction and a fixed proposed **2000 ms deadline measured from OPEN**. No lease refresh. A different active transaction gives BUSY; host must ABORT or wait. Exact duplicate OPEN returns ACCEPTED and does not erase writes or extend the deadline.
2. **WRITE:** collect all six values, validate individual encoding, retain received-parameter bitmap. Missing/invalid write never mutates active config. Staging is not persisted across reset, session change, STOP, timeout or fault.
3. **COMMIT:** matching OPEN/session, complete parameter set, correct digest, valid cross-field/backend capabilities and drive safely off are required. Missing parameter gives MISSING_PARAMETER; bad CRC gives CRC_MISMATCH. Staging remains editable only under the immutable-parameter rule until its deadline; no partial apply. These pre-admission NACKs do not pin a terminal COMMIT result: a later COMMIT is revalidated against completed staging/corrected digest. Once backend COMMIT is accepted, its request identity/result becomes immutable.
4. Accepted COMMIT enters CONFIGURING and returns PENDING. Backend configuration is asynchronous and starts once. Its completion deadline is a separately approved backend contract; the OPEN deadline stops governing once COMMIT is accepted. Publish active config and increment generation **only after backend success**; send COMPLETED. Backend failure/deadline gives NACK and FAULT, inhibits drive and marks active config invalid; do not assume partially written hardware rolled back.
5. Exact duplicate COMMIT while pending returns PENDING; after completion returns the cached final result, without reapplying. Lost ACK must not cause a second hardware configuration. Cache the terminal transaction for proposed **5000 ms**, with ID/payload identity; reuse of its ID during retention gives TRANSACTION_CONFLICT. Session change invalidates it. Host increments IDs modulo 255, skips zero and retained IDs.
6. ABORT has zero action_value. It discards OPEN staging; an exact repeated ABORT is idempotent while its result is retained. ABORT cannot cancel an accepted backend COMMIT: BUSY; use STOP for inhibition and let backend completion be handled as cancelled/stale under the application operation-generation guard. No silent active-config rollback.
7. A timed-out OPEN is discarded. Later WRITE/COMMIT gives TRANSACTION_MISSING. Unknown ABORT transaction also gives TRANSACTION_MISSING. STOP aborts staging/configuration and inhibits the drive; a late backend completion cannot publish a cancelled config.

Active generation starts at zero (invalid). Successful commits produce 1..65535. At generation exhaustion, force session renewal and configuration invalidation while inhibited; do not silently wrap to a previously used generation within the session. Session changes caused only by communication loss may preserve an already committed config after revalidation, but never an in-progress staging transaction.

### Canonical configuration CRC

CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, refin=false, refout=false, xorout=0. Reference ASCII `123456789` → 0x29B1. Input is exactly 25 bytes:

`major(01), minor(00), txn, session[LE,4], then six records (param_id,u16 value LE) sorted by parameter_id 01..06`.

The digest is over the full proposed config, not arrival order. It is an integrity/correlation check, not an authentication or functional-safety guarantee.

## 6. Motor Status — 0x180, DLC 8

| Offset | Field | Type / values |
|---|---|---|
| B0 | application_state | u8 enum below |
| B1 | flags | Defined bits below; reserved bit 7=0 |
| B2..B3 | active_generation | u16 LE; 0 means no valid active config |
| B4..B7 | session_id | nonzero u32 LE; zero only while boot/session authority not ready |

State enum: 0 BOOT_SAFE, 1 UNCONFIGURED, 2 CONFIGURING, 3 STOPPED, 4 STARTING, 5 RUNNING, 6 STOPPING, 7 FAULT. Values 8..255 reserved; GUI must show unknown state and inhibit controls.

Flags: bit0 config_valid; bit1 backend_is_mock; bit2 drive_inhibited_confirmed; bit3 communication_lease_fresh; bit4 backend_ready_for_start; bit5 feedback_has_valid_fields; bit6 protocol_v1_marker (always 1 for this draft); bit7 reserved zero. Mock bit must remain set even when simulating current/speed. STOPPED means inhibited/zero target, not proof of mechanical standstill. START may require additional backend quiescence checks.

Publish at configured period, before configuration at proposed 100 ms, and on state/session/config transitions (coalesced latest). State and config flags/generation are one coherent snapshot. Never report RUNNING merely because a START request was enqueued or its CAN frame acknowledged. Status is not an ACK for any specific command and carries no command sequence.

## 7. Motor Feedback — 0x181, DLC 8

| Offset | Field | Type / unit / valid encoding |
|---|---|---|
| B0 | sample_sequence | u8 modulo 256, increments once per newly acquired snapshot, not retransmission |
| B1 | validity/quality | bit0 applied_demand valid; bit1 current valid; bit2 speed valid; bit3 simulated; bits4..7 reserved zero |
| B2..B3 | applied_demand | i16 LE, -10000..+10000 basis points; sign=direction; control effort, not measured rpm |
| B4..B5 | motor_current | i16 LE, 1 mA/unit; representable -32768..32767 only, not a protection limit |
| B6..B7 | shaft_speed | i16 LE, 1 rpm/unit; representable -32768..32767 only, not a supported speed envelope |

An invalid/unavailable/stale/out-of-range field is encoded zero with its valid bit cleared, **not** silently clamped or represented as a measured zero. It must be shown as unavailable by GUI. Current sign, sensing capability/calibration and rpm sensor availability require SPI/hardware agreement. If backend supplies only quantized fault information, do not fabricate current, speed or temperature. No temperature/supply-voltage field is defined in v1; extending telemetry requires an agreed message/page revision.

Publish one cached acquisition snapshot at feedback period. If no new acquisition, retransmit the same sample sequence/values while validity reflects the approved freshness limit. Proposed mock freshness target 200 ms; physical sensor freshness is unresolved and must be bound in the backend contract. This frame has no session field: GUI associates it with recent Motor Status and marks it untrusted/stale across a session change until a new current-session observation window. It must never authorize actuation or prove cross-frame atomicity. Host acquisition/receive timestamps are distinct.

## 8. Error Status — 0x080, DLC 8

| Offset | Field | Type / values |
|---|---|---|
| B0 | event_sequence | u8 modulo 256; increments on error-set/reason change, unchanged on repeat |
| B1 | primary_reason | 0 NONE, 1 MOTOR_BACKEND, 2 COMM_LOSS, 3 CAN_BUS_OFF, 4 RX_OVERRUN, 5 QUEUE_OVERFLOW, 6 TIMEBASE, 7 BACKEND_DEADLINE, 8 CAN_DRIVER; 9..255 reserved |
| B2..B3 | latched_fault_bits | u16 LE, mapping below |
| B4..B7 | session_id | current u32 LE |

Fault bits: 0 backend_fault, 1 command_lease_expired, 2 CAN_bus_off, 3 RX_overrun, 4 RX_or_ACK_queue_overflow, 5 timebase_invalid, 6 backend_deadline, 7 CAN_driver_fault, 8 safe_off_unconfirmed. Bits9..15 reserved zero. On multiple faults choose primary priority: safe-off unconfirmed/backend fault → timebase/backend deadline → CAN driver/bus-off → lease → overrun/queue; retain the complete bitmap and local first-fault snapshot. For safe-off-unconfirmed use primary MOTOR_BACKEND plus bit8.

Publish immediately if CAN can transmit, then at proposed 1000 ms while latched, and once after authorized clear. Bus-off may prevent reporting; local safe action must occur without waiting for this frame. Protocol validation NACKs alone do not latch a motor fault or refresh the lease. Their counters live in local diagnostics; do not invent physical overcurrent/overtemperature bit meanings before agreeing backend mapping. Native hardware fault codes are cached locally; v1 exposes the generic backend fault bit only.

## 9. ACK/NACK — 0x090, DLC 8

| Offset | Field | Type / meaning |
|---|---|---|
| B0 | request_origin | 1 Command, 2 Configuration, 3 Apply; other values reserved |
| B1 | request_key | Command sequence or configuration transaction ID |
| B2 | result | Enum below |
| B3 | request_selector | Echo opcode / parameter ID / Apply action; 0xFF when unavailable |
| B4..B7 | session_id | Current node session; GUI must also correlate session, origin, key and selector |

Results: 0x00 ACCEPTED (terminal for parameter write, OPEN/ABORT, KEEPALIVE, SET_DEMAND); 0x01 PENDING (accepted asynchronous START/STOP/COMMIT/CLEAR_FAULT); 0x02 COMPLETED (terminal successful backend action). **No ACK means physical command execution is proven unless it is the appropriate terminal result and current Motor Status corroborates state.** SET_DEMAND ACCEPTED means target stored, not that ramp/shaft reached it. CAN controller ACK and PC transport send success are distinct from all these application responses.

NACK results: 0x80 BAD_DLC; 0x81 BAD_VERSION; 0x82 BAD_FIELD_OR_RESERVED; 0x83 OUT_OF_RANGE; 0x84 WRONG_SESSION; 0x85 STALE_SEQUENCE; 0x86 SEQUENCE_CONFLICT; 0x87 BAD_STATE; 0x88 NOT_CONFIGURED; 0x89 UNSUPPORTED; 0x8A TRANSACTION_MISSING (includes expired); 0x8B TRANSACTION_CONFLICT; 0x8C MISSING_PARAMETER; 0x8D CRC_MISMATCH; 0x8E BUSY; 0x8F BACKEND_FAULT; 0x90 OPERATION_TIMEOUT; 0x91 SAFETY_INTERLOCK; 0x92 CAPACITY. Values not listed are reserved; GUI treats them as unknown failure. B3 is always the selector, not an undocumented vendor error code.

For a known host request ID with wrong DLC, NACK only if B0 and B1 are actually present; never read absent bytes or fabricate a correlation key. Otherwise silently discard and count. Wrong-session NACK carries the node's current session, so a host must not treat it as success for its old request. Unknown IDs, IDE/RTR/FD frames, response IDs and uncorrelatable malformed traffic are dropped/counted without response. Reserved bits/bytes must be zero; nonzero yields BAD_FIELD_OR_RESERVED. Rate-limit invalid-traffic responses (proposed 10/s aggregate), never let NACK storms starve STOP/safe-off.

Reserve response capacity before accepting a state-changing non-STOP request. If capacity cannot be reserved, make no change and return CAPACITY if a reserved rejection slot exists; otherwise count/drop and enter the reviewed queue-overflow safe policy. STOP always requests inhibition even if there is no reply capacity. Response caches retain correlation/result records, not motor operations. A lost ACK may delay GUI confirmation; it never justifies unbounded START retransmission.

A fault-driven session renewal terminates host pending requests from the old session as aborted/unknown, never successful. A NACK/Error/Status carrying the new session prompts rediscovery/rearm; the GUI must not keep retrying an old-session START merely because it has not seen its former terminal ACK.

## 10. Startup, safe transitions and deadlines

All timing values here are **proposed software requirements for mock/integration**, not approved physical safety limits.

1. Hardware/board startup must hold drive inhibited before application/RTD initialization. Enter BOOT_SAFE, initialize motor mock/backend, time service and CAN. No restoration of saved demand or automatic START.
2. Publish session/status in UNCONFIGURED. Host discovers v1/session/mock flag, sends fresh KEEPALIVEs, performs OPEN + six parameter writes + COMMIT and waits for COMPLETED/config_valid/STOPPED.
3. Host requests START(direction), waits for COMPLETED and RUNNING with zero demand, then sends SET_DEMAND. All interlocks and direction capabilities must hold.
4. STOP or a critical fault immediately sets target zero, requests the backend's approved safe inhibit path and invalidates outstanding operation generations. Enter STOPPING, then STOPPED on safe-off for an ordinary stop, or FAULT for a fault stop. Keep CAN polling and diagnostics active.
5. If safe-off cannot be confirmed within the backend's approved deadline, latch bit8 and remain FAULT; never claim the motor stopped. Required independent disable/watchdog path is a hardware integration gate.

| Event | Proposed policy |
|---|---|
| KEEPALIVE transmission | Host every 50 ms; fresh command/START/SET_DEMAND accepted can also refresh lease |
| Command lease | 500 ms; enforce in STARTING/RUNNING using node monotonic time, not GUI timestamps |
| Duplicate/malformed/config/telemetry traffic | Does not refresh command lease |
| Lease expiration | Force safe inhibit before processing a late queued START; discard staging, renew session, latch COMM_LOSS; never auto-restart on traffic return |
| Any control RX overrun or software RX overflow | Discard affected commands/staging, safe inhibit and fault; do not execute the delivered latest motion command as if no loss occurred |
| Bus-off / driver fault / invalid timebase | Same independent safe action; preserve pending CAN ownership and fault evidence; reporting is best effort |
| ACK initial wait at host | 100 ms; max two retries of **identical** request, never auto-retry START after reconnect/session change |
| Async operation completion wait | Backend-specific, not chosen here. GUI shows PENDING; node owns the real deadline |
| OPEN staging lifetime | 2000 ms, no renewal on retry |
| Terminal transaction cache | 5000 ms; don't reuse retained transaction IDs |
| Host status freshness | 300 ms at default status rate; inhibit GUI START on stale status; display fault/unknown separately |

Host timeout does not cancel a command already on wire or in an adapter queue. To stop, send STOP through the priority path and confirm safe state. If disconnected, node lease must independently inhibit. Hardware CAN automatic bus-off recovery does not clear the application fault/rearm requirement. Pending TX may remain indefinitely busy: application/motor/time services must never wait for it.

## 11. Golden wire examples (mock only)

Session 0x11223344, transaction 0x25, v1.0; use these for codec tests, not a physical motor prescription.

| ID | Meaning | Eight bytes |
|---|---|---|
| 0x111 | OPEN | `25 00 01 00 44 33 22 11` |
| 0x110 | Write max_demand=1000 bp | `25 02 E8 03 44 33 22 11` |
| 0x100 | KEEPALIVE seq 0x10 | `10 03 00 00 44 33 22 11` |
| 0x100 | START positive seq 0x11 | `11 01 01 00 44 33 22 11` |
| 0x100 | SET_DEMAND=500 bp seq 0x12 | `12 02 F4 01 44 33 22 11` |
| 0x100 | STOP seq 0x13 | `13 00 00 00 44 33 22 11` |
| 0x090 | START pending | `01 11 01 01 44 33 22 11` |
| 0x090 | START completed | `01 11 02 01 44 33 22 11` |
| 0x180 | STOPPED, config valid, mock, safe-off, fresh lease, ready, v1, generation 1 | `03 5F 01 00 44 33 22 11` |

For complete parameter set `(01=0,02=1000,03=2000,04=100,05=100,06=1)`, canonical CRC bytes:

`01 00 25 44 33 22 11 01 00 00 02 E8 03 03 D0 07 04 64 00 05 64 00 06 01 00`

Calculated CRC=0x3B34; COMMIT frame=`25 01 34 3B 44 33 22 11`. CRC reference/vector arithmetic was checked while preparing this draft; that is not an implementation/target test.

## 12. GUI mapping independent of transport

The same codec/domain commands produce exactly these frames for PCAN, an ESP32 bridge, or a mock transport. The GUI must label hex IDs explicitly (0x321=801 decimal), distinguish remote/extended/status frames, and never turn adapter TX success into a motor ACK.

| GUI intent/view | Protocol mapping |
|---|---|
| Connect/discover | Backend connect at configured bitrate; wait for current Motor Status/session; do not START |
| Configuration editor | Range checks for display, then OPEN/WRITE/COMMIT with canonical CRC; node revalidates |
| START/direction | Serialize START and wait for completion + RUNNING; no automatic nonzero target |
| Duty slider | Debounce/coalesce to a latest SET_DEMAND; enforce active maximum; no hidden speed/torque claim |
| STOP | Cancel unsent motion/config work, highest host application priority; wait for safe-off |
| Keepalive worker | Fresh shared command sequence every 50 ms; independent of rendering/plotting; stop on disconnect/session invalidation |
| Status/error panel | Current state/session/config generation, mock indicator, fault bits, stale/disconnected indication |
| Feedback plots | Valid fields only, units/quality/mock label and host timestamps; missing sensor ≠ zero |
| Raw ESP32 GUI testing | Enter the same eight bytes/IDs manually; automatic heartbeat needed for sustained RUNNING beyond the lease |

PCAN-Basic is the planned backend, not a codec dependency. Use its Classic CAN initialize/read/write lifecycle through the selected Python binding; FD APIs are not needed. Confirm channel/DLL/driver/Python bitness and timestamp semantics during implementation. See [PEAK PCAN-Basic](https://www.peak-system.com/products/software/development-packages/pcan-basic/?L=1) for the vendor API; no binding/package was installed or exercised here. ESP32 remains a test peer/possible transport adapter; a Python-to-ESP32 serial/network framing contract is not present and must not be invented as an existing interface.

## 13. Agreement required

Approve ID allocation, the normalized-duty-only scope, heartbeat/lease policy, STOP exception, sequence/host-reconnect rules, transaction CRC/atomicity, session uniqueness strategy, direction/restart policy and backend deadlines before coding. Physical motor operation additionally requires hardware-backed safe inhibit, approved operating/protection envelope and truthful sensing capabilities. Refer to `CAN_Phase4_HLD.md` and `CAN_Phase4_Implementation_Plan.md` for ownership, source evidence and acceptance gates.
