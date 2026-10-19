#ifndef TEST_CLOCK_IP_H
#define TEST_CLOCK_IP_H
#include <stdint.h>
#define CORE_CLK 0U
extern const unsigned Clock_Ip_aClockConfig[1];
void Clock_Ip_Init(const unsigned *config);
uint32_t Clock_Ip_GetClockFrequency(unsigned clock);
#endif
