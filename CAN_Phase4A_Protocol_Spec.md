# Phase 4A simple CAN protocol v1 — implemented software contract

Phase 4A's two IDs supersede the unapproved seven-ID Phase 4 draft. This specification describes the new codec/application/mock code, **not verified target execution**. The user-verified Phase 3 smoke test is preserved separately. No physical motor backend exists or is enabled.

## Transport and byte rules

Classic CAN, standard 11-bit data frames, no RTR/FD/BRS, **DLC exactly 8**. Existing target timing is retained (nominal 500 kbit/s). Host → MCU ID **0x100** (256 decimal); MCU → host ID **0x180** (384 decimal). The existing wrapper still owns CAN0/TX MB0/RX MB1; no multiple-RX extension is needed.

Offsets B0..B7 are wire bytes from zero. Multibyte integers are unsigned little-endian. No packed structure, pointer cast, application CRC, session ID or measured-feedback field is used. The pure decoder accepts explicit standard/remote metadata; on target the wrapper already rejects unsupported frame formats before the application sees a public frame.

### Command, ID 0x100

| Byte | Field | Meaning |
|---|---|---|
| B0 | sequence | u8, modulo-256 freshness/dedup rules below |
| B1 | opcode | 0..7 below |
| B2..3 | argument | u16 LE, units/valid values below |
| B4..7 | reserved | **All zero**, otherwise INVALID_ARGUMENT |

| Opcode | Name | Argument and behavior |
|---|---|---|
| 0 | STOP | 0. Valid from every state. Immediately inhibit mock, clear demand and cancel a pending START before backend completion or CAN TX work. Safe-off confirmation can be asynchronous |
| 1 | START_FORWARD | 0. Only configured STOPPED with fresh lease, max duty >0, backend safe/ready and no fault. Starts at zero duty |
| 2 | START_REVERSE | 0. Same guards plus explicit reverse support. Unsupported reverse gives SAFETY_INTERLOCK |
| 3 | SET_DUTY | 0..1000 **permille**, additionally ≤ configured max; RUNNING only. 1 unit=0.1% normalized duty magnitude; not rpm/torque/current |
| 4 | SET_MAX_DUTY | 0..1000 permille, only safely UNCONFIGURED/STOPPED, no pending operation. Sets a software cap and transitions STOPPED; never enables. Zero is a valid configured cap but inhibits START |
| 5 | GET_STATUS | 0. ACK plus coalesced current status. Does not refresh motion lease |
| 6 | KEEPALIVE | 0. Fresh accepted heartbeat refreshes lease even in FAULT, allowing deliberate recovery when other causes are absent. Never starts or clears fault |
| 7 | CLEAR_FAULT | 0. Requires FAULT, fresh lease, safe-off confirmed, backend fault input gone, no pending operation and no reset-required cause. Goes to STOPPED/UNCONFIGURED, duty zero; never restarts |

Changing direction requires STOP, confirmation, then a new START. START while STARTING/RUNNING/STOPPING is invalid; no reverse-on-the-fly operation. A SET_DUTY packet cannot implicitly start. All limits above are wire/software bounds, not approved motor/bridge electrical limits.

Wrong DLC or unknown opcode gives INVALID_COMMAND; reserved/nonzero no-argument fields and argument >1000 give INVALID_ARGUMENT. Invalid commands have no motor effect and do not refresh the lease. Wrong IDs/extended/remote frames are dropped without response; malformed known-ID frames generate an ACK/NACK only if at least B0/B1 are present. No absent byte is read; an invalid-DLC response echoes argument zero. With DLC 8 the response echoes the decoded argument even if invalid. Wrapper-level INVALID_FRAME is a control-path fault, not a valid command.

## Response, ID 0x180

All types use DLC 8; B7 always 1. All bytes have explicit meaning; there is no unspecified padding.

