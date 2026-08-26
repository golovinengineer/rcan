#pragma once

#if defined(STM32G474xx) || defined(STM32G0B1xx)

#    include "stdbool.h"
#    include "rcan_def.h"
#    include "rcan_timing.h"
#    include "rcan.h"
#    include "rcan_filter.h"

#    if defined(STM32G0B1xx)

#        include "stm32g0xx_hal.h"

#    endif

#    if defined(STM32G474xx)

#        include "stm32g4xx_hal.h"

#    endif
typedef enum
{
    CE_OK,
    CE_EPV,
    /*
     * Error Passive Flag
     * Set when the Error Passive limit has been reached
     * (Receive Error Counter or Transmit Error Counter greater than 127).
     * This Flag is cleared only by hardware.
     */
    CE_XMTFULL, /* Transmit buffer in CAN controller is full */
    CE_OVERRUN, /* CAN controller was read too late */
    CE_STATE_ERROR,
    CE_ERRI,
    CE_SOME_REC,
    CE_SOME_LEC,
    CE_SOME_TEC,
} can_errors_t;

struct can_iface;
typedef struct can_iface rcan;

typedef void (*bx_canfd_rx_cb_t)(rcan* can, const rcan_frame* frame);
typedef void (*bx_canfd_tx_cb_t)(rcan* can);
typedef void (*bx_canfd_err_cb_t)(rcan* can, uint32_t error_status);

struct can_iface
{
    FDCAN_HandleTypeDef handle;
    rcan_timing         timing;
    rcan_filter         filter;
    bool                use_filter;
    bool                rx_notify_en;
    bool                tx_notify_en;
    bool                err_notify_en;
    uint16_t            errors;

    bx_canfd_rx_cb_t  rx_cb;
    bx_canfd_tx_cb_t  tx_cb;
    bx_canfd_err_cb_t err_cb;
};

bool bx_canfd_filter_preconfiguration(rcan* can, uint32_t* accepted_ids, uint32_t size);

bool bx_canfd_start(rcan* can, uint32_t channel, uint32_t bitrate);

bool bx_canfd_is_ok(rcan* can);

bool bx_canfd_stop(rcan* can);

bool bx_canfd_send(rcan* can, rcan_frame* frame);

bool bx_canfd_receive(rcan* can, rcan_frame* frame);

#endif  // defined(STM32G474xx) || defined(STM32G0B1xx)
