# S32K144 CAN Hardware Analysis

Date: 2026-10-15. Scope: CAN0 initialization, Classic CAN, TX MB0, RX MB1, and migration to an independent polling driver. This report supplements `CAN_Project_Analysis.md`; no existing source or project configuration was changed.

## 1. Conclusions and evidence conventions

**CLKSRC survives the successful initialization path.** The later software write of zero to CTRL1 does not clear CLKSRC while CAN is enabled: that bit is writable only in Disable mode. CTRL1 is also unaffected by soft reset. This resolves the uncertainty in section 4.2 of the earlier report. It does not establish measured board behavior if initialization fails or clocks are unavailable.

**The S32K144-specific CAN host clock is SYS_CLK, not BUS_CLK.** CLKSRC=1 selects SYS_CLK for the protocol engine too. Both happen to be configured to 48 MHz in this project, which hid the distinction in the initial analysis.

Evidence labels:

- **V-code:** verified source statement, call, configuration, or absence within the inspected project. Not evidence of execution.
- **V-manual:** verified hardware rule in the installed S32K reference manual.
- **Derived:** expected result on the successful path using those hardware rules.
- **Unresolved:** requires hardware readback, actual board/silicon identification, or further documentation.
- **Recommendation:** proposed behavior of the future driver, not an implemented change.

Short source names used in tables resolve as follows; all line numbers are one-based in the current files:

| Alias | Source file |
|---|---|
| APP | `src/main.c` |
| IP | `RTD/src/FlexCAN_Ip.c` |
| HW | `RTD/src/FlexCAN_Ip_HwAccess.c` |
| HH | `RTD/include/FlexCAN_Ip_HwAccess.h` |
| DEV | `RTD/include/FlexCAN_Ip_DeviceReg.h` |
| CFG | `generate/src/FlexCAN_Ip_Sa_BOARD_InitPeripherals_PBcfg.c` |
| CLK | `generate/src/Clock_Ip_Cfg.c` |
| CLKDATA | `RTD/src/Clock_Ip_Data.c` |
| IRQ | `RTD/src/FlexCAN_Ip_Irq.c` |
| INTC | `RTD/src/IntCtrl_Ip.c` |

The project root is `C:/Users/Admin/workspaceS32DS.3.4/FlexCAN_Ip_example_S32K144/`.

**RM** means the installed **S32K1XXRM Rev. 12.1, 02/2020**, read at:

`C:/NXP/S32DS.3.4/S32DS/help/resources/manuals/k1dev/S32K-RM.pdf`

RM citations give section/table and printed page, which matches the PDF page number in this copy. Manuals have page/section references rather than source-code line numbers. Relevant complete text pages were extracted and inspected. This installed manual is not assumed to be the newest revision.

The installed device-header directory, abbreviated **BASEHDR**, is:

`C:/NXP/S32DS.3.4/S32DS/software/PlatformSDK_S32K1_2022_02/SW32K1_RTD_4_4_1_0_1_D2202/Base_TS_T40D2M10I1R0/header/`

## 2. Initialization call path and every reachable CTRL1/MCR write

APP:66 invokes init; `RTD/include/FlexCAN_Ip.h:212` and `FlexCAN_Ip_Wrapper.h:143` route to `FlexCAN_Ip_Init_Privileged` (IP:1876). It calls controller setup (IP:1908), baud configuration (1912), and mode configuration (1914). APP:67 separately starts the module. Instance 0 selects CAN0 through `Flexcan_Ip_apxBase` (IP:190); BASEHDR `S32K144_FLEXCAN.h:153` defines base `0x40024000`.

The sequence below covers all **software writes to CTRL1/MCR reachable during successful initialization and start for the current configuration**, including conditional prior-state and failure paths. Hardware self-updates such as SOFTRST clearing and FRZACK/LPMACK changes are distinguished from CPU writes. Read-modify-write expressions constitute register writes even when the resulting field is unchanged.

