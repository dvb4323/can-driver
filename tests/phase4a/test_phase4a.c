/* Deterministic host tests: real codec/app/mock, fake CAN wrapper, injected ms.
 * No RTD or physical motor peripheral is linked.
 */
#include "can_protocol.h"
#include "motor_app.h"
#include "motor_mock.h"
#include "app_time.h"
#include "can_driver.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static MotorMock mock;
static Can_DriverState can_state;
static Can_Frame rx, sent[512];
static bool rx_ready, pending, tx_forever;
static Can_Status rx_result, tx_result, diag_result, send_result;
static Can_Diagnostics diag;
static unsigned sent_count, tx_polls;

Can_DriverState CanDriver_GetState(void) { return can_state; }
Can_Status CanDriver_GetDiagnostics(Can_Diagnostics *out)
{ *out=diag; out->tx_pending=(uint8_t)pending; out->driver_state=can_state; return CAN_STATUS_OK; }
Can_Status CanDriver_PollDiagnostics(void)
{ assert(can_state==CAN_DRIVER_RUNNING); return diag_result; }
Can_Status CanDriver_PollRx(Can_Frame *out)
{
    assert(can_state==CAN_DRIVER_RUNNING);
    if (!rx_ready) return CAN_STATUS_NO_DATA;
    rx_ready=false;
    if (rx_result==CAN_STATUS_OK || rx_result==CAN_STATUS_RX_OVERRUN) *out=rx;
    return rx_result;
}
Can_Status CanDriver_Transmit(const Can_Frame *in)
{
    assert(!pending && can_state==CAN_DRIVER_RUNNING);
    assert(in->id==CP_RESPONSE_ID && in->dlc==8 && sent_count<512);
    if (send_result!=CAN_STATUS_OK) return send_result;
    sent[sent_count++]=*in; pending=true; return CAN_STATUS_OK;
}
Can_Status CanDriver_PollTx(void)
{
    assert(pending && can_state==CAN_DRIVER_RUNNING); ++tx_polls;
    if (tx_result!=CAN_STATUS_OK) return tx_result;
    if (tx_forever) return CAN_STATUS_BUSY;
    pending=false; return CAN_STATUS_OK;
}
static void Reset(bool reverse_supported)
{
    Motor_If motor;
    MotorMock_Init(&mock,reverse_supported); motor=MotorMock_Interface(&mock);
    can_state=CAN_DRIVER_RUNNING; pending=false; rx_ready=false; tx_forever=false;
    rx_result=CAN_STATUS_OK; tx_result=CAN_STATUS_OK;
    send_result=CAN_STATUS_OK; diag_result=CAN_STATUS_OK;
    memset(&diag,0,sizeof diag); diag.valid=1U;
    sent_count=0; tx_polls=0;
    assert(MotorApp_Init(&motor,0U));
    assert(g_motor_app.state==CP_UNCONFIGURED && !g_motor_app.configured);
    assert(mock.state.safe_off_confirmed && !mock.state.enabled && !mock.state.requested_duty);
}
static void Send(uint8_t seq, uint8_t opcode, uint16_t arg, uint32_t now)
{
    memset(&rx,0,sizeof rx); rx.id=CP_COMMAND_ID; rx.dlc=8;
    rx.data[0]=seq; rx.data[1]=opcode; rx.data[2]=(uint8_t)arg; rx.data[3]=(uint8_t)(arg>>8);
    rx_ready=true; MotorApp_Process(now);
}
static Can_Frame LastAck(void)
{
    unsigned n=sent_count;
    while (n!=0U) { --n; if (sent[n].data[0]==CP_ACK_NACK) return sent[n]; }
    assert(false); return sent[0];
}
static void ExpectAck(Cp_Result r)
{
    unsigned i;
    /* Drain bounded response work at the SAME injected time. Error events
     * may legitimately precede the terminal ACK being asserted here.
     */
    for (i=0U;i<16U && (g_motor_app.ack_count!=0U || g_motor_app.tx_pending);++i)
        MotorApp_Process(g_motor_app.now_ms);
    Can_Frame ack=LastAck();
    assert(ack.data[2]==(uint8_t)r && ack.data[7]==1);
}
static void StartRunning(bool reverse)
{
    Send(1,CP_SET_MAX_DUTY,700,0); ExpectAck(CP_OK);
    Send(2,CP_KEEPALIVE,0,1);
    Send(3,reverse ? CP_START_REVERSE : CP_START_FORWARD,0,2);
    ExpectAck(CP_PENDING); assert(g_motor_app.state==CP_STARTING && !mock.state.enabled);
    MotorApp_Process(12);
    ExpectAck(CP_COMPLETED);
    assert(g_motor_app.state==CP_RUNNING && mock.state.enabled && g_motor_app.requested_duty==0);
}
static void CodecTests(void)
{
    uint8_t data[8]={0x12,CP_SET_DUTY,0x2C,0x01,0,0,0,0};
    uint8_t out[8]; unsigned i;
    const uint8_t ack[8]={1,0x12,0,CP_SET_DUTY,0x2C,1,0,1};
    const uint8_t status[8]={2,0,CP_RUNNING,0x1D,0x2C,1,0,1};
    const uint8_t error[8]={3,0,CP_FAULT_COMMUNICATION,6,0,0,CP_FAULT_COMMUNICATION,1};
    Cp_DecodeResult r=CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data);
    assert(r.valid && r.command.argument==300 && r.command.sequence==0x12);
    CanProtocol_EncodeAck(out,&r.command,CP_OK,CP_FAULT_NONE);
    assert(memcmp(out,ack,8)==0);
    CanProtocol_EncodeStatus(out,CP_RUNNING,0x1D,300,CP_FAULT_NONE);
    assert(memcmp(out,status,8)==0);
    CanProtocol_EncodeError(out,6,0,CP_FAULT_COMMUNICATION);
    assert(memcmp(out,error,8)==0);
    assert(!CanProtocol_Decode(0x101,false,false,8,data).respond);
    assert(!CanProtocol_Decode(CP_COMMAND_ID,true,false,8,data).valid);
    assert(!CanProtocol_Decode(CP_COMMAND_ID,false,true,8,data).valid);
    assert(!CanProtocol_Decode(CP_COMMAND_ID,false,false,8,NULL).respond);
    for (i=0;i<8;++i) {
        r=CanProtocol_Decode(CP_COMMAND_ID,false,false,(uint8_t)i,data);
        assert(!r.valid && r.respond==(i>=2));
    }
    assert(!CanProtocol_Decode(CP_COMMAND_ID,false,false,9,data).valid);
    for (i=4;i<8;++i) {
        data[i]=1; assert(CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data).result==CP_INVALID_ARGUMENT); data[i]=0;
    }
    data[1]=99; assert(CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data).result==CP_INVALID_COMMAND);
    data[1]=CP_STOP; assert(CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data).result==CP_INVALID_ARGUMENT);
    data[1]=CP_SET_DUTY; data[2]=0xE9; data[3]=3;
    assert(CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data).result==CP_INVALID_ARGUMENT);
    data[2]=0xE8; assert(CanProtocol_Decode(CP_COMMAND_ID,false,false,8,data).valid);
}
static void StateTests(void)
{
    Reset(false);
    Send(1,CP_START_FORWARD,0,0); ExpectAck(CP_NOT_CONFIGURED); assert(!mock.state.enabled);
    Send(2,CP_SET_DUTY,10,1); ExpectAck(CP_INVALID_STATE);
    Send(3,CP_SET_MAX_DUTY,700,2); ExpectAck(CP_OK); assert(g_motor_app.state==CP_STOPPED);
    Send(4,CP_START_REVERSE,0,3); ExpectAck(CP_SAFETY_INTERLOCK); assert(mock.start_requests==0);
    Send(5,CP_START_FORWARD,0,4); ExpectAck(CP_PENDING);
    MotorApp_Process(14); assert(g_motor_app.state==CP_RUNNING);
    Send(6,CP_SET_DUTY,701,15); ExpectAck(CP_INVALID_ARGUMENT); assert(!mock.state.requested_duty);
    Send(7,CP_SET_DUTY,650,16); ExpectAck(CP_OK); assert(mock.state.requested_duty==650);
    Send(8,CP_SET_MAX_DUTY,500,17); ExpectAck(CP_INVALID_STATE); assert(g_motor_app.max_duty==700);
    Send(9,CP_GET_STATUS,0,18); ExpectAck(CP_OK);
    Send(10,CP_STOP,0,19); ExpectAck(CP_PENDING); assert(!mock.state.enabled && !g_motor_app.requested_duty);
    MotorApp_Process(20); ExpectAck(CP_COMPLETED); assert(g_motor_app.state==CP_STOPPED);
    Send(11,CP_SET_MAX_DUTY,0,21); Send(12,CP_START_FORWARD,0,22); ExpectAck(CP_SAFETY_INTERLOCK);
    Reset(true); Send(1,CP_SET_MAX_DUTY,500,0);
    Send(2,CP_START_FORWARD,0,500); ExpectAck(CP_SAFETY_INTERLOCK); /* Expired stopped lease. */
    Reset(true); StartRunning(true); assert(mock.state.reverse);
    Send(4,CP_START_FORWARD,0,13); ExpectAck(CP_INVALID_STATE);
    Send(5,CP_STOP,0,14); MotorApp_Process(15);
    Send(6,CP_START_FORWARD,0,16); MotorApp_Process(26);
    assert(g_motor_app.state==CP_RUNNING && !mock.state.reverse && !g_motor_app.requested_duty);
}
static void SequenceTests(void)
{
    Reset(true);
    Send(254,CP_KEEPALIVE,0,0); Send(255,CP_KEEPALIVE,0,1);
    Send(0,CP_KEEPALIVE,0,2); Send(1,CP_KEEPALIVE,0,3); ExpectAck(CP_OK);
    Send(1,CP_GET_STATUS,0,4); ExpectAck(CP_SEQUENCE_CONFLICT);
    Send(200,CP_KEEPALIVE,0,5); ExpectAck(CP_STALE_SEQUENCE);
    Reset(true); Send(1,CP_SET_MAX_DUTY,700,0); Send(2,CP_START_FORWARD,0,1);
    Send(3,CP_KEEPALIVE,0,2); Send(2,CP_START_FORWARD,0,3);
    ExpectAck(CP_PENDING); assert(mock.start_requests==1 && g_motor_app.duplicates==1);
    MotorApp_Process(11); ExpectAck(CP_COMPLETED);
    Send(2,CP_START_FORWARD,0,12); ExpectAck(CP_COMPLETED); assert(mock.start_requests==1);
    Reset(true); StartRunning(false);
    Send(3,CP_START_FORWARD,0,400); /* Duplicate cannot extend lease from t=2. */
    MotorApp_Process(502); assert(g_motor_app.fault==CP_FAULT_COMMUNICATION && !mock.state.enabled);
    Reset(true); StartRunning(false);
    /* Malformed command cannot refresh lease or actuate: reserved byte set. */
    memset(&rx,0,sizeof rx); rx.id=CP_COMMAND_ID; rx.dlc=8;
    rx.data[0]=4; rx.data[1]=CP_KEEPALIVE; rx.data[7]=1; rx_ready=true;
    MotorApp_Process(400); ExpectAck(CP_INVALID_ARGUMENT);
    MotorApp_Process(502); assert(g_motor_app.fault==CP_FAULT_COMMUNICATION);
}
static void StopAndTimeoutTests(void)
{
    Reset(true); tx_forever=true;
    Send(1,CP_SET_MAX_DUTY,700,0); Send(2,CP_START_FORWARD,0,1);
    uint32_t old=mock.operation;
    Send(200,CP_STOP,0,2); /* Stale STOP still cancels pending START. */
    assert(!mock.start_pending && !mock.state.enabled && g_motor_app.state==CP_STOPPING);
    MotorApp_Process(3); assert(g_motor_app.state==CP_STOPPED);
    mock.state.completed_operation=old; mock.state.completion_result=MOTOR_OK;
    MotorApp_Process(20); assert(!mock.state.enabled && g_motor_app.state==CP_STOPPED);
    assert(sent_count==1 && pending && g_motor_app.tx_pending); /* No overwrite. */
    mock.state.enabled=true; /* A misbehaving late enable must be overridden. */
    MotorApp_Process(21); assert(!mock.state.enabled && g_motor_app.state==CP_FAULT);
}
static void BusyLeaseTests(void)
{
    Reset(true); tx_forever=true;
    Send(1,CP_SET_MAX_DUTY,700,0); Send(2,CP_START_FORWARD,0,1); MotorApp_Process(11);
    assert(g_motor_app.state==CP_RUNNING && pending && sent_count==1);
    MotorApp_Process(500); assert(g_motor_app.state==CP_RUNNING);
    MotorApp_Process(501); assert(g_motor_app.fault==CP_FAULT_COMMUNICATION && !mock.state.enabled);
    MotorApp_Process(502); assert(mock.state.safe_off_confirmed && pending && g_motor_app.tx_pending);
    assert(tx_polls>0 && sent_count==1 && !g_motor_app.tx_completed);
    Reset(true); Send(1,CP_SET_MAX_DUTY,700,0);
    mock.never_complete_start=true; Send(2,CP_START_FORWARD,0,1);
    MotorApp_Process(101); ExpectAck(CP_INTERNAL_TIMEOUT);
    assert(g_motor_app.fault==CP_FAULT_BACKEND_TIMEOUT && !mock.start_pending);
}
static void FaultTests(void)
{
    Reset(true); StartRunning(false);
    mock.state.fault_active=true; MotorApp_Process(13);
    assert(g_motor_app.fault==CP_FAULT_BACKEND && !mock.state.enabled);
    Send(4,CP_CLEAR_FAULT,0,14); ExpectAck(CP_SAFETY_INTERLOCK);
    MotorMock_SetFaultInput(&mock,false); MotorApp_Process(15);
    Send(5,CP_KEEPALIVE,0,16); Send(6,CP_CLEAR_FAULT,0,17); ExpectAck(CP_OK);
    assert(g_motor_app.state==CP_STOPPED && !mock.state.enabled && !g_motor_app.requested_duty);
    assert(g_motor_app.first_fault==CP_FAULT_BACKEND);
    Reset(true); Send(1,CP_SET_MAX_DUTY,700,0); rx_result=CAN_STATUS_RX_OVERRUN;
    Send(2,CP_START_FORWARD,0,1);
    assert(g_motor_app.fault==CP_FAULT_RX_OVERRUN && mock.start_requests==0);
    Reset(true); rx_result=CAN_STATUS_INVALID_FRAME; Send(1,CP_KEEPALIVE,0,0);
    assert(g_motor_app.fault==CP_FAULT_RX_INVALID);
    Reset(true); StartRunning(false); diag.bus_off_seen=1; MotorApp_Process(13);
    assert(g_motor_app.fault==CP_FAULT_BUS_OFF && !mock.state.enabled);
    Reset(true); MotorApp_Process(0); tx_result=CAN_STATUS_NO_TRANSFER; MotorApp_Process(1);
    assert(g_motor_app.fault==CP_FAULT_CAN_API && g_motor_app.tx_pending);
    Reset(true); send_result=CAN_STATUS_HW_ERROR; MotorApp_Process(0);
    assert(g_motor_app.fault==CP_FAULT_CAN_API);
    Reset(true); diag_result=CAN_STATUS_HW_ERROR; MotorApp_Process(0);
    assert(g_motor_app.fault==CP_FAULT_CAN_API);
    Reset(true); can_state=CAN_DRIVER_FAULT; MotorApp_Process(0);
    assert(g_motor_app.fault==CP_FAULT_CAN_API && !mock.state.enabled && sent_count==0);
    Reset(true); tx_forever=true; MotorApp_Process(0);
    for (uint8_t i=1;i<=9;++i) Send(i,CP_KEEPALIVE,0,i);
    assert(g_motor_app.fault==CP_FAULT_QUEUE && g_motor_app.queue_overflows && !mock.state.enabled);
}
static bool ReadInjected(void *ctx, uint32_t *out) { *out=*(uint32_t *)ctx; return true; }
static void TimeResetTests(void)
{
    uint32_t time=123, out=0;
    assert(!AppTime_InitTarget()); /* No target timer implicitly acquired on host. */
    assert(!AppTime_Read(&out)); assert(AppTime_Bind(ReadInjected,&time));
    assert(AppTime_Read(&out) && out==123 && !AppTime_Read(NULL));
    Reset(true);
    Motor_If motor=MotorMock_Interface(&mock);
    assert(MotorApp_Init(&motor,0xFFFFFFF0UL));
    MotorApp_Process(10); assert(g_motor_app.fault==CP_FAULT_NONE);
    MotorApp_Process(9); assert(g_motor_app.fault==CP_FAULT_TIME);
    Reset(true); StartRunning(false); Send(4,CP_SET_DUTY,600,13);
    assert(mock.state.enabled && mock.state.requested_duty==600);
    MotorMock_Init(&mock,true); motor=MotorMock_Interface(&mock);
    assert(MotorApp_Init(&motor,14)); MotorApp_Process(15);
    assert(g_motor_app.state==CP_UNCONFIGURED && !mock.state.enabled && !g_motor_app.requested_duty);
    Send(5,CP_GET_STATUS,0,16); assert(!mock.state.enabled);
    motor.is_mock=false; assert(!MotorApp_Init(&motor,17)); /* No physical backend. */
}
int main(void)
{
    CodecTests(); StateTests(); SequenceTests(); StopAndTimeoutTests();
    BusyLeaseTests(); FaultTests(); TimeResetTests();
    puts("PASS: Phase 4A codec, mock state/lease/dedup/STOP/queues/CAN faults/time/reset contracts");
    return 0;
}
