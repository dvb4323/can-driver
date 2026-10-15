/* Exercise the actual application test logic with wrapper doubles.
 * This neither runs RTD nor simulates the CAN peripheral.
 */
#include <assert.h>
#include <stdio.h>
#define CAN_TEST_POLL_BUDGET 4UL
#define main Phase2_EmbeddedMain
#include "../../src/main.c"
#undef main

static Can_DriverState driver_state;
static Can_Status init_result, start_result, request_result, tx_result, rx_result;
static unsigned tx_delay, rx_delay, tx_calls, rx_calls, sends;
static unsigned corrupt; /* 1 ID, 2 DLC, 3 last payload byte */
static unsigned fault_on_tx, fault_on_rx;

Can_Status CanDriver_Init(const Can_Config *cfg)
{
    assert(cfg->rx_id == 0x14U);
    driver_state = (init_result == CAN_STATUS_OK) ? CAN_DRIVER_READY : CAN_DRIVER_FAULT;
    return init_result;
}
Can_Status CanDriver_Start(void)
{
    driver_state = (start_result == CAN_STATUS_OK) ? CAN_DRIVER_RUNNING : CAN_DRIVER_FAULT;
    return start_result;
}
Can_Status CanDriver_Transmit(const Can_Frame *frame)
{
    assert(driver_state == CAN_DRIVER_RUNNING);
    assert(frame->id == 0x14U && frame->dlc == 8U);
    for (unsigned i=0U;i<8U;++i) assert(frame->data[i] == ((i<7U)? i+1U : 0U));
    assert(++sends == 1U);
    if (request_result != CAN_STATUS_OK) driver_state = CAN_DRIVER_FAULT;
    return request_result;
}
Can_Status CanDriver_PollTx(void)
{
    assert(driver_state == CAN_DRIVER_RUNNING);
    ++tx_calls;
    if (fault_on_tx) driver_state = CAN_DRIVER_FAULT;
    return (tx_calls <= tx_delay) ? CAN_STATUS_BUSY : tx_result;
}
Can_Status CanDriver_PollRx(Can_Frame *out)
{
    assert(driver_state == CAN_DRIVER_RUNNING);
    ++rx_calls;
    if (fault_on_rx) driver_state = CAN_DRIVER_FAULT;
    if (rx_calls <= rx_delay) return CAN_STATUS_NO_DATA;
    if (rx_result == CAN_STATUS_OK || rx_result == CAN_STATUS_RX_OVERRUN)
    {
        const Can_Frame received = {0x14U,8U,{1U,2U,3U,4U,5U,6U,7U,0U}};
        *out = received;
        if (corrupt == 1U) out->id ^= 1U;
        if (corrupt == 2U) out->dlc = 7U;
        if (corrupt == 3U) out->data[7] = 1U;
    }
    return rx_result;
}
Can_DriverState CanDriver_GetState(void) { return driver_state; }
/* These definitions must never be reached on ANY pass/fail/timeout path. */
Can_Status CanDriver_Stop(void) { assert(!"Unsafe test cleanup"); return CAN_STATUS_HW_ERROR; }
Can_Status CanDriver_Deinit(void) { assert(!"Unsafe test cleanup"); return CAN_STATUS_HW_ERROR; }

static void reset(void)
{
    CanTest_Reset();
    driver_state = CAN_DRIVER_UNINITIALIZED;
    init_result=start_result=request_result=tx_result=rx_result=CAN_STATUS_OK;
    tx_delay=rx_delay=tx_calls=rx_calls=sends=corrupt=fault_on_tx=fault_on_rx=0U;
}

int main(void)
{
    reset(); CanTest_Run();
    assert(g_can_test.status == CAN_TEST_PASS && sends == 1U);
    assert(g_can_test.tx_completed && g_can_test.rx_completed);
    assert(g_can_test.id_match && g_can_test.dlc_match && g_can_test.payload_match);
    assert(!g_can_test.tx_completion_unconfirmed && !g_can_test.mismatch_mask);

    reset(); tx_delay=2U; CanTest_Run(); /* RX can finish first. */
    assert(g_can_test.status == CAN_TEST_PASS && tx_calls==3U && rx_calls==1U);
    reset(); rx_delay=2U; CanTest_Run(); /* TX can finish first. */
    assert(g_can_test.status == CAN_TEST_PASS && tx_calls==1U && rx_calls==3U);

    reset(); tx_delay=rx_delay=100U; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_TIMEOUT && g_can_test.iterations==4U);
    assert(g_can_test.tx_budget_exhausted && g_can_test.rx_budget_exhausted);
    assert(g_can_test.tx_completion_unconfirmed && sends==1U && exit_code==2);
    reset(); rx_delay=100U; CanTest_Run();
    assert(g_can_test.tx_completed && !g_can_test.rx_completed);
    assert(!g_can_test.tx_budget_exhausted && g_can_test.rx_budget_exhausted);

    reset(); tx_delay=100U; corrupt=3U; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && g_can_test.rx_error);
    assert(!g_can_test.tx_completed && g_can_test.tx_completion_unconfirmed);
    assert(g_can_test.mismatch_mask==(1UL<<9U) && g_can_test.rx_completed);
    reset(); corrupt=1U; CanTest_Run(); assert(g_can_test.mismatch_mask==1U);
    reset(); corrupt=2U; CanTest_Run(); assert(g_can_test.mismatch_mask==2U);
    reset(); rx_result=CAN_STATUS_RX_OVERRUN; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && g_can_test.rx_completed);
    assert(g_can_test.first_failure==CAN_TEST_FAILURE_RX_OVERRUN);

    reset(); init_result=CAN_STATUS_TIMEOUT; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_TIMEOUT && !sends && !g_can_test.start_calls);
    reset(); start_result=CAN_STATUS_HW_ERROR; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && !sends);
    reset(); request_result=CAN_STATUS_BUSY; CanTest_Run();
    assert(g_can_test.status==CAN_TEST_FAIL && sends==1U && !tx_calls && !rx_calls);

    reset(); tx_result=CAN_STATUS_HW_ERROR; fault_on_tx=1U; CanTest_Run();
    assert(g_can_test.tx_error && !g_can_test.rx_error && !rx_calls);
    assert(g_can_test.rx_blocked_by_driver_fault && g_can_test.tx_completion_unconfirmed);
    reset(); tx_result=CAN_STATUS_NO_TRANSFER; rx_result=CAN_STATUS_TIMEOUT; CanTest_Run();
    assert(g_can_test.tx_error && g_can_test.rx_error);
    assert(g_can_test.first_failure==CAN_TEST_FAILURE_TX_POLL);
    assert(g_can_test.first_failure_api_status==CAN_STATUS_NO_TRANSFER);
    assert(g_can_test.status==CAN_TEST_FAIL); /* Preserve first failure. */
    reset(); rx_result=CAN_STATUS_BUSY; fault_on_rx=1U; CanTest_Run();
    assert(g_can_test.rx_error && g_can_test.driver_state==CAN_DRIVER_FAULT);
    assert(g_can_test.status==CAN_TEST_FAIL); /* BUSY with FAULT is not NO_DATA. */

    puts("PASS: Phase 2 loopback application control flow and diagnostic snapshots (wrapper doubles only)");
    return 0;
}
