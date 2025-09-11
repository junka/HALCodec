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
 * @file du_mcc_utils.h
 * @brief <b> Header file for du_mcc utility functions </b>
 *
 * contains macro definitions and function declarations for du_mcc
 * utility functions.
 */

#ifndef DU_MCC_UTILS_H_
#define DU_MCC_UTILS_H_

/*==================[inclusions]==============================================*/
#include <nvsciipc.h>

/*==================[macros]==================================================*/

#ifdef SEND_RCV_DEBUG
#define sendrcv_print(fmt, arg...) printf(fmt, ##arg)
#else
#define sendrcv_print(fmt, arg...)
#endif

/* Full ethernet packet size */
#define PACKET_SIZE_FULL               512U
/* Timeout period for response from MCU (10 seconds) */
#define RECEIVE_TIMEOUT_MS             10000
/* Size of response message from MCU */
#define AURIX_RESP_MSG_SIZE            100U

/* String size to store parsed lines from config file */
#define MAXBUF                         512
/* String size to store AURIX IP address */
#define MAX_PARAM_LEN                  20U
/* Block type Macros */
#define BLOCK_TYPE_RESPONSE            0xF0U
#define BLOCK_TYPE_ERROR               0xFFU

/* Response(RSP_BID) frame indexes*/
#define RSP_ID_IDX      0x00U
#define RSP_ERRC_IDX    0x03U
#define RSP_LEN1_IDX    0x04U
#define RSP_LEN0_IDX    0x05U
#define RSP_DATA_IDX    0x06U

#define TIMEOUT_WAIT_FOR_EVENT_US      1000000U // 1 sec
#define MAX_RETRY_COUNT                9

/*==================[type definitions]========================================*/

typedef struct _DU_MCC_Struct {
    NvSciEventNotifier *eventNotifier;
    NvSciEventLoopService *eventLoopService;
    NvSciIpcEndpoint hIpc;
} DU_MCC_Struct;

/*==================[static inline functions]================================**/

static inline void du_mcc_print_packet(
    const U8 *Pkt,
    U32 Pkt_size,
    bool IsPktSent)
{
#ifdef SEND_RCV_DEBUG
    U32 i;
    if (IsPktSent == true)
    {
        printf("sending packet, size %d\n", Pkt_size);
    }
    else
    {
        printf("received packet\n");
    }
    for (i = 0; i < Pkt_size; i++)
    {
        printf("%02x ", Pkt[i]);
    }
    printf("\n");
#else
    /* to avoid unused argument Misra error */
    (void)Pkt;
    (void)Pkt_size;
    (void)IsPktSent;
#endif
}

/**
 * Function to calculate checksum .
 *
 * This is a generic function to calculate checksum to be
 * added to the data.
 *
 * @param buffer: data buffer.
 * @param count: size of buffer array passed as input argument.
 * @return : calculated checksum value.
 */
static inline U8 calc_checksum(const U8 *buf, U32 count)
{
    U32 crc = 0U;
    U8 out = 0U;
    U32 i;

    for (i = 0U; i < count; i++)
    {
        crc = (crc ^ buf[i]);
    }

    out = (U8)(crc & 0xFFU);

    return out;
}

/*=======================[external functions]================================**/

#ifdef __cplusplus
extern "C" {
#endif

DU_MCC_Ret_Type DU_MCC_InitResources(
    char *chname,
    NvSciEventLoopService **ppEventLoopService,
    NvSciIpcEndpoint *phIpc,
    NvSciEventNotifier **ppEventNotifier);

void DU_MCC_ReleaseResources(
    NvSciEventLoopService *pEventLoopService,
    NvSciIpcEndpoint hIpc,
    NvSciEventNotifier *pEventNotifier);

DU_MCC_Ret_Type DU_MCC_SendRecvPacket(
    void *handle,
    const U8 * const send_msg,
    const U32 Pkt_size,
    U8 * const r_msg,
    const U16 r_size);

#ifdef __cplusplus
}
#endif

#endif /* DU_MCC_UTILS_H_ */

/*==================[end of file]=============================================*/
