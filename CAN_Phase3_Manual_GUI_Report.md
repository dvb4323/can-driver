# Phase 3 manual STM32 GUI test update

## Scope and result

Updated only the normal-mode application behavior in `src/main.c`, its application host tests in `tests/can/test_can_application.c`, and this change documentation. No new driver or module was introduced. The CAN wrapper, RTD integration, generated configuration, clocks, board initialization, project settings and hardware-verification gate are unchanged.

The current application test specification below supersedes the earlier Phase 3 automatic response-frame convention and normal-profile iteration budget. LOOPBACK remains the default compile-time profile and preserves the Phase 2 frame and bounded regression behavior.

| Profile | S32K one-shot TX | S32K expected RX | Application waiting policy |
|---|---|---|---|
| LOOPBACK=0 (default) | Standard 0x14, DLC 8, `01 02 03 04 05 06 07 00` | Same internal frame | Existing `CAN_TEST_POLL_BUDGET` applies |
| NORMAL_TWO_NODE=1 | Standard 0x123, DLC 8, `11 22 33 44 55 66 77 88` | Standard 0x321, DLC 8, `A1 B2 C3 D4 E5 F6 07 18` | No application iteration limit; wait for manual GUI transmission |

## Application changes

- A profile-specific constant TX frame supplies both the actual one-shot transmit and debugger-visible `request`, avoiding duplicate TX definitions.
- Init supplies the expected RX identifier to the existing wrapper's exact RX filter: 0x321 in normal mode, 0x14 in loopback. TX and RX identifiers are independent; no driver/filter API change is needed.
- Only LOOPBACK checks the application polling budget. NORMAL keeps servicing RX and diagnostics while waiting, and services TX until its completion is confirmed. `iteration_budget=0` explicitly reports the disabled normal-mode budget; setting `CAN_TEST_POLL_BUDGET` does not impose a normal-mode deadline.
- Iterations and TX/RX polling-call counters saturate at `UINT32_MAX` rather than wrapping during a long manual wait. Saturation does not terminate polling.
- PASS still requires separately confirmed TX completion and a received frame matching ID, DLC and every payload byte. RX arriving before software confirms TX completion is supported.
- Existing API failures, bus-off, mismatch and RX overrun still fail the test and preserve first failure/diagnostics. This change removes the short application wait limit, not error handling. A real RTD lifecycle/API timeout can still produce TIMEOUT.
- No automatic resend, overwrite, pending-TX cancellation, Stop or Deinit was added. An unacknowledged TX may remain pending and the controller may retry. The application does not issue another request.

Wrong identifiers are rejected by the configured hardware filter. A delivered frame with wrong DLC/payload still causes the existing strict mismatch failure; reset deliberately before retrying. No auto-response behavior is required from the STM32 application: the user sends the expected RX frame manually through the GUI.

## Debugger observations

`g_can_test` remains a volatile RAM result object. While waiting normally:

- `profile=1`, `mode=CAN_MODE_NORMAL`, `status=RUNNING` (1), `stage=POLL` (5).
- `request` is 0x123 / DLC 8 / `11 22 33 44 55 66 77 88`.
- `expected` is 0x321 / DLC 8 / `A1 B2 C3 D4 E5 F6 07 18`.
- `transmit_calls=1`, `tx_accepted=1`; `tx_completed` independently becomes 1 after acknowledged TX completion.
- Before the manual RX, `rx_completed=0`, RX polling returns NO_DATA, `iteration_budget=0`, and budget-exhausted flags remain zero. Match flags are not meaningful until a frame is delivered.
- `iterations`, `rx_poll_calls`, and cached CAN diagnostics continue updating; a long wait alone never enters the result-hold function.

After confirmed TX and a correct manual RX, expect PASS (2), TX/RX completed=1, ID/DLC/payload matches=1, mismatch mask=0, received frame equal to expected, and `tx_completion_unconfirmed=0`. At `CanTest_HoldResults`, inspect the complete result and first/latest error samples. PASS does not require raw ESR1 to be zero; it includes normal status bits and any earlier captured error evidence.

