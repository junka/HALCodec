/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file du_mcc_utils.cpp
 * @brief <b>Contains definitions of interfaces used for du_mcc
 * interface packet transmission between Tegra and Aurix</b>
 */

/*==================[inclusions]==============================================*/
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <fstream>
#include <du_mcc.h>
#include <du_mcc_utils.h>

#ifdef __QNX__
#include <sys/neutrino.h>

static struct nto_channel_config config = {
    .num_pulses = 3,
    .rearm_threshold = 0,
};
#endif /* __QNX__ */

DU_MCC_Ret_Type DU_MCC_InitResources(
    char *chname,
    NvSciEventLoopService **ppEventLoopService,
    NvSciIpcEndpoint *phIpc,
    NvSciEventNotifier **ppEventNotifier)
{
    NvSciError err = NvSciError_Success;
    DU_MCC_Ret_Type ret = DU_MCC_E_OK;
    struct NvSciIpcEndpointInfo info;
    void *os_config = NULL;

#ifdef __QNX__
    os_config = &config;
#endif

    err = NvSciEventLoopServiceCreateSafe(1, os_config, ppEventLoopService);
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to create event loop service. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    err = NvSciIpcInit();
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to initialize NvSciIpc. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    /* Opening NvSciIpc endpoint */
    err = NvSciIpcOpenEndpointWithEventService(chname, phIpc,
                                               &((*ppEventLoopService)->EventService));
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to open NvSciIpc endpoint. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    err = NvSciIpcGetEventNotifier(*phIpc, ppEventNotifier);
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to get event notifier. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    err = NvSciIpcGetEndpointInfo(*phIpc, &info);
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to get endpoint info. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    sendrcv_print("NvSciIpc Endpoint Info: \n");
    sendrcv_print("nframes: %u \n", info.nframes);
    sendrcv_print("frame_size: %u \n", info.frame_size);

    err = NvSciIpcResetEndpointSafe(*phIpc);
    if (err != NvSciError_Success) {
        ret = DU_MCC_E_NOK;
        printf("%s: Failed to reset endpoint. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

fail:
    return ret;
}

void DU_MCC_ReleaseResources(
    NvSciEventLoopService *pEventLoopService,
    NvSciIpcEndpoint hIpc,
    NvSciEventNotifier *pEventNotifier)
{
    pEventNotifier->Delete(pEventNotifier);

    (void)NvSciIpcCloseEndpointSafe(hIpc, false);

    NvSciIpcDeinit();

    pEventLoopService->EventService.Delete(&(pEventLoopService->EventService));
}

static DU_MCC_Ret_Type wait_event(NvSciEventLoopService *eventLoopService,
                       NvSciEventNotifier *eventNotifier,
                       NvSciIpcEndpoint hIpc,
                       int32_t value)
{
    NvSciError err = NvSciError_Success;
    uint32_t event = 0;
    uint32_t retry_count = MAX_RETRY_COUNT;

    while(retry_count > 0) {
        event = 0;
        err = NvSciIpcGetEventSafe(hIpc, &event);
        if (err != NvSciError_Success) {
            printf("Error in NvSciIpcGetEventSafe() ret: 0x%x\n", err);
            return DU_MCC_E_NOK;
        }

        if (event & value) {
            return DU_MCC_E_OK;
        }

        err = eventLoopService->WaitForEvent(eventNotifier, TIMEOUT_WAIT_FOR_EVENT_US);
        if(err == NvSciError_Timeout) {
            sendrcv_print("WaitForEvent timeout: 0x%x %d/%d\n", err,
                                                         (MAX_RETRY_COUNT-retry_count+1),
                                                         MAX_RETRY_COUNT);
            retry_count--;
        }
        else if (err != NvSciError_Success) {
            printf("WaitForEvent err: 0x%x\n", err);
            break;
        }
    }

    return DU_MCC_E_NOK;
}

/* Pre-requisite: Set the buf and buf_size fields with actuals. */
static DU_MCC_Ret_Type write_msg_ipc(NvSciIpcEndpoint hIpc, const U8* buf, U32 buf_size)
{
    uint32_t bytes;
    uint32_t retry_count = MAX_RETRY_COUNT;
    NvSciError err = NvSciError_NotInitialized;
    DU_MCC_Ret_Type ret = DU_MCC_E_OK;

    while (retry_count-- > 0) {
        err = NvSciIpcWriteSafe(hIpc, buf, buf_size, &bytes);

        if(err == NvSciError_Success) {
            break;
        } else {
            if (err == NvSciError_NotInitialized || err == NvSciError_BadParameter || err == NvSciError_NotSupported) {
                ret = DU_MCC_E_NOK;
                /* Need to break if it's a fatal error instead of looping */
                printf("%s: Failed to NvSciIpcWriteSafe. err is: 0x%x\n",
                        __func__, err);
                break;
            } else {
                ret = DU_MCC_E_NOK;
            }
        }
    }

    if (!(err == NvSciError_Success)) {
        /* Write failed */
        ret = DU_MCC_E_NOK;
        printf("%s: Packet write failed\n", __func__);
    }

    return ret;
}

static DU_MCC_Ret_Type read_msg_ipc(NvSciIpcEndpoint hIpc, U8* buf, U16 buf_size)
{
    uint32_t bytes;
    uint32_t retry_count = MAX_RETRY_COUNT;
    NvSciError err = NvSciError_NotInitialized;
    DU_MCC_Ret_Type ret = DU_MCC_E_OK;

    while (retry_count-- > 0) {
        err = NvSciIpcReadSafe(hIpc, buf, buf_size, &bytes);

        if(err == NvSciError_Success) {
            break;
        } else {
            /* Need to break if it's a fatal error instead of looping */
            if (err == NvSciError_NotInitialized || err == NvSciError_BadParameter || err == NvSciError_NotSupported) {
                printf("%s: Failed to NvSciIpcReadSafe. err is: 0x%x\n",
                        __func__, err);
                break;
            } else {
                return DU_MCC_E_NOK;
            }
        }
    }

    if (!(err == NvSciError_Success)) {
        /* Read failed */
        ret = DU_MCC_E_NOK;
        printf("%s: Packet read failed \n", __func__);
    }

    return ret;
}

static DU_MCC_Ret_Type ChkRespDataValidity(
    const U8 (&recv_msg)[AURIX_RESP_MSG_SIZE],
    U8 * const r_msg,
    const U16 r_size)
{
    U8 error_code = recv_msg[RSP_ERRC_IDX];
    DU_MCC_Ret_Type ret = error_code;

    /* If valid response size and buffer is passed */
    if ((nullptr != r_msg) && (0U < r_size))
    {
        U16 resp_size;
        /* extract response size from response packet */
        resp_size = static_cast<U16>(static_cast<U16>(recv_msg[RSP_LEN1_IDX]) << 8U);
        resp_size = resp_size | static_cast<U16>(recv_msg[RSP_LEN0_IDX]);

        /* Checking validity of checksum present in buffer after response data */
        const U8 chk_sum = calc_checksum(recv_msg, (RSP_DATA_IDX + static_cast<U32>(resp_size)));
        if (chk_sum == recv_msg[RSP_DATA_IDX + resp_size])
        {
            /* Check if response buffer size can accomodate
             * available response data.
             */
            if(r_size >= resp_size)
            {
                (void)memcpy(&r_msg[0], &recv_msg[RSP_DATA_IDX], static_cast<size_t>(resp_size));
                (void)r_msg;
            }
            else
            {
                (void)printf("%s(): Unexpected Response Length\n", __func__);
                (void)error_code;
                ret = DU_MCC_E_NOK;
            }
        }
        else
        {
            (void)printf("%s(): Invalid checksum received in response\n", __func__);
            (void)error_code;
            ret = DU_MCC_E_NOK;
        }
    }

    (void)error_code;
    return ret;
}

/*==================[external function definitions]===========================*/

extern "C" {

/**
 * Function to send and receive packets via IPC.
 *
 * This function sends block packets to MCU and waits on
 * response from MCU. Upon receiving the response, data from response
 * packet is updated to out param.
 *
 * @param handle: handle obtained on calling DU_MCC_InitResources
 * @param send_msg: data buffer to be sent.
 * @param Pkt_size: size of data to be sent.
 * @param r_msg: out param to store data received in response packet.
 * @param r_size: expected size of data to be received in response packet.
 * @return : DU_MCC_E_OK if successful, else appropriate error code.
 */
DU_MCC_Ret_Type DU_MCC_SendRecvPacket(
    void *handle,
    const U8 * const send_msg,
    const U32 Pkt_size,
    U8 * const r_msg,
    const U16 r_size)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_OK;
    DU_MCC_Struct *pHandle = (DU_MCC_Struct*)handle;

    ret = wait_event(pHandle->eventLoopService,
               pHandle->eventNotifier,
               pHandle->hIpc,
               NV_SCI_IPC_EVENT_WRITE);
    if (ret != DU_MCC_E_OK) {
        printf("%s: Failed to wait for IPC write event. ret is: 0x%x\n",
                __func__, ret);
        goto fail;
    }

    sendrcv_print("Attempting IPC Write : %s\t:: ", send_msg);
    for(uint32_t idx = 0; idx < Pkt_size; idx++) {
        sendrcv_print("0x%x ", send_msg[idx]);
    }
    sendrcv_print("\n");

    /* Write to IPC Endpoint */
    ret = write_msg_ipc(pHandle->hIpc, send_msg, Pkt_size);
    if (ret != DU_MCC_E_OK) {
        printf("%s: Failed to write string over ipc. ret is: 0x%x\n",
                __func__, ret);
        goto fail;
    }

    ret = wait_event(pHandle->eventLoopService,
               pHandle->eventNotifier,
               pHandle->hIpc,
               NV_SCI_IPC_EVENT_READ);
    if (ret != DU_MCC_E_OK) {
        printf("%s: Failed to wait for IPC read event. ret is: 0x%x\n",
                __func__, ret);
        goto fail;
    }


    U8 recv_msg[AURIX_RESP_MSG_SIZE];
    /* Read from IPC Endpoint */
    ret = read_msg_ipc(pHandle->hIpc, recv_msg, AURIX_RESP_MSG_SIZE);

    if(DU_MCC_E_OK == ret)
    {
        if(BLOCK_TYPE_RESPONSE == recv_msg[RSP_ID_IDX])
        {
            ret = ChkRespDataValidity(recv_msg, r_msg, r_size);
        }
        else if(BLOCK_TYPE_ERROR == recv_msg[RSP_ID_IDX])
        {
            (void)printf("%s(): Common interface not supported by Aurix\n", __func__);
            (void)ret;
            ret = DU_MCC_E_NOK;
        }
        else
        {
            (void)printf("%s(): Unsuccessful Ack Received 0x%x\n", __func__, recv_msg[RSP_ID_IDX]);
            (void)ret;
            ret = DU_MCC_E_NOK;
        }
    }

    if (ret != DU_MCC_E_OK) {
        printf("%s: Failed to read ipc rawbuffer. ret is: 0x%x\n",
                __func__, ret);
        goto fail;
    }
  
   if (r_msg != NULL) {
      sendrcv_print("%s() Received Message over IPC Endpoint: size: %d\n", __func__, r_size);
      for(uint32_t idx = 0; idx < r_size; idx++) {
          sendrcv_print("0x%x ", r_msg[idx]);
      }
      sendrcv_print("\n");
   }

    return DU_MCC_E_OK;

fail:
    return ret;
}

} /* extern "C" */

/*==================[end of file]=============================================*/
