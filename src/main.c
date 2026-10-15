/*!
** Copyright 2019 NXP
** @file main.c
** @brief
**         Main module.
**         This module contains user's application code.
*/
/*!
**  @addtogroup main_module main module documentation
**  @{
*/
/* MODULE main */


/* Including necessary configuration files. */
#include "Mcal.h"
#include "Clock_Ip.h"
#include "can_driver.h"
#include "can_cfg.h"

/* A count of foreground polling rounds, never a millisecond deadline. */
#ifndef CAN_TEST_POLL_BUDGET
#define CAN_TEST_POLL_BUDGET 1000000UL
#endif

typedef enum
{
    CAN_TEST_NOT_STARTED = 0,
    CAN_TEST_RUNNING,
    CAN_TEST_PASS,
    CAN_TEST_FAIL,
    CAN_TEST_TIMEOUT
} CanTest_Status;

typedef enum
{
    CAN_TEST_STAGE_RESET = 0,
    CAN_TEST_STAGE_CLOCK,
    CAN_TEST_STAGE_INIT,
    CAN_TEST_STAGE_START,
    CAN_TEST_STAGE_TRANSMIT,
    CAN_TEST_STAGE_POLL
} CanTest_Stage;

typedef enum
{
    CAN_TEST_FAILURE_NONE = 0,
    CAN_TEST_FAILURE_INIT,
    CAN_TEST_FAILURE_START,
    CAN_TEST_FAILURE_TRANSMIT,
    CAN_TEST_FAILURE_TX_POLL,
    CAN_TEST_FAILURE_RX_POLL,
    CAN_TEST_FAILURE_FRAME_MISMATCH,
    CAN_TEST_FAILURE_RX_OVERRUN,
    CAN_TEST_FAILURE_DRIVER_STATE,
    CAN_TEST_FAILURE_BUDGET
} CanTest_Failure;

typedef struct
{
    uint32_t magic; /* 0x43414E32 = "CAN2" */
    CanTest_Status status;
    CanTest_Stage stage;
    CanTest_Failure first_failure;
    Can_Status first_failure_api_status;
    Can_DriverState driver_state;
    Can_Status init_status;
    Can_Status start_status;
    Can_Status transmit_status;
    Can_Status tx_poll_status;
    Can_Status rx_poll_status;
    uint32_t init_calls;
    uint32_t start_calls;
    uint32_t transmit_calls;
    uint32_t tx_poll_calls;
    uint32_t rx_poll_calls;
    uint32_t iteration_budget;
    uint32_t iterations;
    uint32_t tx_accepted;
    uint32_t tx_completed;
    uint32_t rx_completed; /* A frame was delivered, even if it mismatches. */
    uint32_t tx_error;
    uint32_t rx_error;
    uint32_t tx_budget_exhausted;
    uint32_t rx_budget_exhausted;
    uint32_t tx_blocked_by_driver_fault;
    uint32_t rx_blocked_by_driver_fault;
    uint32_t tx_completion_unconfirmed; /* Conservative, not hardware BUSY. */
    uint32_t id_match;
    uint32_t dlc_match;
    uint32_t payload_match;
    uint32_t mismatch_mask; /* bit0 ID, bit1 DLC, bits2..9 data[0..7]. */
    Can_Frame expected;
    Can_Frame received;
} CanTest_Results;

/* Observe this RAM object in S32DS Expressions. Terminal status is written
 * after all other results; the hold loop never resets these diagnostics.
 */
volatile CanTest_Results g_can_test;
volatile int exit_code = 0;

static void CanTest_Reset(void)
{
    const CanTest_Results initial = {
        .magic = 0x43414E32UL,
        .status = CAN_TEST_NOT_STARTED,
        .first_failure_api_status = CAN_STATUS_NO_TRANSFER,
        .init_status = CAN_STATUS_NO_TRANSFER,
        .start_status = CAN_STATUS_NO_TRANSFER,
        .transmit_status = CAN_STATUS_NO_TRANSFER,
        .tx_poll_status = CAN_STATUS_NO_TRANSFER,
        .rx_poll_status = CAN_STATUS_NO_TRANSFER,
        .iteration_budget = CAN_TEST_POLL_BUDGET,
        .expected = {0x14U, 8U, {1U, 2U, 3U, 4U, 5U, 6U, 7U, 0U}}
    };
    g_can_test = initial;
    exit_code = 0;
}

