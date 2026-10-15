#ifndef TEST_FLEXCAN_IP_H
#define TEST_FLEXCAN_IP_H
/* Host-only RTD test double surface, never on an embedded include path.
 * Production signatures are checked separately against real RTD headers.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef bool boolean;
#define TRUE true
#define FALSE false
#define STD_OFF 0
#define FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE STD_OFF
#define NULL_PTR NULL
#define INST_FLEXCAN_0 0U
#define FLEXCAN_MSG_ID_STD 0
#define FLEXCAN_LOOPBACK_MODE 1
#define FLEXCAN_PAYLOAD_SIZE_8 0
typedef enum {
    FLEXCAN_STATUS_SUCCESS, FLEXCAN_STATUS_ERROR, FLEXCAN_STATUS_BUSY,
    FLEXCAN_STATUS_TIMEOUT, FLEXCAN_STATUS_BUFF_OUT_OF_RANGE,
    FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS
} Flexcan_Ip_StatusType;
typedef struct {
    uint32 propSeg, phaseSeg1, phaseSeg2, preDivider, rJumpwidth;
} Flexcan_Ip_TimeSegmentType;
typedef struct {
    uint32 max_num_mb;
    int flexcanMode;
    boolean is_rx_fifo_needed, fd_enable, bitRateSwitch, is_pe_clock, enhCbtEnable;
    struct { int payloadBlock0; } payload;
    void (*Callback)(void);
    void (*ErrorCallback)(void);
    Flexcan_Ip_TimeSegmentType bitrate;
} Flexcan_Ip_ConfigType;
typedef struct { unsigned unused; } Flexcan_Ip_StateType;
typedef struct {
    int msg_id_type;
    uint32 data_length;
    boolean fd_enable;
    uint8 fd_padding;
    boolean enable_brs, is_remote, is_polling;
} Flexcan_Ip_DataInfoType;
typedef struct {
    uint32 cs, msgId;
    uint8 data[64], dataLen, id_hit;
    uint32 time_stamp;
} Flexcan_Ip_MsgBuffType;
extern const Flexcan_Ip_ConfigType FlexCAN_Config0;
extern Flexcan_Ip_StateType FlexCAN_State0;
Flexcan_Ip_StatusType FlexCAN_Ip_Init(uint8, Flexcan_Ip_StateType *, const Flexcan_Ip_ConfigType *);
Flexcan_Ip_StatusType FlexCAN_Ip_DisableInterrupts(uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_SetStartMode(uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigRxMb(uint8, uint8, const Flexcan_Ip_DataInfoType *, uint32);
Flexcan_Ip_StatusType FlexCAN_Ip_Receive(uint8, uint8, Flexcan_Ip_MsgBuffType *, boolean);
Flexcan_Ip_StatusType FlexCAN_Ip_Send(uint8, uint8, const Flexcan_Ip_DataInfoType *, uint32, const uint8 *);
void FlexCAN_Ip_MainFunctionWrite(uint8, uint8);
void FlexCAN_Ip_MainFunctionRead(uint8, uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_GetTransferStatus(uint8, uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_AbortTransfer(uint8, uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_SetStopMode(uint8);
Flexcan_Ip_StatusType FlexCAN_Ip_Deinit(uint8);
boolean FlexCAN_Ip_GetStopMode(uint8);
#endif
