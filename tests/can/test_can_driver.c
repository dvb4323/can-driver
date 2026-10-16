#include "can_driver.h"
#include "can_cfg.h"
#include "FlexCAN_Ip.h"
#include "FlexCAN_Ip_HwAccess.h"
#include "IntCtrl_Ip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const Flexcan_Ip_ConfigType FlexCAN_Config0 = {
    .max_num_mb=16, .flexcanMode=FLEXCAN_LOOPBACK_MODE, .is_pe_clock=TRUE,
    .bitrate={7,4,1,5,1}
};
Flexcan_Ip_StateType FlexCAN_State0;
static bool initialized, enabled, rx_busy, tx_busy, rx_ready, tx_complete;
static bool stop_ack = true;
static Flexcan_Ip_StatusType init_result, receive_result, start_result, stop_result;
static Flexcan_Ip_StatusType send_result, tx_status_error;
static Flexcan_Ip_MsgBuffType *rx_destination;
static Flexcan_Ip_MsgBuffType incoming;
static unsigned sends, writes, receives, disabled_irqs;
static uint8 sent_bytes[8];
static int expected_mode = FLEXCAN_LOOPBACK_MODE;
static const Flexcan_Ip_ConfigType *effective_config;
static uint32 error_status;
static uint8 tx_errors, rx_errors;
static unsigned diagnostic_reads;

void IntCtrl_Ip_DisableIrq(IRQn_Type irq)
{ assert(irq >= 78 && irq <= 82); ++disabled_irqs; }
void IntCtrl_Ip_ClearPending(IRQn_Type irq)
{ assert(irq >= 78 && irq <= 82); }
Flexcan_Ip_StatusType FlexCAN_Ip_Init(uint8 i, Flexcan_Ip_StateType *s, const Flexcan_Ip_ConfigType *c)
{
    Flexcan_Ip_ConfigType expected = FlexCAN_Config0;
    assert(i==0 && s==&FlexCAN_State0 && c!=&FlexCAN_Config0);
    assert(c->flexcanMode==expected_mode);
    expected.flexcanMode=expected_mode;
    /* Compare named fields, not padding bytes. */
    assert(c->max_num_mb==expected.max_num_mb && c->is_pe_clock==expected.is_pe_clock);
    assert(c->is_rx_fifo_needed==expected.is_rx_fifo_needed && c->fd_enable==expected.fd_enable);
    assert(c->bitRateSwitch==expected.bitRateSwitch && c->enhCbtEnable==expected.enhCbtEnable);
    assert(c->payload.payloadBlock0==expected.payload.payloadBlock0);
    assert(c->Callback==expected.Callback && c->ErrorCallback==expected.ErrorCallback);
    assert(c->bitrate.propSeg==expected.bitrate.propSeg && c->bitrate.phaseSeg1==expected.bitrate.phaseSeg1);
    assert(c->bitrate.phaseSeg2==expected.bitrate.phaseSeg2 && c->bitrate.preDivider==expected.bitrate.preDivider);
    assert(c->bitrate.rJumpwidth==expected.bitrate.rJumpwidth);
    assert(FlexCAN_Config0.flexcanMode==FLEXCAN_LOOPBACK_MODE);
    effective_config=c;
    assert(!initialized);
    if (init_result != FLEXCAN_STATUS_SUCCESS) return init_result;
    initialized=true; enabled=true; tx_busy=false; rx_busy=false;
    return FLEXCAN_STATUS_SUCCESS;
}
Flexcan_Ip_StatusType FlexCAN_Ip_DisableInterrupts(uint8 i)
{ assert(i==0 && initialized); return FLEXCAN_STATUS_SUCCESS; }
Flexcan_Ip_StatusType FlexCAN_Ip_SetStartMode(uint8 i)
{ assert(i==0 && initialized); enabled=true; return start_result; }
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigRxMb(uint8 i, uint8 mb, const Flexcan_Ip_DataInfoType *info, uint32 id)
{
    assert(i==0 && mb==1 && enabled && initialized);
    assert(info->is_polling && !info->is_remote && !info->fd_enable && id==0x14);
    return FLEXCAN_STATUS_SUCCESS;
}
Flexcan_Ip_StatusType FlexCAN_Ip_Receive(uint8 i, uint8 mb, Flexcan_Ip_MsgBuffType *dst, boolean poll)
{
    assert(i==0 && mb==1 && initialized && enabled && poll && dst);
    assert(!rx_busy); /* Re-registration must follow completion/abort. */
    ++receives;
    if (receive_result != FLEXCAN_STATUS_SUCCESS) return receive_result;
    rx_destination=dst; rx_busy=true; return FLEXCAN_STATUS_SUCCESS;
}
Flexcan_Ip_StatusType FlexCAN_Ip_Send(uint8 i, uint8 mb, const Flexcan_Ip_DataInfoType *info, uint32 id, const uint8 *src)
{
    assert(i==0 && mb==0 && initialized && enabled && !tx_busy && id<=0x7FF);
    assert(info->is_polling && !info->is_remote && !info->fd_enable && !info->enable_brs);
    assert(((uintptr_t)src % sizeof(uint32))==0);
    ++sends;
    if (send_result != FLEXCAN_STATUS_SUCCESS) return send_result;
    memcpy(sent_bytes,src,8); tx_busy=true; return FLEXCAN_STATUS_SUCCESS;
}
void FlexCAN_Ip_MainFunctionWrite(uint8 i, uint8 mb)
{ assert(i==0 && mb==0 && initialized); ++writes; if(tx_complete) tx_busy=false; }
void FlexCAN_Ip_MainFunctionRead(uint8 i, uint8 mb)
{
    assert(i==0 && mb==1 && initialized && rx_busy);
    if(rx_ready) { *rx_destination=incoming; rx_busy=false; rx_ready=false; }
}
Flexcan_Ip_StatusType FlexCAN_Ip_GetTransferStatus(uint8 i, uint8 mb)
{
    assert(i==0 && initialized);
    if(mb==0 && tx_status_error!=FLEXCAN_STATUS_SUCCESS) return tx_status_error;
    return (mb==0 ? tx_busy : rx_busy) ? FLEXCAN_STATUS_BUSY : FLEXCAN_STATUS_SUCCESS;
}
Flexcan_Ip_StatusType FlexCAN_Ip_AbortTransfer(uint8 i, uint8 mb)
{ assert(i==0 && mb==1 && initialized); rx_busy=false; return FLEXCAN_STATUS_SUCCESS; }
Flexcan_Ip_StatusType FlexCAN_Ip_SetStopMode(uint8 i)
{ assert(i==0 && initialized && !tx_busy); enabled=false; return stop_result; }
Flexcan_Ip_StatusType FlexCAN_Ip_Deinit(uint8 i)
{ assert(i==0); initialized=false; enabled=false; rx_busy=false; tx_busy=false; return FLEXCAN_STATUS_SUCCESS; }
boolean FlexCAN_Ip_GetStopMode(uint8 i)
{ assert(i==0); return stop_ack; }