| Byte | ACK_NACK (B0=1) | MOTOR_STATUS (B0=2) | MOTOR_ERROR (B0=3) |
|---|---|---|---|
| B1 | Echo command sequence | 0 | 0 |
| B2 | Result code | State code | Current latched primary fault code, or 0 after clear |
| B3 | Echo opcode, including unsupported opcode when correlatable | Flags | Flags |
| B4..5 | Echo decoded command argument; 0 if invalid DLC | Requested duty, u16 LE permille | Requested duty, normally 0 after safe-off |
| B6 | Primary fault at response creation | Current primary fault | Same primary fault as B2 |
| B7 | Protocol version 1 | Protocol version 1 | Protocol version 1 |

ACK payloads are snapshots stored in a bounded queue; a retried cached response can contain historical fault/result values. Current status/error is authoritative for current state. Backend acceptance, CAN transmit completion, CAN link-layer ACK and this application ACK are different events. SET_DUTY OK means the mock stored the requested duty, not that a real shaft reached a speed.

### Result codes

| Code | Name | Meaning |
|---|---|---|
| 0x00 | OK | Synchronous acceptance/completion: max duty, duty, heartbeat, query, clear, or already-safe STOP |
| 0x01 | PENDING | Accepted asynchronous START/STOP; does not prove enabled/stopped yet |
| 0x02 | COMPLETED | Backend confirmed START at zero demand or confirmed STOP safe-off |
| 0x80 | INVALID_COMMAND | Unknown opcode or invalid length |
| 0x81 | INVALID_ARGUMENT | Reserved bytes/nonzero no-argument field/range/configured cap violation |
| 0x82 | INVALID_STATE | Command not allowed in this state |
| 0x83 | NOT_CONFIGURED | START before SET_MAX_DUTY |
| 0x84 | MOTOR_FAULT | START blocked by latched fault/backend fault, or backend operation failure |
| 0x85 | BUSY | Backend busy or insufficient reserved reply capacity; no accepted action |
| 0x86 | SEQUENCE_CONFLICT | Retained sequence reused with a different valid eight-byte request |
| 0x87 | SAFETY_INTERLOCK | No fresh lease, cap zero, reverse unavailable, clear blocked, or accepted operation superseded by STOP/fault |
| **0x88** | **CAN_ERROR** | New distinct result for an asynchronous operation interrupted by CAN API/bus-off fault |
| **0x89** | **INTERNAL_TIMEOUT** | New distinct result for the mock backend's 100 ms operation deadline |
| **0x8A** | **STALE_SEQUENCE** | New distinct result for a non-retained stale/equal sequence |

The last three codes are explicit Phase 4A extensions, avoiding misleading reuse of MOTOR_FAULT/INVALID_ARGUMENT for transport/timeout/freshness errors. Reserved result codes are unknown errors to a GUI, never success. Capacity, guard and backend checks have deterministic code order; for example no reply capacity can produce BUSY before state validation. START in a configured fault state gives MOTOR_FAULT, while SET_DUTY outside RUNNING gives INVALID_STATE.

### State codes

| B2 | State | Contract |
|---|---|---|
| 0 | UNCONFIGURED | Safe startup, zero duty, no valid max configuration |
| 1 | STOPPED | Configured but inhibited, zero duty; not proof of physical standstill |
| 2 | STARTING | Awaiting matching backend operation confirmation; target zero |
| 3 | RUNNING | Backend confirmed simulated enable; SET_DUTY allowed |
| 4 | STOPPING | Inhibit already requested; awaiting confirmation, demand zero |
| 5 | FAULT | Latched inhibit; confirmation may still be pending. Check safe-off flag |

### Status/error flags, B3

| Bit | Meaning |
|---|---|
| 0 | Configured software maximum is valid, including a zero cap |
| 1 | Backend confirms safe-off; this is not a measured zero speed |
| 2 | Mock backend. Always set for Phase 4A, including simulated RUNNING |
| 3 | Reverse explicitly supported by the mock capability |
| 4 | Accepted-command lease is fresh |
| 5 | Asynchronous START/STOP result pending |
| 6 | Backend's selected/last direction is reverse; can remain set while stopped |
| 7 | Reserved zero |

