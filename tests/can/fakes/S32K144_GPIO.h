#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdint.h>
typedef struct { volatile uint32_t PDOR, PSOR, PCOR, PTOR, PDIR, PDDR; } GPIO_Type;
extern GPIO_Type test_ptd;
#define IP_PTD (&test_ptd)
#endif
