#ifndef TEST_SYSTICK_H
#define TEST_SYSTICK_H
#include <stdint.h>
typedef struct { volatile uint32_t CSRr, RVR, CVR, CALIB; } S32_SysTick_Type;
extern S32_SysTick_Type test_systick;
#define S32_SysTick (&test_systick)
#define S32_SysTick_RVR_RELOAD_MASK 0xFFFFFFU
#define S32_SysTick_CSR_ENABLE_MASK 0x1U
#define S32_SysTick_CSR_TICKINT_MASK 0x2U
#define S32_SysTick_CSR_CLKSOURCE_MASK 0x4U
#endif
