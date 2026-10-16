/* Includes the real application to exercise its static test/result logic.
 * RTD is deliberately not linked: this checks application contracts only.
 */
#define main CanTest_TargetMain
#define CAN_TEST_POLL_BUDGET 4UL
#include "../../src/main.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <string.h>

const unsigned Clock_Ip_aClockConfig[1]={0};
static unsigned init_calls, board_calls, send_calls, rounds;
static Can_DriverState state;
static Can_Diagnostics cached;
static unsigned tx_round, rx_round;
static int mismatch, bus_off, tx_fail, saturate_counts;

void Clock_Ip_Init(const unsigned *config) { (void)config; }
BoardCan_Status BoardCan_InitPins(void) { ++board_calls; return BOARD_CAN_OK; }
BoardCan_Status BoardCan_GetDiagnostics(BoardCan_Diagnostics *out)
{ memset(out,0,sizeof *out); return BOARD_CAN_OK; }
Can_Status CanDriver_Init(const Can_Config *cfg)
{
    assert(cfg->mode==CAN_TEST_MODE);
    assert(cfg->rx_id==(CAN_TEST_PROFILE==CAN_TEST_PROFILE_LOOPBACK ? 0x14U : 0x321U));
    ++init_calls; state=CAN_DRIVER_READY; return CAN_STATUS_OK;
}
Can_Status CanDriver_Start(void) { state=CAN_DRIVER_RUNNING; return CAN_STATUS_OK; }
Can_DriverState CanDriver_GetState(void) { return state; }
Can_Status CanDriver_Transmit(const Can_Frame *frame)
{
#if CAN_TEST_PROFILE == CAN_TEST_PROFILE_NORMAL_TWO_NODE
    const uint8_t payload[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    assert(frame->id==0x123);
#else
    const uint8_t payload[8]={1,2,3,4,5,6,7,0};
    assert(frame->id==0x14);
#endif
    assert(frame->dlc==8 && memcmp(frame->data,payload,8)==0);
    ++send_calls; cached.tx_pending=1; return CAN_STATUS_OK;
}
Can_Status CanDriver_PollDiagnostics(void)
{
    ++rounds; cached.valid=1; ++cached.sample_count;
    cached.bus_off_seen=(uint8_t)bus_off;
    if (saturate_counts)
    {
        g_can_test.tx_poll_calls=UINT32_MAX;
        g_can_test.rx_poll_calls=UINT32_MAX;
    }
#if CAN_TEST_PROFILE == CAN_TEST_PROFILE_NORMAL_TWO_NODE
    /* A manual response may arrive well past the loopback-only budget. */
    if (rounds>CAN_TEST_POLL_BUDGET)
    {
        assert(g_can_test.status==CAN_TEST_RUNNING);
        assert(g_can_test.first_failure==CAN_TEST_FAILURE_NONE);
        assert(!g_can_test.tx_budget_exhausted && !g_can_test.rx_budget_exhausted);
    }
#endif
    return CAN_STATUS_OK;
}
Can_Status CanDriver_GetDiagnostics(Can_Diagnostics *out)
{ *out=cached; return CAN_STATUS_OK; }
Can_Status CanDriver_PollTx(void)
{
    if (tx_fail) { state=CAN_DRIVER_FAULT; return CAN_STATUS_HW_ERROR; }
    if (rounds<tx_round) return CAN_STATUS_BUSY;
    cached.tx_pending=0; return CAN_STATUS_OK;
}
Can_Status CanDriver_PollRx(Can_Frame *out)
{
    if (rounds<rx_round) return CAN_STATUS_NO_DATA;
#if CAN_TEST_PROFILE == CAN_TEST_PROFILE_NORMAL_TWO_NODE
    const Can_Frame reply={0x321U,8U,{0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x07,0x18}};
#else
    const Can_Frame reply={0x14U,8U,{1,2,3,4,5,6,7,0}};
#endif
    *out=reply;
    if (mismatch) out->data[0]^=1;
    return CAN_STATUS_OK;
}

static void reset_case(void)
{
    init_calls=0; board_calls=0; send_calls=0; rounds=0;
    state=CAN_DRIVER_UNINITIALIZED; memset(&cached,0,sizeof cached);
    mismatch=0; bus_off=0; tx_fail=0; saturate_counts=0; tx_round=1; rx_round=1;
    CanTest_Reset();
}

int main(void)
{
    reset_case();
    CanTest_Run();
#if (CAN_TEST_PROFILE == CAN_TEST_PROFILE_NORMAL_TWO_NODE) && !CAN_TEST_HARDWARE_VERIFIED
    assert(g_can_test.status==CAN_TEST_FAIL);
    assert(g_can_test.first_failure==CAN_TEST_FAILURE_HARDWARE_NOT_VERIFIED);
    assert(!init_calls && !board_calls && !send_calls && !rounds);
    puts("PASS: normal profile is blocked before pins and CAN init");
#else
    assert(g_can_test.status==CAN_TEST_PASS && send_calls==1 && init_calls==1);
    assert(g_can_test.tx_completed && g_can_test.rx_completed && !g_can_test.mismatch_mask);
    assert(g_can_test.profile==CAN_TEST_PROFILE && g_can_test.mode==CAN_TEST_MODE);
    assert(board_calls==(CAN_TEST_PROFILE==CAN_TEST_PROFILE_NORMAL_TWO_NODE ? 1U : 0U));
    assert(g_can_test.expected.data[0]==(CAN_TEST_PROFILE==CAN_TEST_PROFILE_LOOPBACK ? 1U : 0xA1U));
    reset_case(); tx_round=3; rx_round=1; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_PASS && rounds==3 && send_calls==1);
    reset_case(); tx_round=99; CanTest_Run();
#if CAN_TEST_PROFILE == CAN_TEST_PROFILE_LOOPBACK
    assert(g_can_test.status==CAN_TEST_TIMEOUT && g_can_test.rx_completed && !g_can_test.tx_completed);
    assert(g_can_test.tx_budget_exhausted && !g_can_test.rx_budget_exhausted);
    assert(cached.tx_pending && g_can_test.tx_completion_unconfirmed && send_calls==1);
    reset_case(); rx_round=99; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_TIMEOUT && g_can_test.tx_completed && !g_can_test.rx_completed);
    assert(!g_can_test.tx_budget_exhausted && g_can_test.rx_budget_exhausted);
