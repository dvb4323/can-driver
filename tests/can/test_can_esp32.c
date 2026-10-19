/* Host-only application simulation; no physical peripheral/RTD execution. */
#define CAN_TEST_PROFILE 2
#define CAN_TEST_HARDWARE_VERIFIED 1
#define main CanApp_TargetMain
#include "../../src/main.c"
#undef main
#include <assert.h>
#include <string.h>
#include <stdio.h>

const unsigned Clock_Ip_aClockConfig[1] = {0};
PCC_Type test_pcc;
PORT_Type test_porte, test_portd;
GPIO_Type test_ptd;
S32_SysTick_Type test_systick;
static unsigned rounds, stop_round, accepted_at, sends, received_count;
static int pending, led_seen;
static Can_DriverState driver_state;

void Clock_Ip_Init(const unsigned *config) { (void)config; }
uint32_t Clock_Ip_GetClockFrequency(unsigned clock)
{ assert(clock==CORE_CLK); return 48000000U; }
BoardCan_Status BoardCan_InitPins(void) { return BOARD_CAN_OK; }
BoardCan_Status BoardCan_GetDiagnostics(BoardCan_Diagnostics *out)
{ memset(out,0,sizeof *out); return BOARD_CAN_OK; }
Can_Status CanDriver_Init(const Can_Config *config)
{
    assert(config->mode==CAN_MODE_NORMAL && config->rx_id==0x321);
    driver_state=CAN_DRIVER_READY; return CAN_STATUS_OK;
}
Can_Status CanDriver_Start(void)
{ driver_state=CAN_DRIVER_RUNNING; return CAN_STATUS_OK; }
Can_DriverState CanDriver_GetState(void) { return driver_state; }
Can_Status CanDriver_GetDiagnostics(Can_Diagnostics *out)
{
    memset(out,0,sizeof *out);
    out->valid=1; out->driver_state=driver_state; out->tx_pending=(uint8_t)pending;
    return CAN_STATUS_OK;
}
Can_Status CanDriver_PollDiagnostics(void)
{
    ++rounds;
    test_systick.CVR=(test_systick.CVR-48000U)&S32_SysTick_RVR_RELOAD_MASK;
    if (g_can_app.led_on) led_seen=1;
    return (rounds>=stop_round) ? CAN_STATUS_HW_ERROR : CAN_STATUS_OK;
}
Can_Status CanDriver_Transmit(const Can_Frame *frame)
{
    const uint8_t payload[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    assert(!pending && frame->id==0x123 && frame->dlc==8);
    assert(memcmp(frame->data,payload,8)==0);
    if (sends) assert((rounds-accepted_at)>=100); /* Never exceeds 10 Hz in this simulation. */
    pending=1; accepted_at=rounds; ++sends; return CAN_STATUS_OK;
}
Can_Status CanDriver_PollTx(void)
{
    assert(pending);
    if ((rounds-accepted_at)<(sends==1 ? 250U : 3U)) return CAN_STATUS_BUSY;
    pending=0; return CAN_STATUS_OK;
}
Can_Status CanDriver_PollRx(Can_Frame *out)
{
    if ((rounds%50U)!=0U) return CAN_STATUS_NO_DATA;
    memset(out,0,sizeof *out);
    out->id=0x321; out->dlc=2; out->data[0]=0xDE; out->data[1]=0xAD;
    ++received_count; return CAN_STATUS_OK;
}
static void reset(unsigned end)
{
    memset(&test_pcc,0,sizeof test_pcc);
    memset(&test_portd,0,sizeof test_portd);
    memset(&test_ptd,0,sizeof test_ptd);
    memset(&test_systick,0,sizeof test_systick);
    test_pcc.PCCn[PCC_PORTD_INDEX]=PCC_PCCn_PR_MASK|PCC_PCCn_CGC_MASK;
    rounds=0; stop_round=end; accepted_at=0; sends=0; received_count=0;
    pending=0; led_seen=0; driver_state=CAN_DRIVER_UNINITIALIZED;
}
int main(void)
{
    reset(1001);
    CanApp_Run();
    assert(sends>5 && g_can_app.tx_count>4);
    assert(g_can_app.rx_count==received_count && received_count>10);
    assert(g_can_app.received.dlc==2 && g_can_app.received.data[0]==0xDE);
    assert(led_seen && g_can_app.led_blinks_started>=2 && !g_can_app.led_on);
    assert((test_systick.CSRr&S32_SysTick_CSR_TICKINT_MASK)==0);
    assert(g_can_app.failure_stage==6 && g_can_app.first_failure_status==CAN_STATUS_HW_ERROR);
    reset(20);
    CanApp_Run();
    assert(sends==1 && pending && g_can_app.tx_pending);
    assert(g_can_app.failure_diagnostics.tx_pending && g_can_app.tx_count==0);
    reset(20);
    test_systick.CSRr=S32_SysTick_CSR_ENABLE_MASK;
    CanApp_Run();
    assert(!sends && g_can_app.failure_stage==3 && g_can_app.first_failure_status==CAN_STATUS_BAD_STATE);
    puts("PASS: ESP32 repeated TX ownership/pacing, arbitrary RX blink, no IRQ, preserved fault, timer ownership");
    return 0;
}
