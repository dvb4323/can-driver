#ifndef MOTOR_MOCK_H
#define MOTOR_MOCK_H
#include "motor_if.h"

/* Simulation controls are public for deterministic tests, not physical limits. */
typedef struct {
    Motor_Snapshot state;
    bool start_pending;
    bool stop_pending;
    bool never_complete_start;
    uint32_t operation;
    uint32_t started_at;
    uint32_t stopped_at;
    uint32_t start_delay_ms;
    uint32_t stop_delay_ms;
    uint32_t start_requests;
    uint32_t safe_off_requests;
} MotorMock;
void MotorMock_Init(MotorMock *mock, bool reverse_supported);
Motor_If MotorMock_Interface(MotorMock *mock);
void MotorMock_SetFaultInput(MotorMock *mock, bool active);
#endif
