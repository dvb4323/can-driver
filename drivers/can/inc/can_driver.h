#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include "can_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Singleton CAN0 adapter. Foreground, single caller, non-reentrant; no heap.
 * Caller must initialize clocks and grant exclusive CAN0 ownership first.
 * Never call alongside the original main.c CAN sequence or another CAN0 owner.
 * Lifecycle calls are synchronous RTD calls (dummy timeout counter currently).
 */
Can_Status CanDriver_Init(const Can_Config *config);
Can_Status CanDriver_Start(void);
/* Stop refuses an unconsumed TX; call PollTx until completion first.
 * Stop discards any unread RX frame and unregisters receive storage.
 */
Can_Status CanDriver_Stop(void);
/* Requires READY or FAULT. FAULT deinit explicitly discards outstanding work. */
Can_Status CanDriver_Deinit(void);
Can_DriverState CanDriver_GetState(void);

/* OK means accepted, not transmitted. Input is copied before return.
 * No second request is accepted until PollTx returns OK for this request.
 */
Can_Status CanDriver_Transmit(const Can_Frame *frame);
/* Services RTD TX completion; OK consumes completion, BUSY retains ownership,
 * NO_TRANSFER means no wrapper request is outstanding. No waiting/deadline.
 */
Can_Status CanDriver_PollTx(void);
/* Services one frame and immediately registers the next RTD receive.
 * Output valid only for OK or RX_OVERRUN; NO_DATA leaves output untouched.
 * Call regularly: one hardware RX MB, no software queue or lossless guarantee.
 */
Can_Status CanDriver_PollRx(Can_Frame *frame);

#ifdef __cplusplus
}
#endif

#endif
