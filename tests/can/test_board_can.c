#include "board_can.h"
#include "S32K144_PCC.h"
#include "S32K144_PORT.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

PCC_Type test_pcc;
PORT_Type test_porte;

int main(void)
{
    BoardCan_Diagnostics diag;
    const uint32_t old4=PORT_PCR_MUX(2) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK |
        PORT_PCR_IRQC_MASK | PORT_PCR_ISF_MASK | PORT_PCR_DSE_MASK;
    test_porte.PCR[4]=old4;
    test_porte.PCR[5]=PORT_PCR_MUX(3) | PORT_PCR_PFE_MASK;
    test_porte.PCR[6]=0xA5U;
    assert(BoardCan_GetDiagnostics(NULL)==BOARD_CAN_INVALID_ARGUMENT);
    assert(BoardCan_InitPins()==BOARD_CAN_CLOCK_DISABLED);
    assert(test_porte.PCR[4]==old4 && test_porte.PCR[5]==(PORT_PCR_MUX(3)|PORT_PCR_PFE_MASK));
    assert(BoardCan_GetDiagnostics(&diag)==BOARD_CAN_OK && !diag.pcr_valid);
    test_pcc.PCCn[PCC_PORTE_INDEX]=PCC_PCCn_CGC_MASK | PCC_PCCn_PR_MASK;
    test_porte.PCR[5] |= PORT_PCR_LK_MASK;
    assert(BoardCan_InitPins()==BOARD_CAN_PIN_LOCKED);
    assert(test_porte.PCR[4]==old4); /* Check both locks before any mutation. */
    test_porte.PCR[5]=PORT_PCR_MUX(5) | PORT_PCR_LK_MASK;
    assert(BoardCan_InitPins()==BOARD_CAN_OK);
    assert(BoardCan_GetDiagnostics(&diag)==BOARD_CAN_OK && diag.pcr_valid);
    assert(diag.pcr4_before==old4);
    /* Plain fake storage observes the write value, not real W1C behavior:
     * confirms ISF=0 is written and unrelated pad settings are retained.
     */
    assert(test_porte.PCR[4]==(PORT_PCR_MUX(5)|PORT_PCR_DSE_MASK));
    assert(test_porte.PCR[5]==(PORT_PCR_MUX(5)|PORT_PCR_LK_MASK));
    assert(test_porte.PCR[6]==0xA5U);
    assert(test_pcc.PCCn[PCC_PORTE_INDEX]==(PCC_PCCn_CGC_MASK|PCC_PCCn_PR_MASK));
    puts("PASS: board clock/locks, two-pin mux, unrelated pads, W1C write value");
    return 0;
}
