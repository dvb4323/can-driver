#include "motor_app.h"
#include "can_driver.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    bool used, pending;
    Cp_Command command;
    Cp_Result result;
    Cp_Fault fault;
    uint32_t completed_at;
} Cache;
typedef struct {
    bool active;
    Cp_Command command;
    int cache;
    uint32_t id, started_at;
} Operation;
typedef struct {
    Motor_If motor;
    bool initialized, time_valid, has_sequence, has_lease;
    bool status_due, error_due, bus_off_observed, reset_required;
    uint8_t last_sequence;
    uint32_t now, lease_at, last_status, last_error, next_operation;
    Cache cache[MOTOR_APP_CACHE_CAPACITY];
    Can_Frame ack[MOTOR_APP_ACK_CAPACITY];
    unsigned head, count;
    Operation operation; /* Active operation reserves one terminal ACK slot. */
    Can_Frame tx;
    bool tx_pending;
} App;
static App s;
volatile MotorApp_Debug g_motor_app;

static void Increment(volatile uint32_t *n) { if (*n!=UINT32_MAX) ++(*n); }
static bool LeaseFresh(void)
{ return s.time_valid && s.has_lease && (uint32_t)(s.now-s.lease_at)<MOTOR_APP_LEASE_MS; }
static void RefreshLease(void) { s.has_lease=true; s.lease_at=s.now; }
static Motor_Snapshot Snapshot(void) { return s.motor.snapshot(s.motor.context); }
static bool Room(unsigned n)
{ return s.count+(s.operation.active ? 1U : 0U)+n<=MOTOR_APP_ACK_CAPACITY; }
static void SaveDiagnostics(void)
{
    Can_Diagnostics d;
    if (CanDriver_GetDiagnostics(&d)==CAN_STATUS_OK) g_motor_app.diagnostics=d;
}
static uint8_t Flags(void)
{
    Motor_Snapshot m=Snapshot();
    return (uint8_t)((g_motor_app.configured ? CP_FLAG_CONFIGURED : 0U) |
        (m.safe_off_confirmed ? CP_FLAG_SAFE_OFF : 0U) |
        (s.motor.is_mock ? CP_FLAG_MOCK : 0U) |
        (m.reverse_supported ? CP_FLAG_REVERSE_SUPPORTED : 0U) |
        (LeaseFresh() ? CP_FLAG_LEASE_FRESH : 0U) |
        (s.operation.active ? CP_FLAG_OPERATION_PENDING : 0U) |
        (m.reverse ? CP_FLAG_REVERSE : 0U));
}
static void CacheResult(int index, Cp_Result result)
{
    if (index<0) return;
    s.cache[index].result=result; s.cache[index].fault=g_motor_app.fault;
    s.cache[index].pending=(result==CP_PENDING); s.cache[index].completed_at=s.now;
}
static bool PushAck(const Cp_Command *c, Cp_Result result, Cp_Fault fault)
{
    Can_Frame *frame;
    if (!Room(1U)) return false;
    frame=&s.ack[(s.head+s.count)%MOTOR_APP_ACK_CAPACITY];
    frame->id=CP_RESPONSE_ID; frame->dlc=8U;
    CanProtocol_EncodeAck(frame->data,c,result,fault);
    ++s.count;
    if (s.count>g_motor_app.ack_high_water) g_motor_app.ack_high_water=s.count;
    return true;
}
static void CompleteOperation(Cp_Result result)
{
    Operation op=s.operation;
    s.operation.active=false; /* Consume reserved terminal capacity. */
    CacheResult(op.cache,result);
    (void)PushAck(&op.command,result,g_motor_app.fault);
    s.status_due=true;
}
static void Trip(Cp_Fault fault)
{
    uint32_t bit=1UL<<(unsigned)fault;
    bool new_fault=(g_motor_app.fault_mask&bit)==0U;
    if (g_motor_app.first_fault==CP_FAULT_NONE) {
        SaveDiagnostics();
        g_motor_app.first_fault=fault; g_motor_app.first_fault_ms=s.now;
        g_motor_app.first_fault_diagnostics=g_motor_app.diagnostics;
    }
    if (g_motor_app.fault==CP_FAULT_NONE) g_motor_app.fault=fault;
    g_motor_app.fault_mask|=bit;
    g_motor_app.requested_duty=0U;
    if (new_fault || Snapshot().enabled) {
        s.motor.force_safe_off(s.motor.context,s.now);
        if (s.operation.active) {
            Cp_Result r=(fault==CP_FAULT_BACKEND_TIMEOUT) ? CP_INTERNAL_TIMEOUT :
                ((fault==CP_FAULT_CAN_API || fault==CP_FAULT_BUS_OFF) ? CP_CAN_ERROR : CP_SAFETY_INTERLOCK);
            CompleteOperation(r);
        }
    }
    if (fault==CP_FAULT_TIME || fault==CP_FAULT_CAN_API || fault==CP_FAULT_BUS_OFF)
        s.reset_required=true;
    g_motor_app.state=CP_FAULT;
    if (new_fault) { s.error_due=true; s.status_due=true; }
}
static void Reply(const Cp_Command *c, Cp_Result result)
{
    if (!PushAck(c,result,g_motor_app.fault)) {
        Increment(&g_motor_app.queue_overflows); Trip(CP_FAULT_QUEUE);
    }
}
static int Find(uint8_t seq)
{
    unsigned i;
    for (i=0U;i<MOTOR_APP_CACHE_CAPACITY;++i) {
        if (s.cache[i].used && !s.cache[i].pending &&
            (uint32_t)(s.now-s.cache[i].completed_at)>=1000U) s.cache[i].used=false;
        if (s.cache[i].used && s.cache[i].command.sequence==seq) return (int)i;
    }
    return -1;
}
static bool Same(const Cp_Command *a, const Cp_Command *b)
{ return memcmp(a->identity,b->identity,8U)==0; }
static int Remember(const Cp_Command *c)
{
    unsigned i, selected=0U;
    uint32_t age=0U;
    bool found=false;
    for (i=0U;i<MOTOR_APP_CACHE_CAPACITY;++i) {
        if (!s.cache[i].used) { selected=i; found=true; break; }
        if (!s.cache[i].pending && (!found || (uint32_t)(s.now-s.cache[i].completed_at)>=age)) {
            selected=i; age=s.now-s.cache[i].completed_at; found=true;
        }
    }
    if (!found) return -1;
    memset(&s.cache[selected],0,sizeof s.cache[selected]);
    s.cache[selected].used=true; s.cache[selected].command=*c;
    s.cache[selected].completed_at=s.now;
    return (int)selected;
}
static bool Fresh(uint8_t seq)
{
    uint8_t delta=(uint8_t)(seq-s.last_sequence);
    return !s.has_sequence || (delta!=0U && delta<128U);
}
static void Consume(uint8_t seq) { s.has_sequence=true; s.last_sequence=seq; }
static void BeginOperation(const Cp_Command *c, int cache, uint32_t id)
{
    s.operation.active=true; s.operation.command=*c; s.operation.cache=cache;
    s.operation.id=id; s.operation.started_at=s.now;
    CacheResult(cache,CP_PENDING);
    /* Caller pre-reserved both initial and terminal responses. */
    (void)PushAck(c,CP_PENDING,g_motor_app.fault);
    s.status_due=true;
}
static void Stop(const Cp_Command *c)
{
    int cache=Find(c->sequence);
    s.motor.force_safe_off(s.motor.context,s.now); g_motor_app.requested_duty=0U;
    if (s.operation.active && s.operation.command.opcode==CP_STOP && Same(c,&s.operation.command)) {
        Reply(c,CP_PENDING); return; /* No deadline extension on retry. */
    }
    if (s.operation.active) CompleteOperation(CP_SAFETY_INTERLOCK);
    if (cache>=0 && !Same(c,&s.cache[cache].command)) cache=-1;
    else if (cache<0 && Fresh(c->sequence)) { Consume(c->sequence); cache=Remember(c); }
    if (Snapshot().safe_off_confirmed) {
        g_motor_app.state=(g_motor_app.fault!=CP_FAULT_NONE) ? CP_FAULT :
            (g_motor_app.configured ? CP_STOPPED : CP_UNCONFIGURED);
        CacheResult(cache,CP_OK); Reply(c,CP_OK);
    } else if (Room(2U)) {
        if (g_motor_app.fault==CP_FAULT_NONE) g_motor_app.state=CP_STOPPING;
        BeginOperation(c,cache,0U);
    } else {
        Increment(&g_motor_app.queue_overflows); Trip(CP_FAULT_QUEUE);
    }
    s.status_due=true;
}
static void Handle(const Cp_Command *c)
{
    int index;
    Cp_Result result=CP_OK;
    Motor_Snapshot m=Snapshot();
    if (c->opcode==CP_STOP) { Stop(c); return; }
    index=Find(c->sequence);
    if (index>=0) {
        if (!Same(c,&s.cache[index].command)) {
            Increment(&g_motor_app.sequence_rejections); Reply(c,CP_SEQUENCE_CONFLICT);
        } else {
            Increment(&g_motor_app.duplicates);
            if (!PushAck(c,s.cache[index].result,s.cache[index].fault)) {
                Increment(&g_motor_app.queue_overflows); Trip(CP_FAULT_QUEUE);
            }
        }
        return;
    }
    if (!Fresh(c->sequence)) {
        Increment(&g_motor_app.sequence_rejections); Reply(c,CP_STALE_SEQUENCE); return;
    }
    if (!Room((c->opcode==CP_START_FORWARD || c->opcode==CP_START_REVERSE) ? 2U : 1U)) {
        if (Room(1U)) Reply(c,CP_BUSY);
        else { Increment(&g_motor_app.queue_overflows); Trip(CP_FAULT_QUEUE); }
        return; /* Capacity rejection does not consume sequence. */
    }
    Consume(c->sequence); index=Remember(c);
    if (index<0) { Reply(c,CP_BUSY); return; }
    switch (c->opcode) {
    case CP_START_FORWARD:
    case CP_START_REVERSE:
        if (!g_motor_app.configured) result=CP_NOT_CONFIGURED;
        else if (g_motor_app.fault!=CP_FAULT_NONE || m.fault_active) result=CP_MOTOR_FAULT;
        else if (g_motor_app.state!=CP_STOPPED || s.operation.active) result=CP_INVALID_STATE;
        else if (!LeaseFresh() || !s.time_valid || !m.safe_off_confirmed || g_motor_app.max_duty==0U ||
                 (c->opcode==CP_START_REVERSE && !m.reverse_supported)) result=CP_SAFETY_INTERLOCK;
        else {
            Motor_Result mr;
            ++s.next_operation;
            if (s.next_operation==0U) ++s.next_operation;
            mr=s.motor.start(s.motor.context,c->opcode==CP_START_REVERSE,s.next_operation,s.now);
            if (mr==MOTOR_PENDING) {
                g_motor_app.state=CP_STARTING; g_motor_app.requested_duty=0U;
                g_motor_app.last_operation=s.next_operation;
                RefreshLease(); BeginOperation(c,index,s.next_operation); return;
            }
            /* This mock contract always confirms asynchronously. */
            if (mr==MOTOR_BUSY) result=CP_BUSY;
            else if (mr==MOTOR_UNSUPPORTED) result=CP_SAFETY_INTERLOCK;
            else { Trip(CP_FAULT_BACKEND); result=CP_MOTOR_FAULT; }
        }
        break;
    case CP_SET_DUTY:
        if (g_motor_app.state!=CP_RUNNING || s.operation.active) result=CP_INVALID_STATE;
        else if (c->argument>g_motor_app.max_duty) result=CP_INVALID_ARGUMENT;
        else if (s.motor.set_duty(s.motor.context,c->argument)!=MOTOR_OK) {
            Trip(CP_FAULT_BACKEND); result=CP_MOTOR_FAULT;
        } else g_motor_app.requested_duty=c->argument;
        break;
    case CP_SET_MAX_DUTY:
        if ((g_motor_app.state!=CP_UNCONFIGURED && g_motor_app.state!=CP_STOPPED) ||
            !m.safe_off_confirmed || s.operation.active) result=CP_INVALID_STATE;
        else {
            g_motor_app.max_duty=c->argument; g_motor_app.configured=true;
            g_motor_app.requested_duty=0U; g_motor_app.state=CP_STOPPED;
        }
        break;
    case CP_GET_STATUS: s.status_due=true; break;
    case CP_KEEPALIVE: break;
    case CP_CLEAR_FAULT:
        if (g_motor_app.state!=CP_FAULT) result=CP_INVALID_STATE;
        else if (!m.safe_off_confirmed || m.fault_active || s.operation.active ||
                 s.reset_required || !LeaseFresh()) result=CP_SAFETY_INTERLOCK;
        else if (s.motor.clear_fault(s.motor.context)!=MOTOR_OK) result=CP_MOTOR_FAULT;
        else {
            g_motor_app.fault=CP_FAULT_NONE; g_motor_app.fault_mask=0U;
            g_motor_app.state=g_motor_app.configured ? CP_STOPPED : CP_UNCONFIGURED;
            g_motor_app.requested_duty=0U; s.error_due=true;
        }
        break;
    default: result=CP_INVALID_COMMAND; break;
    }
    if (result==CP_OK && c->opcode!=CP_GET_STATUS) RefreshLease();
    CacheResult(index,result); Reply(c,result); s.status_due=true;
}