static void CanTest_RecordFailure(CanTest_Failure reason, Can_Status status)
{
    if (g_can_test.first_failure == CAN_TEST_FAILURE_NONE)
    {
        g_can_test.first_failure = reason;
        g_can_test.first_failure_api_status = status;
    }
}

static void CanTest_Compare(const Can_Frame *received)
{
    uint32_t index;
    uint32_t mask = 0U;
    g_can_test.received = *received;
    g_can_test.rx_completed = 1U;
    if (received->id != g_can_test.expected.id)
    {
        mask |= 1UL;
    }
    if (received->dlc != g_can_test.expected.dlc)
    {
        mask |= 2UL;
    }
    for (index = 0U; index < CAN_CFG_MAX_DLC; ++index)
    {
        if (received->data[index] != g_can_test.expected.data[index])
        {
            mask |= (1UL << (index + 2U));
        }
    }
    g_can_test.mismatch_mask = mask;
    g_can_test.id_match = ((mask & 1UL) == 0U) ? 1U : 0U;
    g_can_test.dlc_match = ((mask & 2UL) == 0U) ? 1U : 0U;
    g_can_test.payload_match = ((mask & 0x3FCUL) == 0U) ? 1U : 0U;
    if (mask != 0U)
    {
        g_can_test.rx_error = 1U;
        CanTest_RecordFailure(CAN_TEST_FAILURE_FRAME_MISMATCH, CAN_STATUS_OK);
    }
}

