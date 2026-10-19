#include "app_time.h"
#include <stddef.h>

#ifndef APP_TIME_SYSTICK_REVIEWED
#define APP_TIME_SYSTICK_REVIEWED 0
#endif
#if APP_TIME_SYSTICK_REVIEWED != 0 && APP_TIME_SYSTICK_REVIEWED != 1
#error "APP_TIME_SYSTICK_REVIEWED must be 0 or 1"
#endif
static AppTime_Reader s_reader;
static void *s_context;
bool AppTime_Bind(AppTime_Reader reader, void *context)
{
    s_reader=reader; s_context=context; return reader!=NULL;
}
bool AppTime_Read(uint32_t *now_ms)
{ return now_ms!=NULL && s_reader!=NULL && s_reader(s_context,now_ms); }

#if APP_TIME_SYSTICK_REVIEWED
#include "Clock_Ip.h"
#include "OsIf_Cfg.h"
#include "S32K144_SYSTICK.h"
#if OSIF_USE_SYSTEM_TIMER != STD_OFF
#error "OSIF owns the system timer; do not claim SysTick for Phase 4A"
#endif
typedef struct { uint32_t last, ms, remainder, ticks_per_ms; } TickTime;
static TickTime s_tick;
static bool ReadTick(void *context, uint32_t *out)
{
    TickTime *t=context;
    uint32_t now, delta, ticks;
    uint32_t expected=S32_SysTick_CSR_CLKSOURCE_MASK|S32_SysTick_CSR_ENABLE_MASK;
    if ((S32_SysTick->CSRr & (expected|S32_SysTick_CSR_TICKINT_MASK))!=expected ||
        S32_SysTick->RVR!=S32_SysTick_RVR_RELOAD_MASK) return false;
    now=S32_SysTick->CVR;
    delta=(t->last-now)&S32_SysTick_RVR_RELOAD_MASK; t->last=now;
    ticks=t->remainder+delta;
    t->ms+=ticks/t->ticks_per_ms; t->remainder=ticks%t->ticks_per_ms;
    *out=t->ms;
    return true;
}
#endif
bool AppTime_InitTarget(void)
{
    s_reader=NULL; s_context=NULL;
#if APP_TIME_SYSTICK_REVIEWED
    {
        uint32_t frequency=Clock_Ip_GetClockFrequency(CORE_CLK);
        if (frequency<1000U || (frequency%1000U)!=0U || frequency>100000000U) return false;
        if ((S32_SysTick->CSRr & (S32_SysTick_CSR_ENABLE_MASK|S32_SysTick_CSR_TICKINT_MASK))!=0U)
            return false; /* Never overwrite an enabled timer, including IRQ use. */
        s_tick.ms=0U; s_tick.remainder=0U; s_tick.ticks_per_ms=frequency/1000U;
        S32_SysTick->RVR=S32_SysTick_RVR_RELOAD_MASK;
        S32_SysTick->CVR=0U;
        S32_SysTick->CSRr=S32_SysTick_CSR_CLKSOURCE_MASK|S32_SysTick_CSR_ENABLE_MASK;
        s_tick.last=S32_SysTick->CVR;
        return AppTime_Bind(ReadTick,&s_tick);
    }
#else
    return false;
#endif
}
