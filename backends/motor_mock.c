#include "motor_mock.h"
#include <string.h>

void MotorMock_Init(MotorMock *m, bool reverse_supported)
{
    memset(m,0,sizeof *m);
    m->state.safe_off_confirmed=true;
    m->state.reverse_supported=reverse_supported;
    m->start_delay_ms=10U; m->stop_delay_ms=1U;
}
static Motor_Result Start(void *ctx, bool reverse, uint32_t op, uint32_t now)
{
    MotorMock *m=ctx;
    if (m->state.fault_active) return MOTOR_ERROR;
    if (reverse && !m->state.reverse_supported) return MOTOR_UNSUPPORTED;
    if (!m->state.safe_off_confirmed || m->start_pending || m->stop_pending) return MOTOR_BUSY;
    m->state.safe_off_confirmed=false; m->state.reverse=reverse;
    m->state.requested_duty=0U; m->start_pending=true;
    m->operation=op; m->started_at=now; ++m->start_requests;
    return MOTOR_PENDING;
}
static Motor_Result Duty(void *ctx, uint16_t duty)
{
    MotorMock *m=ctx;
    if (m->state.fault_active || !m->state.enabled || duty>1000U) return MOTOR_ERROR;
    m->state.requested_duty=duty; return MOTOR_OK;
}
static void Off(void *ctx, uint32_t now)
{
    MotorMock *m=ctx;
    bool was_active=m->state.enabled || m->start_pending;
    ++m->safe_off_requests;
    m->state.enabled=false; m->state.requested_duty=0U;
    m->start_pending=false; m->operation=0U; /* Cancel before any late service. */
    if (was_active) {
        m->state.safe_off_confirmed=false; m->stop_pending=true; m->stopped_at=now;
    }
}
static Motor_Result Clear(void *ctx)
{
    MotorMock *m=ctx;
    return (!m->state.fault_active && m->state.safe_off_confirmed) ? MOTOR_OK : MOTOR_ERROR;
}
static void Service(void *ctx, uint32_t now)
{
    MotorMock *m=ctx;
    if (m->state.fault_active) Off(ctx,now);
    if (m->stop_pending && (uint32_t)(now-m->stopped_at)>=m->stop_delay_ms) {
        m->stop_pending=false; m->state.safe_off_confirmed=true;
    }
    if (m->start_pending && !m->never_complete_start &&
        (uint32_t)(now-m->started_at)>=m->start_delay_ms) {
        m->start_pending=false; m->state.enabled=true;
        m->state.completed_operation=m->operation; m->state.completion_result=MOTOR_OK;
    }
}
static Motor_Snapshot Snapshot(void *ctx) { return ((MotorMock *)ctx)->state; }
Motor_If MotorMock_Interface(MotorMock *m)
{
    Motor_If result={m,true,Start,Duty,Off,Clear,Service,Snapshot};
    return result;
}
void MotorMock_SetFaultInput(MotorMock *m, bool active) { m->state.fault_active=active; }