## S32DS build and debugger procedure

No build/debug command was run by the assistant. All builds and debugger actions remain user-operated through S32DS UI under the standing workflow.

1. Refresh the project with F5 and select **Build Configurations > Set Active > Debug_FLASH**. Keep `CAN_TEST_PROFILE` absent/0 for the default loopback regression, and leave `CAN_TEST_HARDWARE_VERIFIED` absent/0.
2. Use **Project > Build Project**. Share the console summary or first compiler error. Confirm `src/main.c` was rebuilt and the executable linked. These source changes do not require configuration generation or driver/board project changes. Until a new build succeeds, an existing ELF is stale for this update.
3. For source/structure inspection, verify compiler debug information and optimization settings through **Properties > C/C++ Build > Settings > Tool Settings**. Use debug information such as `-g3` and no optimization if needed; the previous report noted the stored configuration did not enable debug information. Rebuild after changing settings.
4. In a later authorized session with an electrically safe setup, use the existing board debug launch and the freshly built ELF. Break on code inside `CanTest_HoldResults` and add `g_can_test` in Expressions. Default LOOPBACK should still reach PASS with the original 0x14 frame.

**Physical CAN testing remains blocked until VP230 logic-voltage compatibility and onboard SBC RX isolation are verified and physical testing is authorized.** Confirm common GND, CAN_H/CAN_L, transceiver operating state, and termination as well. This software update does not satisfy those prerequisites or change the gate.

Only after those prerequisites and authorization:

1. Explicitly select `CAN_TEST_PROFILE=1` and `CAN_TEST_HARDWARE_VERIFIED=1` through the S32DS compiler-symbol UI and rebuild. The delivered defaults do not enable either. Profile 1 with gate 0 intentionally fails before pin/CAN initialization.
2. Configure the STM32 CAN node for normal, non-silent Classic CAN at the existing target 500 kbit/s, standard identifiers, data frames (no RTR/FD). Have its receiver running before starting S32K so it can acknowledge and display the one-shot S32K frame. GUI reception should accept ID 0x123.
3. Start/resume S32K. Confirm the GUI receives ID 0x123, DLC 8, `11 22 33 44 55 66 77 88`. The target stays RUNNING while waiting for manual input; no immediate reply is required. If using a debugger halt to inspect waiting status, resume the target before sending from the GUI.
4. In the GUI TX controls, choose **standard data frame**, enter ID **0x321** (check whether the GUI expects hexadecimal or decimal), DLC **8**, and bytes **A1 B2 C3 D4 E5 F6 07 18**. Send once manually. Do not use a periodic-send mode or an automatic echo rule.
5. Wait for the breakpoint in `CanTest_HoldResults`; inspect TX completion and the received frame/comparison fields. Only matching RX plus completed TX produces PASS. If no peer ACK is available, TX may stay pending even if RX arrives.
6. On failure preserve `g_can_test`, especially first failure, independent TX/RX flags and cached diagnostics. Avoid live ESR1 watches, which can consume read-sensitive event evidence. Do not resend/overwrite pending TX to recover; reset/terminate deliberately. The controller can continue retrying even after the application holds results. Remove power before wiring changes.
7. Restore profile 0 and gate 0 through UI and rebuild for loopback regression after any approved changes.

## Validation status

The application host tests were updated to check the independent normal TX/RX IDs and exact payloads, delayed RX and delayed TX completion beyond a deliberately tiny loopback budget, RUNNING during the manual wait, counter saturation, strict mismatch handling, diagnostics/fault preservation, the hardware gate, and unchanged loopback budget expiration. Tests were not compiled or executed. The existing host runner already selects loopback, blocked-normal and simulated-normal cases and needed no change.

`git diff --check` found no whitespace errors. A pre/post SHA-256 workspace comparison confirmed that only `src/main.c`, the application host test and the implementation-report addendum changed, with this report added and no files removed. Driver, board, RTD/generated configuration and existing build outputs retained their hashes. Target build result, host test execution and board/GUI communication are pending. No hardware success is claimed.