bool MotorApp_Init(const Motor_If *motor, uint32_t now_ms)
{
    memset(&s,0,sizeof s);
    {
        const MotorApp_Debug empty={0}; g_motor_app=empty;
    }
    if (motor==NULL || !motor->is_mock || motor->context==NULL || motor->start==NULL ||
        motor->set_duty==NULL || motor->force_safe_off==NULL || motor->clear_fault==NULL ||
        motor->service==NULL || motor->snapshot==NULL) {
        g_motor_app.state=CP_FAULT; g_motor_app.fault=CP_FAULT_BACKEND; return false;
    }
    s.motor=*motor; s.initialized=true; s.time_valid=true; s.now=now_ms;
    s.last_status=now_ms; s.last_error=now_ms; s.status_due=true;
    s.motor.force_safe_off(s.motor.context,now_ms);
    g_motor_app.state=CP_UNCONFIGURED; g_motor_app.mock_backend=true;
    g_motor_app.time_valid=true; g_motor_app.safe_off_confirmed=Snapshot().safe_off_confirmed;
    return true;
}
void MotorApp_TimeFault(void)
{
    if (!s.initialized) return;
    s.time_valid=false; Trip(CP_FAULT_TIME); g_motor_app.time_valid=false;
}
static void ServiceMotor(void)
{
    Motor_Snapshot m;
    s.motor.service(s.motor.context,s.now); m=Snapshot();
    if (m.fault_active) Trip(CP_FAULT_BACKEND);
    if (s.operation.active) {
        if ((uint32_t)(s.now-s.operation.started_at)>=MOTOR_APP_OPERATION_MS) {
            Trip(CP_FAULT_BACKEND_TIMEOUT);
        } else if (s.operation.command.opcode==CP_STOP && m.safe_off_confirmed) {
            g_motor_app.state=(g_motor_app.fault!=CP_FAULT_NONE) ? CP_FAULT :
                (g_motor_app.configured ? CP_STOPPED : CP_UNCONFIGURED);
            CompleteOperation(CP_COMPLETED);
        } else if (s.operation.command.opcode!=CP_STOP && m.completed_operation==s.operation.id) {
            if (m.completion_result!=MOTOR_OK || !m.enabled || !LeaseFresh() || g_motor_app.fault!=CP_FAULT_NONE)
                Trip(CP_FAULT_BACKEND);
            else { g_motor_app.state=CP_RUNNING; g_motor_app.requested_duty=0U; CompleteOperation(CP_COMPLETED); }
        }
    }
    m=Snapshot();
    if (m.enabled && g_motor_app.state!=CP_RUNNING) Trip(CP_FAULT_BACKEND);
    g_motor_app.safe_off_confirmed=Snapshot().safe_off_confirmed;
    g_motor_app.reverse=Snapshot().reverse;
}
static void ServiceTx(void)
{
    Can_Status result;
    unsigned selected=0U;
    if (s.tx_pending) {
        result=CanDriver_PollTx(); g_motor_app.last_can_status=result;
        if (result==CAN_STATUS_OK) { s.tx_pending=false; Increment(&g_motor_app.tx_completed); }
        else if (result!=CAN_STATUS_BUSY) { Trip(CP_FAULT_CAN_API); return; }
    }
    if (s.tx_pending) return;
    if (s.error_due) {
        s.tx.id=CP_RESPONSE_ID; s.tx.dlc=8U;
        CanProtocol_EncodeError(s.tx.data,Flags(),g_motor_app.requested_duty,g_motor_app.fault); selected=1U;
    } else if (s.count!=0U) { s.tx=s.ack[s.head]; selected=2U; }
    else if (s.status_due) {
        s.tx.id=CP_RESPONSE_ID; s.tx.dlc=8U;
        CanProtocol_EncodeStatus(s.tx.data,g_motor_app.state,Flags(),g_motor_app.requested_duty,g_motor_app.fault); selected=3U;
    } else return;
    result=CanDriver_Transmit(&s.tx); g_motor_app.last_can_status=result;
    if (result!=CAN_STATUS_OK) { Trip(CP_FAULT_CAN_API); return; }
    s.tx_pending=true; g_motor_app.pending_tx=s.tx; Increment(&g_motor_app.tx_accepted);
    if (selected==1U) { s.error_due=false; s.last_error=s.now; }
    else if (selected==2U) { s.head=(s.head+1U)%MOTOR_APP_ACK_CAPACITY; --s.count; }
    else { s.status_due=false; s.last_status=s.now; }
}
void MotorApp_Process(uint32_t now_ms)
{
    Can_Status result;
    Can_Frame frame;
    if (!s.initialized) return;
    if ((uint32_t)(now_ms-s.now)>0x7FFFFFFFUL) MotorApp_TimeFault();
    else s.now=now_ms;
    if ((g_motor_app.state==CP_STARTING || g_motor_app.state==CP_RUNNING) && !LeaseFresh())
        Trip(CP_FAULT_COMMUNICATION); /* Before accepting a late queued heartbeat. */
    if (CanDriver_GetState()!=CAN_DRIVER_RUNNING) Trip(CP_FAULT_CAN_API);
    else {
        result=CanDriver_PollDiagnostics(); SaveDiagnostics();
        if (result!=CAN_STATUS_OK) Trip(CP_FAULT_CAN_API);
        if ((g_motor_app.diagnostics.bus_off_seen!=0U && !s.bus_off_observed) ||
            (g_motor_app.diagnostics.valid && g_motor_app.diagnostics.latest.fault_state==CAN_FAULT_BUS_OFF)) {
            s.bus_off_observed=true; Trip(CP_FAULT_BUS_OFF);
        }
        if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
            result=CanDriver_PollRx(&frame); g_motor_app.last_can_status=result;
            if (result==CAN_STATUS_OK || result==CAN_STATUS_RX_OVERRUN) {
                Cp_DecodeResult r;
                Increment(&g_motor_app.rx_count); g_motor_app.last_command=frame;
                r=CanProtocol_Decode(frame.id,false,false,frame.dlc,frame.data);
                if (result==CAN_STATUS_RX_OVERRUN) {
                    Increment(&g_motor_app.rx_overruns);
                    if (r.valid && r.command.opcode==CP_STOP) Stop(&r.command);
                    Trip(CP_FAULT_RX_OVERRUN);
                } else if (r.valid) Handle(&r.command);
                else {
                    Increment(&g_motor_app.protocol_errors);
                    if (r.respond) Reply(&r.command,r.result);
                }
            } else if (result==CAN_STATUS_INVALID_FRAME) Trip(CP_FAULT_RX_INVALID);
            else if (result!=CAN_STATUS_NO_DATA) Trip(CP_FAULT_CAN_API);
        }
    }
    /* RX STOP/cancellation precedes a pending backend START completion.
     * Backend/safety service continues even if the CAN adapter is faulted.
     */
    ServiceMotor();
    if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
        if ((uint32_t)(s.now-s.last_status)>=MOTOR_APP_STATUS_MS) s.status_due=true;
        if (g_motor_app.fault!=CP_FAULT_NONE && (uint32_t)(s.now-s.last_error)>=1000U) s.error_due=true;
        ServiceTx();
    }
    g_motor_app.now_ms=s.now; g_motor_app.last_lease_ms=s.lease_at;
    g_motor_app.lease_valid=LeaseFresh(); g_motor_app.time_valid=s.time_valid;
    g_motor_app.tx_pending=s.tx_pending; g_motor_app.operation_pending=s.operation.active;
    g_motor_app.ack_count=s.count;
    g_motor_app.safe_off_confirmed=Snapshot().safe_off_confirmed;
    SaveDiagnostics();
}