No rpm, current, temperature or real motor feedback is fabricated. Requested duty and direction are command/state information.

### Fault codes, B6 and error B2

| Code | Cause / clear policy |
|---|---|
| 0 | None; error type with 0 reports an authorized clear |
| 1 | Communication lease expired |
| 2 | Backend fault/failure/unexpected enable outside RUNNING |
| 3 | CAN wrapper/API/state failure — reset required |
| 4 | CAN bus-off observed — reset required even if controller recovers |
| 5 | RX overrun/lost command history |
| 6 | Wrapper INVALID_FRAME |
| 7 | Response queue overflow |
| 8 | Invalid/uncertain/backward time — reset required |
| 9 | Mock backend operation timeout |

The first latched cause remains primary until authorized clear/reset. A local fault bitmap preserves additional causes; separate first-ever fault/time/diagnostics remain available even after CLEAR_FAULT. All causes force demand zero/inhibit. Codes 1/2/5/6/7/9 can clear only with safe-off confirmed, a new fresh heartbeat/accepted command and no active backend fault/pending operation. Codes 3/4/8 require a deliberate reset/reinitialization; bus-off sticky diagnostics are not automatically cleared to permit restart.

## Deduplication, ordering and STOP exception

The application keeps eight request/result records. Each contains sequence and exact validated eight-byte identity, current result and result-time fault. Completed entries expire after 1000 ms and may be evicted earlier by the fixed capacity (oldest terminal record); pending async entries stay pinned. Retries are guaranteed only while retained. An expired/evicted old request is rejected by sequence ordering rather than re-executed under the bounded-delay assumption.

After format/argument validation, look up sequence: exact retained identity replays PENDING/terminal result without another action **or lease refresh**; changed valid content yields SEQUENCE_CONFLICT. Otherwise first request establishes baseline; modulo delta `(received-last_consumed)&255` must be 1..127. Delta 0 or 128..255 gives STALE_SEQUENCE. Wrap 254→255→0→1 is supported. Fresh semantic NACKs consume/cache sequence; format-invalid and capacity-rejected requests do not. Host uses one shared sequence stream across all opcodes and retransmits the exact bytes of a pending request.

**A fully valid STOP bypasses freshness/conflict rejection and always requests inhibition**, even when stale, repeated or responses cannot transmit. It does not refresh the lease. A duplicate pending STOP returns PENDING without extending its stop deadline. If it supersedes START, START gets a terminal SAFETY_INTERLOCK where reply capacity was reserved; late completion cannot re-enable. A stale/conflicting STOP need not replace the unrelated cache record. Already-confirmed safe STOP returns OK; otherwise PENDING then COMPLETED. Fault state is not cleared by STOP.

**Limitations:** u8 modulo order cannot detect arbitrary delayed replay after enough wrap, and this protocol has no session/authentication field. Cache reset loses previous identities. On MCU reset the mock always becomes unconfigured/inhibited with demand zero; there is no stored-motion restoration. GUI reconnect must discard unsent START/duty, STOP first, query state, deliberately configure/rearm, and never automatically replay motion. Recorded old configuration+START sequences cannot be made cryptographically safe with this layout; physical enable remains unavailable until replay/rearm policy is reviewed. STOP is not a substitute for an independent hardware emergency stop.

## Lease, asynchronous operations and queues

