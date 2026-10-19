#ifndef MOTOR_APP_H
#define MOTOR_APP_H
#include "can_protocol.h"
#include "motor_if.h"
#include "can_types.h"

#define MOTOR_APP_LEASE_MS 500U
#define MOTOR_APP_STATUS_MS 100U
#define MOTOR_APP_OPERATION_MS 100U /* Mock-only completion deadline. */
#define MOTOR_APP_ACK_CAPACITY 8U
#define MOTOR_APP_CACHE_CAPACITY 8U

typedef struct {
    Cp_State state;
    Cp_Fault fault;
    Cp_Fault first_fault;
    uint32_t fault_mask;
    uint32_t first_fault_ms;
    uint32_t now_ms;
    uint32_t last_lease_ms;
    uint32_t last_operation;
    uint16_t requested_duty;
    uint16_t max_duty;
    bool configured;
    bool safe_off_confirmed;
    bool reverse;
    bool mock_backend;
    bool time_valid;
    bool lease_valid;
    bool tx_pending;
    bool operation_pending;
    uint32_t tx_accepted;
    uint32_t tx_completed;
    uint32_t rx_count;
    uint32_t protocol_errors;
    uint32_t duplicates;
    uint32_t sequence_rejections;
    uint32_t rx_overruns;
    uint32_t queue_overflows;
    uint32_t ack_count;
    uint32_t ack_high_water;
    Can_Status last_can_status;
    Can_Frame last_command;
    Can_Frame pending_tx;
    Can_Diagnostics diagnostics;
    Can_Diagnostics first_fault_diagnostics;
} MotorApp_Debug;

extern volatile MotorApp_Debug g_motor_app;
/* Caller initializes/starts CAN wrapper once, and initializes a mock backend.
 * Interface is copied; context/storage must outlive the application.
 * No physical backend is accepted in Phase 4A.
 */
bool MotorApp_Init(const Motor_If *motor, uint32_t now_ms);
/* One bounded foreground step: one RX, one TX poll/submit, backend service.
 * now_ms is monotonic modulo 2^32; caller must service within <2^31 ms.
 */
void MotorApp_Process(uint32_t now_ms);
void MotorApp_TimeFault(void);
#endif
