#ifndef MOTOR_IF_H
#define MOTOR_IF_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { MOTOR_OK=0, MOTOR_PENDING, MOTOR_BUSY, MOTOR_ERROR, MOTOR_UNSUPPORTED } Motor_Result;
typedef struct {
    bool safe_off_confirmed;
    bool enabled;
    bool reverse;
    bool reverse_supported;
    bool fault_active;
    uint16_t requested_duty; /* Commanded permille, no measured feedback. */
    uint32_t completed_operation;
    Motor_Result completion_result;
} Motor_Snapshot;

/* Single foreground owner. All calls bounded, no heap, no retained caller
 * frame pointers. ForceSafeOff MUST override queued STARTs inside backend.
 * Phase 4A accepts mock interfaces only; physical adapter is unavailable.
 */
typedef struct {
    void *context;
    bool is_mock;
    Motor_Result (*start)(void *, bool reverse, uint32_t operation, uint32_t now_ms);
    Motor_Result (*set_duty)(void *, uint16_t permille);
    void (*force_safe_off)(void *, uint32_t now_ms);
    Motor_Result (*clear_fault)(void *);
    void (*service)(void *, uint32_t now_ms);
    Motor_Snapshot (*snapshot)(void *);
} Motor_If;
#endif