static void CanTest_Run(void)
{
    const Can_Config config = {0x14U};
    const Can_Frame transmit = {0x14U, 8U, {1U, 2U, 3U, 4U, 5U, 6U, 7U, 0U}};
    Can_Frame received;
    Can_Status status;
    uint32_t iteration;

    g_can_test.status = CAN_TEST_RUNNING;
    g_can_test.stage = CAN_TEST_STAGE_INIT;
    g_can_test.init_calls = 1U;
    status = CanDriver_Init(&config);
    g_can_test.init_status = status;
    if (status != CAN_STATUS_OK)
    {
        CanTest_RecordFailure(CAN_TEST_FAILURE_INIT, status);
    }
    else
    {
        g_can_test.stage = CAN_TEST_STAGE_START;
        g_can_test.start_calls = 1U;
        status = CanDriver_Start();
        g_can_test.start_status = status;
        if (status != CAN_STATUS_OK)
        {
            CanTest_RecordFailure(CAN_TEST_FAILURE_START, status);
        }
        else
        {
            g_can_test.stage = CAN_TEST_STAGE_TRANSMIT;
            g_can_test.transmit_calls = 1U;
            g_can_test.tx_completion_unconfirmed = 1U;
            status = CanDriver_Transmit(&transmit); /* Exactly one attempt. */
            g_can_test.transmit_status = status;
            if (status != CAN_STATUS_OK)
            {
                g_can_test.tx_error = 1U;
                CanTest_RecordFailure(CAN_TEST_FAILURE_TRANSMIT, status);
            }
            else
            {
                g_can_test.tx_accepted = 1U;
            }
        }
    }

    for (iteration = 0U;
         (iteration < CAN_TEST_POLL_BUDGET) &&
         (g_can_test.first_failure == CAN_TEST_FAILURE_NONE) &&
         ((g_can_test.tx_completed == 0U) || (g_can_test.rx_completed == 0U));
         ++iteration)
    {
        g_can_test.stage = CAN_TEST_STAGE_POLL;
        g_can_test.iterations = iteration + 1U;
        if (g_can_test.tx_completed == 0U)
        {
            ++g_can_test.tx_poll_calls;
            status = CanDriver_PollTx();
            g_can_test.tx_poll_status = status;
            if (status == CAN_STATUS_OK)
            {
                g_can_test.tx_completed = 1U;
                g_can_test.tx_completion_unconfirmed = 0U;
            }
            else if (status != CAN_STATUS_BUSY)
            {
                g_can_test.tx_error = 1U;
                CanTest_RecordFailure(CAN_TEST_FAILURE_TX_POLL, status);
            }
        }

        /* Service RX independently, including the TX-error round if the
         * wrapper still permits it. Never call an RX API on FAULT state.
         */
        g_can_test.driver_state = CanDriver_GetState();
        if ((g_can_test.rx_completed == 0U) &&
            (g_can_test.driver_state == CAN_DRIVER_RUNNING))
        {
            ++g_can_test.rx_poll_calls;
            status = CanDriver_PollRx(&received);
            g_can_test.rx_poll_status = status;
            if ((status == CAN_STATUS_OK) || (status == CAN_STATUS_RX_OVERRUN))
            {
                CanTest_Compare(&received);
                if (status == CAN_STATUS_RX_OVERRUN)
                {
                    g_can_test.rx_error = 1U;
                    CanTest_RecordFailure(CAN_TEST_FAILURE_RX_OVERRUN, status);
                }
            }
            else if (status != CAN_STATUS_NO_DATA)
            {
                g_can_test.rx_error = 1U;
                CanTest_RecordFailure(CAN_TEST_FAILURE_RX_POLL, status);
            }
        }
        g_can_test.driver_state = CanDriver_GetState();
        if (g_can_test.driver_state != CAN_DRIVER_RUNNING)
        {
            g_can_test.tx_blocked_by_driver_fault = (g_can_test.tx_completed == 0U) ? 1U : 0U;
            g_can_test.rx_blocked_by_driver_fault = (g_can_test.rx_completed == 0U) ? 1U : 0U;
            CanTest_RecordFailure(CAN_TEST_FAILURE_DRIVER_STATE, CAN_STATUS_BAD_STATE);
        }
    }

    g_can_test.driver_state = CanDriver_GetState();
    if (g_can_test.first_failure != CAN_TEST_FAILURE_NONE)
    {
        exit_code = 1;
        g_can_test.status = (g_can_test.first_failure_api_status == CAN_STATUS_TIMEOUT) ?
                            CAN_TEST_TIMEOUT : CAN_TEST_FAIL;
    }
    else if ((g_can_test.tx_completed != 0U) && (g_can_test.rx_completed != 0U))
    {
        exit_code = 0;
        g_can_test.status = CAN_TEST_PASS;
    }
    else
    {
        g_can_test.tx_budget_exhausted = (g_can_test.tx_completed == 0U) ? 1U : 0U;
        g_can_test.rx_budget_exhausted = (g_can_test.rx_completed == 0U) ? 1U : 0U;
        CanTest_RecordFailure(CAN_TEST_FAILURE_BUDGET, CAN_STATUS_NO_TRANSFER);
        exit_code = 2;
        g_can_test.status = CAN_TEST_TIMEOUT;
    }
}

/* Set a function breakpoint here. No cleanup, retry, or further CAN access:
 * keep the first failure and any unconfirmed TX ownership intact.
 */
__attribute__((noinline, noreturn)) void CanTest_HoldResults(void)
{
    for (;;)
    {
        /* Intentionally retain diagnostics for the debugger. */
    }
}

int main(void)
{
    CanTest_Reset();
    g_can_test.stage = CAN_TEST_STAGE_CLOCK;
    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
#if defined (FEATURE_CLOCK_IP_HAS_SPLL_CLK)
    while ( CLOCK_IP_PLL_LOCKED != Clock_Ip_GetPllStatus() )
    {
        /* Busy wait until the System PLL is locked */
    }

    Clock_Ip_DistributePll();
#endif
    CanTest_Run();
    CanTest_HoldResults();
}

/* END main */
/*!
** @}
*/