| Order / condition | Source and line | Register/field and verified CPU operation | Expected behavior / polling-driver recommendation |
|---|---|---|---|
| 1. Only if initially enabled | IP:428–434; HW:526 | MCR.FRZ=1 | V-code: requests permission to freeze. Recommendation: handle warm initialization explicitly. |
| 2. Same branch | HW:527 | MCR.HALT=1 | V-code: request Freeze; HW:536 waits FRZACK. Recommendation: require acknowledgment with a real deadline before changing protected fields. |
| 3. Conditional inside freeze helper | HW:528–530 | MCR.MDIS=0 if set | V-code: helper can enable a disabled module. On the ordinary initially-enabled branch, this extra clear is not needed/executed. Recommendation: keep state transitions explicit. |
| 4. Same initially-enabled branch | HW:632–642 | MCR.MDIS=1, wait LPMACK=1 | V-code: puts CAN in Disable before selecting its clock. Recommendation: verify both request and acknowledgment. Cold-reset entry skips steps 1–4 because MDIS is already set. |
| 5. Clock selection | IP:442; HH:1349; CFG:152 | CTRL1.CLKSRC=1 | V-code: selects peripheral source while disabled. V-manual: S32K144 maps this to SYS_CLK. Recommendation: read back selection while disabled. |
| 6. Enable | IP:445 | MCR.MDIS=0 | V-code: enables clocks before reset/RAM access. Recommendation: wait for the appropriate enabled/freeze state, not just the CPU write. |
| 7. Soft reset | HW:727 | MCR.SOFTRST=1 | HW:730 polls until hardware clears it. V-manual: MDIS and CTRL1 are not reset by this operation. Recommendation: keep both source clocks available during the handshake. |
| 8. After reset succeeds | HW:742 | MCR.AEN=1 | V-code: enables abort mechanism. Do not interpret the nearby comment as clearing AEN; the executable expression sets it. Recommendation: choose a documented TX abort policy and handle its completion states. |
| 9. Clear controls | HW:779; DEV:98 | CPU writes CTRL1=0 | V-code: entire word written. V-manual: CLKSRC write is blocked outside Disable; it remains 1. Other writable fields clear. Recommendation: use deliberate field programming and explain the protected bit rather than relying on a confusing full-word write. |
| 10. Classic mode | IP:467; HH:673 | MCR.FDEN=0 | V-code: FD capability exists on CAN0, but config disables it. Recommendation: explicitly disable FD/BRS/TDC for Classic-only operation. |
| 11. Sampling option | IP:477; HW:2462–2468; HH:1338 | CTRL1.SMP=0 | V-code: three-sample option absent. Recommendation: make sample mode explicit. |
| 12. Bus-off option | HW:2472–2474; HH:1316; CFG:141 | CTRL1.BOFFREC=0 | V-code: enabled automatic recovery uses inverted bit semantics. Recommendation: define automatic recovery versus application recovery policy; do not set BOFFREC based on its name alone. |
| 13. MB allocation | IP:403; HW:1611; CFG:116 | MCR.MAXMB=16−1=15 | V-code/Derived: MB0–15 enabled. Recommendation: validate capacity/allocation and clear required RAM independently of active MB count. |
| 14. Clear nominal timing fields | IP:530; HH:967–971 | CTRL1 clears PROPSEG, PSEG1, PSEG2, PRESDIV, RJW | V-code: first timing write; preserves other readable fields including CLKSRC. Recommendation: configure only in acknowledged Freeze and validate all values. |
| 15. Set nominal timing fields | HH:973–979; CFG:160–168 | CTRL1 PROPSEG=7, PSEG1=4, PSEG2=1, PRESDIV=5, RJW=1 | V-code: second timing write. Recommendation: encode physical timing values in one controlled write after validation. |
| 16. Loopback | IP:1914; HW:1652 | CTRL1.LPB=1 | V-code: internal loopback, not physical transmission. Recommendation: explicit diagnostic mode separate from normal mode. |
| 17. Loopback | HW:1653 | CTRL1.LOM=0 | V-code: not listen-only. Recommendation: reject TX in a separately supported listen-only mode. |
| 18. Self reception | HW:1655; HH:1200 | MCR.SRXDIS=0 | V-code: allows self reception for loopback. Recommendation: select self-reception policy intentionally for each mode. |
| 19. Start, separate APP call | IP:3704 | MCR.MDIS=0 | V-code: redundant enable in successful path. Recommendation: enforce initialized/stopped state before start. |
| 20. Start | HW:598 | MCR.HALT=0 | V-code: releases halt. Recommendation: start after RX setup in the new driver. |
| 21. Start | HW:599 | MCR.FRZ=0 | HW:604 waits FRZACK=0. Recommendation: verify readiness/synchronization using bounded handshakes; clearing FRZ also changes debugger-freeze behavior. |

**Derived final CTRL1:** `0x05613007` for this normal successful initialization/start sequence, before any later control changes: PRESDIV=5, RJW=1, PSEG1=4, PSEG2=1, CLKSRC=1, LPB=1, PROPSEG=7. LOM/SMP/BOFFREC and interrupt masks are zero. This is an expected readback, not a captured value. MCR status bits change asynchronously, so this report does not invent one constant final MCR word.

### 2.1 Failure and excluded branches