#else
    assert(g_can_test.status==CAN_TEST_PASS && rounds==99 && send_calls==1);
    assert(g_can_test.tx_completed && g_can_test.rx_completed && !cached.tx_pending);
    reset_case(); rx_round=99; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_PASS && rounds==99 && send_calls==1);
    assert(g_can_test.tx_completed && g_can_test.rx_completed && !g_can_test.mismatch_mask);
    assert(g_can_test.iteration_budget==0 && !g_can_test.rx_budget_exhausted);
#endif
    reset_case(); saturate_counts=1; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_PASS);
    assert(g_can_test.tx_poll_calls==UINT32_MAX && g_can_test.rx_poll_calls==UINT32_MAX);
    reset_case(); mismatch=1; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && g_can_test.first_failure==CAN_TEST_FAILURE_FRAME_MISMATCH);
    assert(g_can_test.received.data[0]!=g_can_test.expected.data[0]);
    reset_case(); tx_round=99; rx_round=99; bus_off=1; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && g_can_test.first_failure==CAN_TEST_FAILURE_BUS_OFF);
    assert(g_can_test.failure_diagnostics.bus_off_seen && cached.tx_pending && send_calls==1);
    reset_case(); tx_fail=1; CanTest_Run();
    assert(g_can_test.first_failure==CAN_TEST_FAILURE_TX_POLL);
    assert(g_can_test.tx_blocked_by_driver_fault && g_can_test.rx_blocked_by_driver_fault);
    puts("PASS: application profiles, completion ordering, budgets, mismatch, faults, diagnostics");
#endif
    return 0;
}