uint32 FlexCAN_Ip_GetErrorStatus(uint8 i)
{
    uint32 result=error_status;
    assert(i==0 && initialized && enabled);
    ++diagnostic_reads;
    error_status &= ~0xFC00U; /* Protocol error events clear on read. */
    return result;
}
uint8 FlexCAN_Ip_GetControllerTxErrorCounter(uint8 i)
{ assert(i==0 && initialized && enabled); return tx_errors; }
uint8 FlexCAN_Ip_GetControllerRxErrorCounter(uint8 i)
{ assert(i==0 && initialized && enabled); return rx_errors; }

static void init_and_start(void)
{
    const Can_Config config=CAN_CONFIG_DEFAULT;
    assert(CanDriver_Init(&config)==CAN_STATUS_OK);
    assert(CanDriver_Init(&config)==CAN_STATUS_BAD_STATE);
    assert(CanDriver_GetState()==CAN_DRIVER_READY);
    assert(CanDriver_Start()==CAN_STATUS_OK);
}
static void close_driver(void)
{
    assert(CanDriver_Stop()==CAN_STATUS_OK);
    assert(CanDriver_Deinit()==CAN_STATUS_OK);
    assert(CanDriver_GetState()==CAN_DRIVER_UNINITIALIZED);
}
static void inject(uint32 cs)
{
    memset(&incoming,0,sizeof incoming);
    incoming.cs=cs; incoming.msgId=0x14; incoming.dataLen=8;
    for(unsigned i=0;i<8;++i) incoming.data[i]=(uint8)(i+1);
    rx_ready=true;
}
int main(void)
{
    Can_Frame frame={.id=0x14,.dlc=8,.data={1,2,3,4,5,6,7,0}}, out, sentinel;
    Can_Config invalid={.rx_id=0x800,.mode=CAN_MODE_LOOPBACK};
    Can_Diagnostics diag, saved;
    assert(CanDriver_GetDiagnostics(NULL)==CAN_STATUS_INVALID_ARGUMENT);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && !diag.valid);
    assert(CanDriver_PollDiagnostics()==CAN_STATUS_BAD_STATE);
    assert(CanDriver_Init(NULL)==CAN_STATUS_INVALID_ARGUMENT);
    assert(CanDriver_Init(&invalid)==CAN_STATUS_INVALID_ARGUMENT);
    invalid.rx_id=0x14; invalid.mode=(Can_Mode)99;
    assert(CanDriver_Init(&invalid)==CAN_STATUS_INVALID_ARGUMENT && disabled_irqs==0);
    assert(CanDriver_Start()==CAN_STATUS_BAD_STATE);
    assert(CanDriver_PollTx()==CAN_STATUS_BAD_STATE);
    assert(CanDriver_PollRx(NULL)==CAN_STATUS_INVALID_ARGUMENT);
    init_and_start();
    assert(effective_config->flexcanMode==FLEXCAN_LOOPBACK_MODE);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && !diag.valid);
    error_status=FLEXCAN_ESR1_ACKERR_MASK | 0x10U; tx_errors=100; rx_errors=2;
    assert(CanDriver_PollDiagnostics()==CAN_STATUS_OK && diagnostic_reads==1);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && diagnostic_reads==1);
    assert(diag.valid && diag.latest.ack_error && diag.latest.tx_error_counter==100);
    assert(diag.latest.rx_error_counter==2 && diag.latest.fault_state==CAN_FAULT_PASSIVE);
    assert(diag.first_error_valid && diag.first_error.esr1==(FLEXCAN_ESR1_ACKERR_MASK|0x10U));
    assert(diag.accumulated_error_events==FLEXCAN_ESR1_ACKERR_MASK);
    assert(CanDriver_PollDiagnostics()==CAN_STATUS_OK);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && !diag.latest.ack_error);
    assert(diag.first_error.ack_error && diag.sample_count==2);
    error_status=0; tx_errors=0; rx_errors=0;
    assert(CanDriver_Deinit()==CAN_STATUS_BAD_STATE);
    assert(CanDriver_PollTx()==CAN_STATUS_NO_TRANSFER);
    assert(CanDriver_Transmit(NULL)==CAN_STATUS_INVALID_ARGUMENT);
    frame.id=0x800; assert(CanDriver_Transmit(&frame)==CAN_STATUS_INVALID_ARGUMENT);
    frame.id=0x14; frame.dlc=9; assert(CanDriver_Transmit(&frame)==CAN_STATUS_INVALID_ARGUMENT);
    frame.dlc=8;
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_OK);
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_BUSY && sends==1 && writes==0);
    assert(CanDriver_Stop()==CAN_STATUS_BUSY);
    assert(CanDriver_PollTx()==CAN_STATUS_BUSY);
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_BUSY);
    frame.data[0]=99; assert(sent_bytes[0]==1);
    tx_complete=true;
    assert(CanDriver_PollTx()==CAN_STATUS_OK);
    assert(CanDriver_PollTx()==CAN_STATUS_NO_TRANSFER);
    frame.dlc=0; assert(CanDriver_Transmit(&frame)==CAN_STATUS_OK);
    assert(CanDriver_PollTx()==CAN_STATUS_OK);
    frame.id=0x7FF; frame.dlc=7; assert(CanDriver_Transmit(&frame)==CAN_STATUS_OK);
    assert(CanDriver_PollTx()==CAN_STATUS_OK);
    memset(&sentinel,0xA5,sizeof sentinel); out=sentinel;
    assert(CanDriver_PollRx(&out)==CAN_STATUS_NO_DATA && memcmp(&out,&sentinel,sizeof out)==0);
    for(unsigned n=0;n<4;++n) {
        unsigned before=receives;
        inject(0x02080000UL);
        assert(CanDriver_PollRx(&out)==CAN_STATUS_OK);
        assert(out.id==0x14 && out.dlc==8 && out.data[0]==1 && out.data[7]==8);
        assert(receives==before+1 && rx_busy);
    }
    inject(0x06080000UL); assert(CanDriver_PollRx(&out)==CAN_STATUS_RX_OVERRUN);
    out=sentinel; inject(0x02080000UL|FLEXCAN_IP_CS_RTR_MASK);
    assert(CanDriver_PollRx(&out)==CAN_STATUS_INVALID_FRAME && rx_busy);
    assert(memcmp(&out,&sentinel,sizeof out)==0);
    inject(0x020F0000UL); assert(CanDriver_PollRx(&out)==CAN_STATUS_INVALID_FRAME);
    inject(0x02080000UL|FLEXCAN_IP_CS_IDE_MASK);
    assert(CanDriver_PollRx(&out)==CAN_STATUS_INVALID_FRAME);
    inject(0x02080000UL|FLEXCAN_IP_MB_EDL_MASK);
    assert(CanDriver_PollRx(&out)==CAN_STATUS_INVALID_FRAME);
    assert(CanDriver_Stop()==CAN_STATUS_OK);
    assert(CanDriver_Start()==CAN_STATUS_OK); /* Must enable before MB config. */
    close_driver();
    assert(CanDriver_GetDiagnostics(&saved)==CAN_STATUS_OK && saved.valid);
    { unsigned before=diagnostic_reads;
    init_result=FLEXCAN_STATUS_TIMEOUT;
    { const Can_Config cfg=CAN_CONFIG_DEFAULT;
      assert(CanDriver_Init(&cfg)==CAN_STATUS_TIMEOUT); }
    assert(diagnostic_reads==before); /* Failed init cannot sample the device. */
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK);
    assert(diag.latest.esr1==saved.latest.esr1 && diag.sample_count==saved.sample_count);
    }
    assert(CanDriver_GetState()==CAN_DRIVER_FAULT);
    assert(CanDriver_Start()==CAN_STATUS_BAD_STATE);
    assert(CanDriver_Deinit()==CAN_STATUS_OK); /* No published RTD state. */
    init_result=FLEXCAN_STATUS_SUCCESS;
    init_and_start();
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && !diag.valid && !diag.first_fault_valid);
    receive_result=FLEXCAN_STATUS_BUSY; inject(0x02080000UL); out=sentinel;
    assert(CanDriver_PollRx(&out)==CAN_STATUS_BUSY);
    assert(CanDriver_GetState()==CAN_DRIVER_FAULT && memcmp(&out,&sentinel,sizeof out)==0);
    stop_ack=false; assert(CanDriver_Deinit()==CAN_STATUS_HW_ERROR);
    assert(CanDriver_GetState()==CAN_DRIVER_FAULT);
    stop_ack=true; assert(CanDriver_Deinit()==CAN_STATUS_OK);
    receive_result=FLEXCAN_STATUS_SUCCESS;
    init_and_start(); send_result=FLEXCAN_STATUS_BUSY;
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_BUSY);
    assert(CanDriver_GetState()==CAN_DRIVER_FAULT);
    assert(CanDriver_Deinit()==CAN_STATUS_OK);
    send_result=FLEXCAN_STATUS_SUCCESS;
    init_and_start();
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_OK);
    tx_status_error=FLEXCAN_STATUS_ERROR;
    error_status=FLEXCAN_ESR1_BIT0ERR_MASK;
    assert(CanDriver_PollTx()==CAN_STATUS_HW_ERROR);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && diag.first_fault_valid);
    assert(diag.first_fault_sample_valid && diag.first_fault.bit0_error && diag.tx_pending);
    assert(diag.driver_state==CAN_DRIVER_FAULT);
    { unsigned before=diagnostic_reads;
      assert(CanDriver_GetDiagnostics(&saved)==CAN_STATUS_OK && diagnostic_reads==before);
      assert(CanDriver_PollDiagnostics()==CAN_STATUS_BAD_STATE && diagnostic_reads==before); }
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_BAD_STATE);
    assert(CanDriver_Deinit()==CAN_STATUS_OK);
    tx_status_error=FLEXCAN_STATUS_SUCCESS;
    expected_mode=FLEXCAN_NORMAL_MODE;
    {
        Can_Config normal={.rx_id=0x14,.mode=CAN_MODE_NORMAL};
        assert(CanDriver_Init(&normal)==CAN_STATUS_OK);
        normal.mode=CAN_MODE_LOOPBACK; /* Caller lifetime cannot alter private config. */
        assert(effective_config->flexcanMode==FLEXCAN_NORMAL_MODE);
        assert(CanDriver_Start()==CAN_STATUS_OK);
    }
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_OK);
    tx_complete=false;
    error_status=0x20U | FLEXCAN_ESR1_BOFFINT_MASK;
    assert(CanDriver_PollDiagnostics()==CAN_STATUS_OK);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && diag.bus_off_seen);
    assert(diag.latest.fault_state==CAN_FAULT_BUS_OFF && diag.first_fault_valid);
    assert(diag.first_fault.fault_state==CAN_FAULT_BUS_OFF && diag.tx_pending);
    error_status=0;
    assert(CanDriver_PollDiagnostics()==CAN_STATUS_OK);
    assert(CanDriver_GetDiagnostics(&diag)==CAN_STATUS_OK && diag.bus_off_seen);
    assert(diag.latest.fault_state==CAN_FAULT_ACTIVE && diag.first_fault.fault_state==CAN_FAULT_BUS_OFF);
    assert(CanDriver_GetState()==CAN_DRIVER_RUNNING);
    assert(CanDriver_Transmit(&frame)==CAN_STATUS_BUSY);
    assert(CanDriver_Stop()==CAN_STATUS_BUSY);
    assert(CanDriver_PollTx()==CAN_STATUS_BUSY);
    tx_complete=true;
    assert(CanDriver_PollTx()==CAN_STATUS_OK);
    close_driver();
    assert(disabled_irqs>=10);
    puts("PASS: modes/config copy, cached diagnostics/events/faults, lifecycle, TX ownership, RX re-registration");
    return 0;
}