| Finding and source | Register/bitfield | Verified scope / recommendation |
|---|---|---|
| Controller reset fails: IP:448–452 | MCR.FRZ, HALT, conditional MDIS clear, then MDIS set through HW:526–642 | V-code: cleanup calls freeze/disable and discards cleanup return values. Recommendation: preserve original failure and report safe-state failure separately. |
| FIFO setup fails: IP:388–394 | Same freeze/disable writes | V-code: conditional cleanup; current FIFO-off branch normally succeeds. Recommendation: do not copy dead feature branches into a minimal driver. |
| MB-count validation fails: IP:403–408 | Same freeze/disable writes | V-code: cleanup branch. Recommendation: validate allocation before register mutation. |
| Nonloopback self-reception setup: IP:382–384 | MCR.SRXDIS=1 via HH:1200 | **Not executed** for CFG:138 loopback. Recommendation: define normal-mode self reception explicitly. |
| FIFO/DMA feature paths: IP:289–291, 320–366; HW:826; HH:716 | MCR.RFEN/DMA | FIFO not requested; DMA compile feature is off (`generate/include/FlexCAN_Ip_Cfg.h:160`). No explicit FIFO/DMA MCR write occurs in the current init path; reset supplies their initial zero state. Recommendation: explicitly establish these disabled states during custom init. |
| User-mode support branch: IP:1915–1920 | MCR.SUPV=0 | Compiled out by `generate/include/FlexCAN_Ip_Cfg.h:117` STD_OFF. Recommendation: match access privilege to retained startup, without AUTOSAR wrappers in public CAN API. |
| Error interrupt helper: HW:662–705 | CTRL1 masks and MCR.WRNEN | Not called by current initialization/application. Recommendation: leave error masks zero and poll errors. |
| RX individual-mask helper: HH:1470/1475 | MCR.IRMQ | Not called by current application/init. Recommendation: explicitly choose global versus individual masks. |
| Default-register helper: HH:1676/1683 | CTRL1=0; MCR=`0xD890000F` | **Deinitialization**, not initialization: called IP:3486 after entering Freeze. Recommendation: keep shutdown writes separate in the trace. |

APP:74 stop adds Freeze requests (IP:3720 onward -> HW:526/527) and Disable (HW:637). APP:75 deinit enters Freeze again, resets register defaults, then disables (IP:3482–3488). Thus a debugger inspection after program exit cannot be treated as an initialization snapshot.

## 3. CLKSRC survival: hardware proof and limits

| Evidence | Register/bitfield | Verified rule or result | Recommendation |
|---|---|---|---|
| CFG:152; IP:442; HH:1349 | CTRL1.CLKSRC bit 13 | V-code: requests 1 before MDIS is cleared. | Select source only in acknowledged Disable. |
| RM table 55-6, p.1806; section 55.4.2.3.2, p.1814 | CTRL1, SOFTRST | V-manual: CTRL1 is unaffected by soft reset. | Do not assume soft reset supplies control-register defaults. |
| RM section 55.4.2.3.4, p.1816 | CTRL1.CLKSRC | V-manual: writable only in Disable, blocked otherwise. | Read back CLKSRC and preserve it deliberately. |
| RM section 55.4.2.2.4, p.1810 | MCR.MDIS | V-manual: MDIS is itself unaffected by soft reset. | Establish mode with handshakes before reset/configuration. |
| IP:445 -> HW:727 -> HW:779 | MDIS=0; SOFTRST; CTRL1 write zero | Derived: CLKSRC remains 1 through both reset and the software zero-write. Later timing/mode writes preserve it. | Treat a source-loss bug as unsupported by this trace; validate final hardware state instead. |

**Answer:** yes, for the successful path respecting the documented hardware state. The previous report's approximately 83.33 kbit/s alternative is not the expected consequence of `CTRL1=0` here. Measured frequency, actual initialization success, debugger interference, reset-entry conditions, and final register readback remain unverified. APP:66–75 ignores returned status values, so it does not prove the path succeeded.

## 4. CAN0 clock dependencies

RM table 27-8 p.567 and table 27-9 p.572 establish the S32K144 mapping; generic FlexCAN terminology alone is insufficient.

