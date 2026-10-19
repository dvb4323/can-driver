#ifndef APP_TIME_H
#define APP_TIME_H
#include <stdbool.h>
#include <stdint.h>

typedef bool (*AppTime_Reader)(void *context, uint32_t *now_ms);
/* Injected source has static/caller-owned lifetime and supplies modulo-2^32
 * monotonic milliseconds. False means uncertain/invalid time, not zero time.
 */
bool AppTime_Bind(AppTime_Reader reader, void *context);
bool AppTime_Read(uint32_t *now_ms);
/* Default false: target timer is an explicit reviewed dependency.
 * APP_TIME_SYSTICK_REVIEWED=1 opts into a polling-only MOCK bench source.
 * It requires exclusive SysTick ownership and <one-wrap service gaps.
 */
bool AppTime_InitTarget(void);
#endif
