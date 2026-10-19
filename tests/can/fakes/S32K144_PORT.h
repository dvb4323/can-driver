#ifndef TEST_PORT_H
#define TEST_PORT_H
#include <stdint.h>
typedef struct { volatile uint32_t PCR[32]; } PORT_Type;
extern PORT_Type test_porte;
extern PORT_Type test_portd;
#define IP_PORTD (&test_portd)
#define IP_PORTE (&test_porte)
#define PORT_PCR_PS_MASK 0x1U
#define PORT_PCR_PE_MASK 0x2U
#define PORT_PCR_PFE_MASK 0x10U
#define PORT_PCR_DSE_MASK 0x40U
#define PORT_PCR_MUX_MASK 0x700U
#define PORT_PCR_MUX(x) (((uint32_t)(x) << 8U) & PORT_PCR_MUX_MASK)
#define PORT_PCR_LK_MASK 0x8000U
#define PORT_PCR_IRQC_MASK 0xF0000U
#define PORT_PCR_ISF_MASK 0x1000000U
#endif