| Dependency and source/line | Register/field | Verified configuration / assumptions | Custom polling-driver recommendation |
|---|---|---|---|
| APP:42; CLK:161–168, 227–228 | SCG FIRC enable/range; RCCR.SCS | V-code: FIRC enabled, RUN source FIRC. `ClockYaml.txt:11,20,22` reports 48 MHz core/FIRC/CAN0. | Pass verified SYS_CLK Hz to CAN timing logic; read back SCG source/dividers after setup. |
| CLK:509–510, 539–540, 569–570 | SCG RCCR.DIVCORE, DIVBUS, DIVSLOW | V-code: divisors 1,1,2. Derived configured RUN core/bus/slow=48/48/24 MHz. | Do not assume SYS_CLK and BUS_CLK remain equal after future clock changes. |
| CLK:776–777; CLKDATA:923; BASEHDR `S32K144_PCC.h:157` | PCC.PCCn[36].CGC | V-code: CAN0 gate enabled, gate-only mapping. V-manual: SYS_CLK supplies CHI through this gate. | Enable gate before register access; do not use PCC.PCS as CAN PE source selection. |
| CFG:152; HH:1349; RM p.572 | CTRL1.CLKSRC=1 | V-code/V-manual: PE receives SYS_CLK, synchronous with CHI. Derived current input 48 MHz. | Guarantee frequency/tolerance for the selected CAN timing and mode. |
| CLK:177–185, 479–480; RM p.572 | SCG SOSC, SOSCDIV2; CLKSRC=0 alternative | V-code: 8 MHz oscillator configured and divided by 1; not selected for current PE. External crystal/frequency physically unverified. | If choosing oscillator PE clock, confirm valid SOSCDIV2 and SYS_CLK/PE ratio rather than reusing old timing blindly. |
| RM p.567, 1935 table 55-32 | CHI/PE relationship and clocks per CAN bit | V-manual: synchronous source may operate 1:1; asynchronous constraints apply, with clock summary specifying SYS_CLK >1.5× protocol clock. Table 55-32 requires 16 CHI clocks/bit for 16 MBs, RFEN=0. | Use the device-specific stronger ratio requirement; current 48 MHz/500 kbit/s=96 clocks/bit satisfies the minimum. |
| CLK:196–214, 489–500; APP:43–50 | SCG SPLL and lock | V-code: PLL configured but RUN selector is FIRC. APP PLL wait/distribution is guarded by `FEATURE_CLOCK_IP_HAS_SPLL_CLK`; no definition was found in inspected project/Base headers or saved argument file. | Do not claim this guarded block executes. PLL is not a direct source dependency for the current CAN source; shared clock initialization may still depend on enabled SOSC/PLL setup succeeding. |
| CLK:958–959; BASEHDR `S32K144_PCC.h:179` | PCC.PCCn[77].CGC for PORTE | V-code: gate enabled; required for PCR programming, distinct from CAN clock gate. | Keep pin access clock ownership in board/platform initialization. |
| `generate/include/FlexCAN_Ip_Cfg.h:136–139`; `RTD/src/OsIf_Timer.c:198–220` | Timeout source, no CAN register | V-code: dummy counter returns 0, elapsed returns 1, microseconds conversion returns input. These are loop counts, not calibrated microseconds. | Use a free-running independent monotonic counter without a timer ISR; bound reset/freeze waits and application waits. |

The current encoded timing (CFG:160–168; HH:967–979) gives divisor 6, PROP=8 tq, PSEG1=5 tq, PSEG2=2 tq, SJW=2 tq. **Derived** with SYS_CLK=48 MHz: `48 MHz/(6×16)=500 kbit/s`, sample point `14/16=87.5%`. This calculation does not validate FIRC tolerance over the intended operating conditions. Recommendation: validate oscillator tolerance and network timing margins for physical operation.

## 5. Missing CAN0 pin mux and EVB setup

### 5.1 Missing pin configuration

| Source/line | Register/field | Verified versus unresolved | Recommendation |
|---|---|---|---|
| `FlexCAN_Example_S32K144.mex:19–37` | PORTE.PCR[4/5].MUX | V-code: Pins code update disabled, `BOARD_InitPins` has empty pin list. Referenced `board/Port_Ci_Port_Ip_Cfg.*` files are absent. | Add application-owned board pin setup in a later implementation change. |
| APP:39–76; `Project_Settings/Startup_Code/system.c:340` | PORTE PCR / transceiver controls | V-code: no CAN pin init call; SystemInit supplies CPU/access/cache setup, not CAN pin routing. Project searches found no CAN PCR programming. | Explicitly call board setup before normal CAN operation. |
| CLK:958–959 | PCC.PCCn[77].CGC | V-code: PORTE clock available; not a mux assignment. | Confirm gate then configure pin fields intentionally. |
| NXP AN5413 Rev.2 section 2.10.3.1, `PORT_init`; EVB guide HMI mapping | PCR[4].MUX=5, PCR[5].MUX=5 | External verified mapping: PTE4=CAN0_RX, PTE5=CAN0_TX, ALT5. Physical board wiring/revision remains unverified. | Configure ALT5 with masked field replacement; select pulls/drive characteristics from board requirements, avoid assuming `|=MUX(5)` replaces a nonzero mux. |

