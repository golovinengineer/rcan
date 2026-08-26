#if defined(STM32G474xx) || defined(STM32G0B1xx)

#    include "bx_canfd.h"

static bool bx_canfd_set_filter(rcan* can);

static bool bx_canfd_set_timing(rcan* can, uint32_t bitrate);

static bool bx_canfd_set_data_timing(rcan* can, uint32_t data_bitrate);

static bool bx_canfd_make_can_tx_header(rcan_frame* frame, FDCAN_TxHeaderTypeDef* tx_header);

static bool bx_canfd_enable_rx_notification(rcan* can);
static bool bx_canfd_enable_tx_notification(rcan* can);
static bool bx_canfd_enable_err_notification(rcan* can);

bool bx_canfd_filter_preconfiguration(rcan* can, uint32_t* accepted_ids, uint32_t size)
{
    can->use_filter = true;
    return rcan_filter_calculate(accepted_ids, size, &can->filter);
}

bool bx_canfd_start(rcan* can, uint32_t channel, uint32_t bitrate)
{
    // TODO create function of convert all baudrate to standart.

    can->handle.Instance          = (FDCAN_GlobalTypeDef*) channel;
    can->handle.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    can->handle.Init.FrameFormat  = FDCAN_FRAME_CLASSIC;
    can->handle.Init.Mode         = FDCAN_MODE_NORMAL;

    can->handle.Init.AutoRetransmission = ENABLE;
    can->handle.Init.TransmitPause      = DISABLE;
    can->handle.Init.ProtocolException  = DISABLE;

    can->handle.Init.StdFiltersNbr   = 1;
    can->handle.Init.ExtFiltersNbr   = 1;
    can->handle.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if (!bx_canfd_set_timing(can, bitrate))
        return false;

    if (!bx_canfd_set_data_timing(can, 0))  // FIXME data_bitrate
        return false;

    if (HAL_FDCAN_Init(&can->handle) != HAL_OK)
        return false;

    if (can->use_filter)
    {
        if (!bx_canfd_set_filter(can))
            return false;
    }

    if (can->rx_notify_en)
    {
        if (!bx_canfd_enable_rx_notification(can))
        {
            return false;
        }
    }

    if (can->tx_notify_en)
    {
        if (!bx_canfd_enable_tx_notification(can))
        {
            return false;
        }
    }

    if (can->err_notify_en)
    {
        if (!bx_canfd_enable_err_notification(can))
        {
            return false;
        }
    }

    return HAL_FDCAN_Start(&can->handle) == HAL_OK;
}

