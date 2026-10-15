#ifndef TEST_INTCTRL_IP_H
#define TEST_INTCTRL_IP_H
typedef enum {
    CAN0_ORed_IRQn=78, CAN0_Error_IRQn, CAN0_Wake_Up_IRQn,
    CAN0_ORed_0_15_MB_IRQn, CAN0_ORed_16_31_MB_IRQn
} IRQn_Type;
void IntCtrl_Ip_DisableIrq(IRQn_Type irq);
void IntCtrl_Ip_ClearPending(IRQn_Type irq);
#endif
