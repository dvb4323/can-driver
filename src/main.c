/* Select explicitly in S32DS. Default preserves the user-verified Phase 3. */
#ifndef APP_PHASE4A
#define APP_PHASE4A 0
#endif
#if APP_PHASE4A != 0 && APP_PHASE4A != 1
#error "APP_PHASE4A must be 0 or 1"
#endif

#if !APP_PHASE4A
/* Exact saved fixture; .inc prevents a second compiled main/CAN owner. */
#include "../regression/phase3_smoke_main.inc"
#else
#include "Mcal.h"
#include "Clock_Ip.h"
#include "board_can.h"
#include "can_driver.h"
#include "can_protocol.h"
#include "motor_app.h"
#include "motor_mock.h"
#include "app_time.h"

/* 1 clocks, 2 timer dependency, 3 pins, 4 CAN Init, 5 CAN Start, 6 running,
 * 90 time unavailable, 91 pins, 92 Init, 93 Start, 94 time read failure.
 */
volatile uint32_t g_phase4a_boot_stage;
volatile Can_Status g_phase4a_can_status;
volatile BoardCan_Status g_phase4a_board_status;
static MotorMock s_motor_mock;

__attribute__((noinline, noreturn)) static void Phase4A_BootHold(void)
{
    /* Mock is inhibited; no transfer API on failed/uninitialized CAN. */
    for (;;) { }
}
int main(void)
{
    const Can_Config config={CP_COMMAND_ID,CAN_MODE_NORMAL};
    Motor_If motor;
    uint32_t now=0U;
    MotorMock_Init(&s_motor_mock,true); /* Simulation only; no motor pins. */
    motor=MotorMock_Interface(&s_motor_mock);
    (void)MotorApp_Init(&motor,0U);
    g_phase4a_boot_stage=1U;
    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
#if defined (FEATURE_CLOCK_IP_HAS_SPLL_CLK)
    while (CLOCK_IP_PLL_LOCKED != Clock_Ip_GetPllStatus()) { }
    Clock_Ip_DistributePll();
#endif
    g_phase4a_boot_stage=2U;
    if (!AppTime_InitTarget()) {
        MotorApp_TimeFault(); g_phase4a_boot_stage=90U; Phase4A_BootHold();
    }
    g_phase4a_boot_stage=3U;
    g_phase4a_board_status=BoardCan_InitPins();
    if (g_phase4a_board_status!=BOARD_CAN_OK) {
        g_phase4a_boot_stage=91U; Phase4A_BootHold();
    }
    g_phase4a_boot_stage=4U;
    g_phase4a_can_status=CanDriver_Init(&config);
    if (g_phase4a_can_status!=CAN_STATUS_OK) {
        g_phase4a_boot_stage=92U; Phase4A_BootHold();
    }
    g_phase4a_boot_stage=5U;
    g_phase4a_can_status=CanDriver_Start();
    if (g_phase4a_can_status!=CAN_STATUS_OK) {
        g_phase4a_boot_stage=93U; Phase4A_BootHold();
    }
    if (!AppTime_Read(&now)) {
        MotorApp_TimeFault(); g_phase4a_boot_stage=94U; Phase4A_BootHold();
    }
    (void)MotorApp_Init(&motor,now);
    g_phase4a_boot_stage=6U;
    for (;;) {
        uint32_t sample;
        if (AppTime_Read(&sample)) now=sample;
        else { MotorApp_TimeFault(); g_phase4a_boot_stage=94U; }
        MotorApp_Process(now); /* Exactly one bounded step; never waits for TX. */
    }
}
#endif