- GUI sends a **fresh KEEPALIVE every 100 ms**. The mock motion lease expires at **age ≥500 ms** in STARTING/RUNNING. Time is supplied as monotonic modulo-2^32 milliseconds; iteration counts and OSIF_COUNTER_DUMMY are never used as milliseconds.
- Fresh successful KEEPALIVE, SET_MAX_DUTY, SET_DUTY, CLEAR_FAULT and accepted START refresh the lease. START needs an already-fresh lease (a recent accepted configuration or heartbeat suffices). GET_STATUS, STOP, malformed/NACKed/duplicate packets and ordinary bus traffic do not refresh it.
- Deadline check precedes RX dispatch, so a heartbeat arriving after expiry cannot prevent the latched safe action. A later heartbeat permits recovery checks, never automatic restart.
- Mock START delay defaults to 10 ms, stop confirmation 1 ms; drive inhibition and demand-zero happen immediately on STOP/fault request. Each asynchronous operation has a **100 ms mock-only completion deadline**; timeout latches code 9 and cancels the pending START. These are simulation requirements, not physical stop/protection deadlines.
- Each Process turn handles at most one RX, one backend service, one TX completion poll and one new TX submission. Loops inside codec/cache are fixed-size. No TX BUSY wait/delay loop or heap allocation exists.
- ACK queue capacity is eight; an active async operation reserves one slot for its terminal result. START/async STOP requires initial plus terminal room. Other requests cannot consume the reservation. Insufficient space gives BUSY when a rejection fits; actual overflow faults/inhibits. STOP always inhibits even if it cannot queue a reply.
- One stable application TX frame remains owned through wrapper completion. Error event has priority over queued ACKs, then coalesced Status. Status defaults to 100 ms; latched errors repeat at most on the 1000 ms due schedule (new fault/clear events can report sooner). MB0 cannot be preempted; permanent BUSY prevents wire replies but not RX, lease or motor safe-off.
- RX_OVERRUN discards the unreliable motion command and latches fault; a recognizable valid STOP is honored before faulting. Invalid wrapper metadata and CAN API errors fault safely. A controller CAN ACK does not confirm application execution.

## Golden vectors

All entries below contain exactly eight bytes. Example context: mock supports reverse; max=700 permille, current forward duty=300 where indicated.

| ID | Meaning | Data |
|---|---|---|
| 0x100 | seq1 SET_MAX_DUTY 700 | `01 04 BC 02 00 00 00 00` |
| 0x180 | ACK OK for that max | `01 01 00 04 BC 02 00 01` |
| 0x100 | seq2 START_FORWARD | `02 01 00 00 00 00 00 00` |
| 0x180 | START PENDING | `01 02 01 01 00 00 00 01` |
| 0x180 | START COMPLETED | `01 02 02 01 00 00 00 01` |
| 0x100 | seq3 SET_DUTY 300 | `03 03 2C 01 00 00 00 00` |
| 0x180 | ACK OK duty 300 | `01 03 00 03 2C 01 00 01` |
| 0x180 | RUNNING, configured/mock/reverse-capable/fresh, requested duty 300 | `02 00 03 1D 2C 01 00 01` |
| 0x100 | seq4 GET_STATUS | `04 05 00 00 00 00 00 00` |
| 0x100 | seq5 KEEPALIVE | `05 06 00 00 00 00 00 00` |
| 0x100 | seq6 STOP | `06 00 00 00 00 00 00 00` |
| 0x180 | STOPPED, configured/safe/mock/reverse-capable/fresh | `02 00 01 1F 00 00 00 01` |
| 0x180 | Confirmed safe-off after communication timeout; lease stale | `03 00 01 0F 00 00 01 01` |
| 0x100 | seq7 CLEAR_FAULT (send a fresh heartbeat first) | `07 07 00 00 00 00 00 00` |
| 0x100 | seq8 unknown opcode | `08 63 00 00 00 00 00 00` |
| 0x180 | Unknown opcode NACK, no existing fault | `01 08 80 63 00 00 00 01` |

For sustained GUI testing use automatic fresh heartbeat sequences, not the fixed seq5 vector repeatedly. Manual per-frame typing cannot reliably sustain a 500 ms lease. The old Phase 3 0x123/0x321 frames are regression traffic, not Phase 4A commands.
