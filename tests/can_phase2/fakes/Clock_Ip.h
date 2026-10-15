#ifndef PHASE2_TEST_CLOCK_H
#define PHASE2_TEST_CLOCK_H
/* Host doubles only; real clock initialization is checked by target build. */
static const unsigned Clock_Ip_aClockConfig[1] = {0U};
static inline void Clock_Ip_Init(const unsigned *config) { (void)config; }
#endif