Pin mapping references: [NXP cookbook Rev.2](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/S32K/3855/2/AN5413_rev2.pdf), [NXP EVB guide](https://www.nxp.com/document/guide/getting-started-with-the-s32k144evb%3ANGS-S32K144EVB). A GPIO PDDR write alone does not route CAN signals. RM p.1816 verifies that loopback ignores the RX pin and holds TX recessive, explaining why missing mux does not prevent this internal test.

### 5.2 What available project files actually establish about the transceiver

| Source/line | Register/bitfield or interface | Verified finding / uncertainty | Recommendation |
|---|---|---|---|
| `description.txt:12–14` | Board identity / CAN external path | Names RevA PCB 29248 SCH RevC and says no CAN connections needed for this loopback example. No SBC register values or initialization sequence provided. | Do not apply loopback wiring instructions to physical-bus tests. Identify the actual board. |
| APP:17–19, 42–75; `generate/include/modules.h:215` | SPI/SBC interface | No SBC/transceiver API calls; SPI module marked off. No UJA1169/MC33903 driver or SBC register transaction found in project sources. | Implement board-specific setup only after identifying the populated SBC and its operating state. |
| CLK:353–354, 860–861 | LPSPI1 clock mux/gate | V-code: LPSPI1 clock source SIRCDIV2 and gate enabled. This is not an SPI/SBC driver or proof of a transaction. | If SBC configuration is needed, supply SPI mux/configuration and bounded transfer logic separately. |
| EVB guide HMI mapping (external, not project code) | PTB14 SCK, PTB15 SIN, PTB16 SOUT, PTB17 PCS3 | External reference for SBC SPI wiring; project has no implementation. | Confirm actual schematic and pin alternate functions before using this route. |
| NXP EVB product documentation (external) | UJA1169TK SBC/CAN transceiver | Reference EVB uses UJA1169TK; exact project board population not established. | Confirm power, CAN mode, standby/normal state, watchdog policy and status via the actual SBC datasheet/schematic. |

[NXP EVB product description](https://www.nxp.com/products/processors-and-microcontrollers/s32-automotive-platform/s32k-auto-general-purpose-mcus/s32k144-q100-evaluation-board-for-automotive-general-purpose%3AS32K144EVB) identifies the reference SBC. The [NXP cookbook](https://www.nxp.com/docs/en/application-note/AN5413.pdf) also distinguishes older MC33903 boards. **No exact SBC register-write sequence can be recovered from this checkout.** Do not invent one or assume power-on/default transceiver state. Normal-mode validation additionally needs confirmed CANH/CANL routing, suitable power, reference ground, termination and an acknowledging peer. Those hardware details are unresolved, not missing software constants that can safely be guessed.

## 6. Exact current MB0/MB1 access sequence

### 6.1 Register map and initial clearing

BASEHDR `S32K144_FLEXCAN.h:99–123,153` defines CAN0 base and offsets. HH:148/176 define standard-ID shift 18 and CODE shift 24. HW:435–486 calculates the current eight-byte MB stride as 16 bytes.

| Word | TX MB0 | RX MB1 |
|---|---|---|
| CS | RAMn[0] = `0x40024080` | RAMn[4] = `0x40024090` |
| ID | RAMn[1] = `0x40024084` | RAMn[5] = `0x40024094` |
| DATA0 | RAMn[2] = `0x40024088` | RAMn[6] = `0x40024098` |
| DATA1 | RAMn[3] = `0x4002408C` | RAMn[7] = `0x4002409C` |

Addresses/values in this section are **Derived** from source, not recorded bus accesses. IFLAG1=`0x40024030`; IMASK1=`0x40024028`; TIMER=`0x40024008`.

V-code: HW:294–307 zeroes MB RAM and RXIMR memory using hardware-capacity loops; HW:746–750 initializes global/MB14/MB15 masks; HW:752–754 sets IMASK1=0 and W1C-clears flags. HW:1611–1625 sets MAXMB=15, then clears CS/ID/data of the 16 active MBs again. RM table 55-6 pp.1806–1807 says MB/mask memory is not automatically reset. Recommendation: initialize RAM and explicitly select masks; don't treat zeroed RXIMR as active filtering unless IRMQ selects it.

### 6.2 RX MB1 configuration, before transmit

APP:68 -> IP:2021–2061 -> HW:1255–1301. For each call, `FlexCAN_SetRxMsgBuff` first clears CS and ID, then sets format/ID, then CODE if not the sentinel. **The sentinel `RX_NOT_USED` is not itself written as a hardware CODE.**

| Order | Source/line | Actual register operation for standard ID 20 | Verification / recommendation |
|---|---|---|---|
| 1 | IP:2043; HH:786 | `IFLAG1 = 0x00000002` | V-code: direct W1C. Preserve this owned-bit pattern. |
| 2 | IP:2052–2053; HW:1270–1271,1290–1294 | CS1=0; ID1=0; CS1 RMW clears IDE/SRR; ID1 RMW clears standard-ID field then ORs `0x00500000` | V-code; resulting values derived. RX_NOT_USED suppresses final CODE write (HW:1298). Recommendation: use a validated inactive-configure-empty sequence with minimal writes. |
| 3 | IP:2056–2057; same helper | Repeat CS/ID reset and standard-ID setup; CS CODE RMW writes RX_INACTIVE=0 | V-code: repeated initialization, no RX data words written by this helper. Recommendation: do not reproduce redundant steps without a hardware need. |
| 4 | IP:2060–2061; HW:1298–1301 | Repeat CS/ID reset/setup; CS CODE RMW writes RX_EMPTY=4, final CS1=`0x04000000` | Derived final state: RX armed with IDE=0 and ID=`0x00500000`. Recommendation: activate receive only after ID/filter setup. |

HH:276–283 defines RX codes. Although APP metadata says data_length=8, this helper does not program an eight-byte RX length filter or preset RX DLC. Hardware supplies DLC on receipt. The global mask setup (HW:746) intends full ID comparison on the default global-mask path; the custom driver should explicitly configure IRMQ, mask fields and format/RTR comparison semantics.

### 6.3 TX MB0 request and launch

APP:69 makes TX interrupt-driven; APP:70 -> IP:1974 -> `FlexCAN_StartSendData` (IP:1023) -> HW:1447. On the successful standard data-frame path:

| Order | Source/line | Register or software operation | Verification / polling-driver recommendation |
|---|---|---|---|
| 1 | IP:1054–1066; HH:786 | Check software MB idle; `IFLAG1=1`; set RTD software TX_BUSY and polling=false | V-code: software state check, not a universal hardware-idle test. Recommendation: reconcile software/hardware ownership before overwriting a pending MB. |
| 2 | HW:1469–1470 | CS0=`0x08000000` (TX_INACTIVE); ID0=0 | V-code: prepare inactive MB before data. Recommendation: write inactive only when safe under documented TX cancellation rules. |
| 3 | HW:1481–1483 | Write DATA0=`0x01020304`, DATA1=`0x05060700` | Derived for APP:27 payload; RTD swaps byte order. Recommendation: pack explicit shifts, avoid alignment/aliasing assumptions. |
| 4 | HW:1511–1512 | ID0 RMW clears standard-ID field then ORs `0x00500000` | V-code: unshifted software ID 20, IDE/SRR zero in local CS composition. Recommendation: validate 11-bit ID before encoding. |
| 5 | HW:1517–1518,1546,1553 | Compose DLC=8/CODE=12, then `*CS0 |= config`; result CS0=`0x0C080000` | V-code: final activating operation is an RMW OR, not a simple assignment. Derived resulting value includes prior inactive bit, which is already part of CODE 12. Recommendation: construct complete final CS and write it last in the custom driver. |
| 6 | IP:1997–2000; HW:1013,1020–1023 | Set shadow mask bit0, then IMASK1=1 (when no other masks) | V-code: enables TX completion interrupt after launch. Recommendation: omit; keep IMASK1 zero and poll raw IFLAG1 bit0. |

Hardware then performs transmission and internally loops back a matching frame. Successful TX sets IFLAG1 bit0; RX sets bit1. Current source does not provide deterministic ordering between their completion services, since TX ISR execution may interleave foreground RX polling.

### 6.4 RX software registration and completion

| Order | Source/line | Register/bitfield or state | Verification / recommendation |
|---|---|---|---|
| 1 | APP:71; IP:2077–2101,1005–1007 | RX software busy, destination `&rxData`, polling=true | V-code: **no new RX MB register writes** or interrupt enable on this path. RX was already armed before send; a pending frame flag is retained. Recommendation: define this pending-frame behavior explicitly. |
| 2 | APP:72–73; IP:3557–3562; HH:828 onward | Read raw IFLAG1 bit1; require RX software busy | V-code: polling tests raw flag, independent of IMASK1. Recommendation: preserve raw-flag polling, not masked IRQ status. |
| 3 | IP:1301; HW:993 | Read CS1 to lock MB | V-code: helper performs one read, **no CODE.BUSY retry loop**. Recommendation: use documented bounded BUSY handling in a new driver. |
| 4 | IP:1304; HW:1354,1371,1378,1391,1425–1426 | Read CS1 for DLC and status/timestamp; ID1; DATA0/1; swap into rxData | V-code: CS may be read repeatedly, not just once. Derived normal full-frame CS includes CODE=2, DLC=8 and variable timestamp. Recommendation: preserve metadata coherently under lock; detect RX_OVERRUN and validate Classic DLC. |
| 5 | IP:1306; HH:786 | `IFLAG1=2` | V-code: clears only MB1 flag. Recommendation: use direct W1C, never `IFLAG1 |= 2`. |
| 6 | IP:1322; HH:770 | Read TIMER to unlock | V-code: FIFO-off path does not explicitly rewrite CS1 to RX_EMPTY. Recommendation: follow documented receive/unlock behavior rather than blindly rewriting CS. |
| 7 | IP:1334–1360 | Mark RX software idle; callback null; no RX mask changes on polling-to-polling path | V-code: application exits after one receipt; it does not register another software receive. Recommendation: provide repeatable foreground receive service and expose overrun/loss. |

IP:1308–1316 contains an alternate CS rewrite for a FIFO-related erratum condition; it is not taken with this config's FIFO disabled. Recommendation: do not mistake that conditional workaround for the ordinary RX rearm sequence.

### 6.5 Current TX interrupt completion

IRQ:212–215 calls `FlexCAN_IRQHandler(0,0,15)`. IP:2735–2740 finds **masked** pending status; HH:1368–1369 reads `IFLAG1 & IMASK1`, so MB1's polling flag does not ordinarily become a TX-ISR receive event. IP:2767–2769 dispatches TX_BUSY MB0 to IP:1372.

For this nonremote frame, IP:1394 reads MB0 CS timestamp through HW:1314–1326, IP:1395 reads TIMER, and IP:1396 clears IFLAG1 bit0. IP:1399 marks TX idle; IP:1410–1412 resets polling state and clears mask bit0 through HW:1032–1033. Callbacks are null (CFG:188–190). **Recommendation:** implement these completion/ownership effects in foreground polling; merely disabling NVIC while retaining the RTD nonpolling transfer state is incomplete.

## 7. CAN interrupt removal map

The goal is no CAN IRQ dependency, not removal of the CPU's vector table or unrelated platform interrupts.

| Source/line | Register/interface | Verified current use | Required future polling behavior |
|---|---|---|---|
| APP:19,34–36 | IntCtrl include / handler declaration | CAN IRQ integration in application | Remove CAN-specific declarations/dependencies when replacing application CAN path. |
| APP:55–56; INTC:241,205–214 | NVIC.ISER; RAM vector at VTOR | Enables IRQ81 and installs handler at vector slot 97. BASEHDR `S32K144_COMMON.h:245` verifies IRQ number. | Omit enable/install; disable CAN0 NVIC sources and clear stale pending state during ownership transfer. |
| `generate/src/IntCtrl_Ip_Cfg.c:117–121,213–217` | CAN0 IRQ descriptors/routes | MB0–15 enabled with priority5 in generated descriptor; routes otherwise undefined handlers. APP does not call `IntCtrl_Ip_Init`. | Do not claim descriptor priority is applied. Preserve generated files now; prevent future board init from enabling CAN IRQs. |
| APP:69; IP:1997–2000 | Transfer is_polling=false -> IMASK1 | Actually activates TX IRQ usage | Independent TX service must use hardware polling; changing only the metadata still leaves an RTD dependency. |
| `generate/include/FlexCAN_Ip_Cfg.h:192`; IP:1958 | Compile interrupt support; isIntActive=true | RTD CAN IRQ capability active | Custom core must not depend on RTD state/IRQ services. Existing unused files may remain compiled if they cannot own hardware. |
| IRQ:190–223; IP:2710–2780 | CAN0 ORed/error/MB handlers | MB0–15 installed; other handlers present, not necessarily enabled | Leave all CAN0 NVIC sources 78–82 disabled; no need to delete unrelated vectors/startup. |
| HW:752; IP:2000; HW:1023/1033 | IMASK1 | Init clears, send sets bit0, ISR clears bit0 | Keep zero throughout operation; poll raw flags. |
| HW:779; HH:303–307; IP:4367 onward | CTRL1.BOFFMSK,ERRMSK,TWRNMSK,RWRNMSK; MCR.WRNEN | Error-enable APIs exist but are not called; reset/default clears masks | Keep CAN error/warning/wakeup masks disabled; poll ESR1/ECR with documented side effects. WRNEN itself is not an NVIC enable. |
| `Project_Settings/Startup_Code/startup_cm4.s:138–146,311` | VTOR; PRIMASK global enable | Common runtime support, not CAN-specific | Preserve; polling CAN does not require globally disabling all interrupts. |
| `RTD/src/SchM_Can.c:501–519`; IP:1271–1332 | Temporary interrupt masking in critical sections | RTD uses Suspend/Resume; RX erratum branch only suspends for FIFO-related conditions | Eliminate unnecessary RTD exclusive-area dependencies in single-caller core; separately bound MB lock duration if other system ISRs exist. |

For safe ownership transfer: quiesce the old driver, disable peripheral interrupt masks and CAN NVIC lines, clear owned W1C/pending flags, then configure the independent driver. Do not clear a flag before consuming an RX frame unless intentionally discarding it.

## 8. Silicon errata and unresolved hardware dependencies

The project's generated errata flags do not identify the physical chip mask. The external mask report below applies specifically to **0N57U**, not automatically to the attached MCU. Read the package mask marking/device identification and obtain its matching errata before implementation sign-off.

| Finding/source | Registers/conditions | Verified applicability versus assumptions | Recommendation |
|---|---|---|---|
| `generate/include/SoC_Ips.h:219–223`; IP:1271–1332 | Project `ERR_IPV_FLEXCAN_E050246`; RX CS.CODE lock, MCR.RFEN | V-code: macro enabled; RTD workaround checks legacy FIFO and RX_INACTIVE before rewriting CODE. Current RFEN=0 avoids that branch. | Keep FIFO off initially; evaluate the actual device-mask erratum rather than assuming the project flag is a complete errata list. |
| NXP `S32K144_0N57U`, ERR050443, pp.6–7 | Combined FIFO plus dedicated RX MB use; long lock interval; CS.CODE | V-document: relevant mixed-receive corruption condition. Current dedicated-MB-only operation avoids its stated trigger. The public ID differs from the RTD macro; equivalence is not assumed without vendor mapping. | Avoid mixed FIFO/MB reception; keep lock/read/clear/unlock short and deterministic. |
| Same mask report, ERR011543, pp.5–6; CFG:149,157 | FD+BRS, nominal/data SJW | V-document: Classic CAN is excluded from affected configurations. V-code: current FD/BRS false. | No Classic-only workaround needed for this item; reassess if scope expands to FD. |
| `generate/include/SoC_Ips.h:226–229`; CLK:227–228 | SCG ERR010777; RCCR/HCCR.SCS during source switching | V-code: errata flag defined. No reference to that flag was found in copied Clock implementation; flag alone proves no workaround execution. Mask report requires double reads to confirm source switching completion. | Apply matching silicon workaround in a new clock layer; audit actual RTD selector behavior separately if retaining it. |
| Mask report ERR011097/ERR011089/ERR010655, pp.8–9; `generate/include/modules.h:215` | LPSPI TCR.TXMSK/CONT/CPHA, WCF, MBF | External dependency if a future SBC SPI driver is added; current project does not use SPI transfers. | Review relevant SPI errata when implementing SBC setup; do not copy wait-on-WCF logic without checking conditions. |
| APP:66–75; HW:730,536,604 | Reset/freeze/readiness and ignored status | V-code: helper timeouts exist but dummy timing is used; application ignores failures. No captured hardware state. | Return structured failures, use real deadlines and readback at init/start boundaries. |
| `description.txt:12`; `.mex:5–6`; CFG:138 | Board/silicon identity and external physical path | Project names MCU/package/board reference; no physical board revision, silicon mask, SBC state, power/termination or bus capture. | Record actual identifiers and bench wiring; verify physical CAN with loopback disabled. |
| BASEHDR `S32K144_FLEXCAN.h:99–123`; `.cproject` external Base include paths | Vendor register definitions/access environment | Device definitions are installed outside project; their availability and selected device macros are build dependencies. | Reuse device declarations explicitly, or supply independently maintained equivalent definitions if full SDK independence is required. Preserve existing configuration during this analysis. |

External errata reference: [NXP S32K144 mask 0N57U errata, Rev. 20/APR/2020](https://www.nxp.com/docs/en/errata/S32K144_0N57U.pdf). This is a focused review, not a claim that one mask report covers all S32K144 silicon. Other core/platform errata remain relevant to the overall system even when CAN interrupt service is removed.

## 9. Recommended implementation entry conditions and bench evidence

These follow from the findings above; no implementation has been made:

1. Identify board/SBC/silicon mask; confirm pin and power wiring. Resolve the missing board setup from section 5.
2. Retain a known clock setup initially, but establish the actual **SYS_CLK** and selected PE source. Confirm PCC[36].CGC and source validity before accessing CAN.
3. In acknowledged Disable, choose CLKSRC; enable and confirm Freeze, reset with a real deadline, initialize RAM/masks and Classic timing, and read back protected settings.
4. Keep CAN interrupt masks/NVIC sources and DMA disabled. Configure RX MB1 before start, then use raw IFLAG1 polling for RX and TX completion.
5. Capture MCR/CTRL1/PCC/SCG immediately after init/start, not after APP shutdown. Expected CTRL1 is `0x05613007` in the current loopback setup; a physical driver should clear LPB and choose its intended self-reception policy.
6. Verify the exact frame `ID=0x14, DLC=8, data=01 02 03 04 05 06 07 00`, then test nonloopback traffic with an acknowledging peer and analyzer. Test timeout, bus-off, pending TX ownership, unrelated flags, RX overrun, and service latency.

**Verification performed:** source/configuration tracing, installed reference-manual review and focused NXP documentation checks. No build, regeneration, flash, live register read, oscilloscope capture, or physical CAN test was performed. Only this report was added; the previous report and all source/project files were preserved.
