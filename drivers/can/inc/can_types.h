#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <stdint.h>

/* Phase 1 supports standard 11-bit Classic CAN data frames only. */
typedef struct
{
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} Can_Frame;

typedef struct
{
    uint16_t rx_id; /* Exact standard identifier accepted by RX MB1. */
} Can_Config;

typedef enum
{
    CAN_STATUS_OK = 0,
    CAN_STATUS_BUSY,
    CAN_STATUS_NO_DATA,
    CAN_STATUS_NO_TRANSFER,
    CAN_STATUS_INVALID_ARGUMENT,
    CAN_STATUS_BAD_STATE,
    CAN_STATUS_CONFIG_ERROR,
    CAN_STATUS_TIMEOUT, /* RTD timeout; NOT a calibrated real-time deadline. */
    CAN_STATUS_HW_ERROR,
    CAN_STATUS_INVALID_FRAME,
    CAN_STATUS_RX_OVERRUN /* Output frame is valid, earlier traffic was lost. */
} Can_Status;

typedef enum
{
    CAN_DRIVER_UNINITIALIZED = 0,
    CAN_DRIVER_READY, /* Initialized, not running. */
    CAN_DRIVER_RUNNING,
    CAN_DRIVER_FAULT /* Only Deinit may touch the controller in this state. */
} Can_DriverState;

#endif
