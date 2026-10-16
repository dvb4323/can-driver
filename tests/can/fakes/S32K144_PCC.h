#ifndef TEST_PCC_H
#define TEST_PCC_H
#include <stdint.h>
typedef struct { volatile uint32_t PCCn[128]; } PCC_Type;
extern PCC_Type test_pcc;
#define IP_PCC (&test_pcc)
#define PCC_PORTE_INDEX 77
#define PCC_PCCn_CGC_MASK 0x40000000U
#define PCC_PCCn_PR_MASK 0x80000000U
#endif