bool bx_canfd_is_ok(rcan* can)
{
    if (HAL_FDCAN_GetError(&can->handle) != HAL_FDCAN_ERROR_NONE)
        return false;

    if (HAL_FDCAN_GetState(&can->handle) == HAL_FDCAN_STATE_RESET)
        return false;

    if (HAL_FDCAN_GetState(&can->handle) == HAL_FDCAN_STATE_ERROR)
        return false;

    if (__HAL_FDCAN_GET_FLAG(&can->handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST))
    {
        __HAL_FDCAN_CLEAR_FLAG(&can->handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
        return false;
    }

    FDCAN_ProtocolStatusTypeDef status;

    if (HAL_FDCAN_GetProtocolStatus(&can->handle, &status) != HAL_OK)
        return false;

    if (status.BusOff == 1 || status.ErrorPassive == 1)
    {
        __HAL_FDCAN_CLEAR_FLAG(&can->handle, FDCAN_FLAG_ERROR_PASSIVE);
        __HAL_FDCAN_CLEAR_FLAG(&can->handle, FDCAN_FLAG_BUS_OFF);
        return false;
    }

    if (status.LastErrorCode == FDCAN_PROTOCOL_ERROR_NO_CHANGE)
        return true;

    if (status.LastErrorCode != FDCAN_PROTOCOL_ERROR_NONE)
        return false;

    return true;
}

bool bx_canfd_stop(rcan* can)
{
    if (HAL_OK != HAL_FDCAN_AbortTxRequest(&can->handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2))
    {
        return false;
    }

    if (HAL_OK != HAL_FDCAN_Stop(&can->handle))
    {
        return false;
    }
    if (HAL_OK != HAL_FDCAN_DeInit(&can->handle))
    {
        return false;
    }
    return true;
}

bool bx_canfd_send(rcan* can, rcan_frame* frame)
{
    if (can == NULL || frame == NULL || frame->type == nonframe || frame->len > RCAN_MAX_FRAME_PAYLOAD_SIZE)
        return false;

    if (HAL_FDCAN_GetTxFifoFreeLevel(&can->handle) == 0)
        return false;

    FDCAN_TxHeaderTypeDef tx_header = {0};

    if (!bx_canfd_make_can_tx_header(frame, &tx_header))
        return false;

    return HAL_FDCAN_AddMessageToTxFifoQ(&can->handle, &tx_header, frame->payload) == HAL_OK;
}

bool bx_canfd_receive(rcan* can, rcan_frame* frame)
{
    if (can == NULL || frame == NULL)
        return false;

    uint32_t fifo = 0;
    if (HAL_FDCAN_GetRxFifoFillLevel(&can->handle, FDCAN_RX_FIFO0) != 0)
        fifo = FDCAN_RX_FIFO0;
    else if (HAL_FDCAN_GetRxFifoFillLevel(&can->handle, FDCAN_RX_FIFO1) != 0)
        fifo = FDCAN_RX_FIFO1;
    else
        return false;

    bool                  success   = false;
    FDCAN_RxHeaderTypeDef rx_header = {0};

    success = HAL_FDCAN_GetRxMessage(&can->handle, fifo, &rx_header, frame->payload) == HAL_OK;
    if (success)
    {
        frame->id  = rx_header.Identifier;
        frame->len = rx_header.DataLength;
        if (rx_header.IdType == FDCAN_EXTENDED_ID)
            frame->type = ext_id;
        else if (rx_header.IdType == FDCAN_STANDARD_ID)
            frame->type = std_id;

        frame->rtr = rx_header.RxFrameType == FDCAN_REMOTE_FRAME ? true : false;
    }
    return success;
}

static bool bx_canfd_set_filter(rcan* can)
{
    FDCAN_FilterTypeDef sFilterConfig = {0};

    if (can->filter.is_extended)
        sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    else
        sFilterConfig.IdType = FDCAN_STANDARD_ID;

    sFilterConfig.FilterIndex  = 0;
    sFilterConfig.FilterType   = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1    = can->filter.mask_filter.id;
    sFilterConfig.FilterID2    = can->filter.mask_filter.mask;

    if (HAL_FDCAN_ConfigFilter(&can->handle, &sFilterConfig) != HAL_OK)
        return false;

    if (HAL_FDCAN_ConfigGlobalFilter(&can->handle,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT,
                                     FDCAN_FILTER_REMOTE,
                                     FDCAN_FILTER_REMOTE) != HAL_OK)
        return false;

    return true;
}

static bool bx_canfd_set_timing(rcan* can, uint32_t bitrate)
{
    // TODO check source of system tick for can !!! if not PCLK1 set error or some other sheet
    // TODO add  uint32_t clock = HAL_RCC_GetPCLK1Freq();
    uint32_t clock = SystemCoreClock;
    // TODO add Divider + convert DIV to value real
    if (!rcan_calculate_timing(clock, bitrate, &can->timing))
        return false;

    can->handle.Init.NominalPrescaler     = can->timing.bit_rate_prescaler;
    can->handle.Init.NominalSyncJumpWidth = can->timing.max_resynchronization_jump_width;
    can->handle.Init.NominalTimeSeg1      = can->timing.bit_segment_1;
    can->handle.Init.NominalTimeSeg2      = can->timing.bit_segment_2;
    return true;
}

static bool bx_canfd_set_data_timing(rcan* can, uint32_t data_bitrate)
{
    // TODO in future add support can FD check recomended CAN open speeds
    can->handle.Init.DataPrescaler     = 1;
    can->handle.Init.DataSyncJumpWidth = 1;
    can->handle.Init.DataTimeSeg1      = 1;
    can->handle.Init.DataTimeSeg2      = 1;
    return true;
}

static bool bx_canfd_make_can_tx_header(rcan_frame* frame, FDCAN_TxHeaderTypeDef* tx_header)
{
    if (frame->type == std_id)
    {
        tx_header->IdType = FDCAN_STANDARD_ID;

        if (frame->id > RCAN_STD_ID_MAX)
            return false;
    }
    else if (frame->type == ext_id)
    {
        tx_header->IdType = FDCAN_EXTENDED_ID;
    }
    else
    {
        return false;
    }

    if (!frame->rtr)
    {
        tx_header->TxFrameType = FDCAN_DATA_FRAME;
        tx_header->DataLength  = frame->len;
    }
    else
    {
        tx_header->TxFrameType = FDCAN_REMOTE_FRAME;
        tx_header->DataLength  = 0;
    }

    tx_header->ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    tx_header->BitRateSwitch       = FDCAN_BRS_OFF;
    tx_header->FDFormat            = FDCAN_CLASSIC_CAN;
    tx_header->Identifier          = frame->id;
    return true;
}

static bool bx_canfd_enable_rx_notification(rcan* can)
{
    // Activate notifications for Rx FIFO 0
    return HAL_FDCAN_ActivateNotification(&can->handle, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) == HAL_OK;
}

static bool bx_canfd_enable_tx_notification(rcan* can)
{
    // Activate notifications for Tx operation is complete
    return HAL_FDCAN_ActivateNotification(&can->handle,
                                           FDCAN_IT_TX_COMPLETE,
                                           FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2) == HAL_OK;
}

static bool bx_canfd_enable_err_notification(rcan* can)
{
    // Activate notifications for FDCAN error list
    return HAL_FDCAN_ActivateNotification(&can->handle, FDCAN_IT_LIST_PROTOCOL_ERROR, 0) == HAL_OK;
}

#endif  // defined(STM32G474xx) || defined(STM32G0B1xx)
