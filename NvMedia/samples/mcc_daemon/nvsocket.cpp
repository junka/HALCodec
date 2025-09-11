/*
 * Copyright (c) 2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "nvsocket.h"

/* Full ethernet packet size */
#define PACKET_SIZE_FULL               512U
#define MAX_PARAM_LEN                  20U

/** Function to convert NvSocketWrapperLib error codes to
 *  MccDaemonStatus return types.
 */
static MccDaemonStatus_t GetStatusFromErrorCode(const NvSocketWrapper::ErrorCode code)
{
    /* Converts the NvSocketWrapper error code
     * to MccDaemonStatus types
     */
    MccDaemonStatus_t status;
    switch(code) {
        case  NvSocketWrapper::ErrorCode::E_SUCCESS:
            status = MccDaemonStatusOK;
            break;
        case  NvSocketWrapper::ErrorCode::E_ENDPOINT:
            (void)debug_printf("MCC_Daemon: Endpoint Connect Error!\n");
            status = MccDaemonStatusError;
            break;
        case  NvSocketWrapper::ErrorCode::E_TIMEOUT:
            (void)debug_printf("MCC_Daemon: Receive Timeout!\n");
            status = MccDaemonStatusTimeout;
            break;
        case  NvSocketWrapper::ErrorCode::E_SERVER_TEARDOWN:
            (void)debug_printf("MCC_Daemon: Server Teardown.\n");
            status = MccDaemonStatusError;
            break;
        default:
            status = MccDaemonStatusError;
            break;
    }
    return status;
}

MccDaemonStatus_t init_nvsocket_resources(char *aurix_ip_addr, char *client_ip_addr, nvsocket_context_t *nvsocket_ctx, int idx)
{
    NvSocketWrapper::ErrorCode code;
    MccDaemonStatus_t ret = MccDaemonStatusOK;
    udp_endpoint_t *udp_ep = &nvsocket_ctx->udp_ep[idx];
    int portNum = udp_ep->portNum;
    char ip_addr[MAX_PARAM_LEN] = "";

    udp_ep->endPoint =
        NvSocketWrapper::Endpoint::GetInstance(NvSocketWrapper::TPType::E_UDP);

    /* Initialize server config */
    if (idx >= 1) {
        strncpy(ip_addr, "0.0.0.0", (sizeof(ip_addr)-1));
    }
    else {
        strncpy(ip_addr, client_ip_addr, (sizeof(ip_addr)-1));
    }

    code = udp_ep->endPoint->InitServer(ip_addr, portNum);

    ret = GetStatusFromErrorCode(code);
    if (ret == MccDaemonStatusOK) {
        udp_ep->txRxMsg = NvSocketWrapper::Message::GetInstance(PACKET_SIZE_FULL);

        udp_ep->txRxMsg->SetRemoteIPAddr(aurix_ip_addr);
        udp_ep->txRxMsg->SetRemotePort(portNum);
        udp_ep->txRxMsg->SetLocalIPAddr(ip_addr);
        udp_ep->txRxMsg->SetLocalPort(portNum);
    } else {
        debug_printf("MCC_Daemon: InitServer() failed! clientIP=%s port=%d. ret is: 0x%x, code is: %d\n",
                     ip_addr, portNum, ret, (int)code);
    }

    return ret;
}

void release_nvsocket_resources(nvsocket_context_t *nvsocket_ctx, int idx)
{
    udp_endpoint_t *udp_ep = &nvsocket_ctx->udp_ep[idx];

    udp_ep->txRxMsg.reset();
    udp_ep->endPoint.reset();
}

MccDaemonStatus_t wait_n_recv_nvsocket_msg(udp_endpoint_t *udp_ep, int64_t timeout_us, uint8_t *buf, uint32_t buf_size)
{
    NvSocketWrapper::ErrorCode code;
    MccDaemonStatus_t ret = MccDaemonStatusOK;
    int timeout_ms = timeout_us / 1000;

    code = udp_ep->endPoint->RecvMsg(udp_ep->txRxMsg, timeout_ms);
    if (code == NvSocketWrapper::ErrorCode::E_SUCCESS) {
        (void)memcpy(buf,(udp_ep->txRxMsg->GetData())->data(), buf_size);
        return MccDaemonStatusOK;
    } else if (code == NvSocketWrapper::ErrorCode::E_TIMEOUT) {
        return MccDaemonStatusTimeout;
    } else {
        ret = GetStatusFromErrorCode(code);
        debug_printf("MCC_Daemon: NvSocket: RecvMsg Error. ret is: 0x%x, code is: %d\n",
                     ret, (int)code);
        return ret;
    }
}

MccDaemonStatus_t send_nvsocket_msg(udp_endpoint_t *udp_ep, uint8_t *buf, uint32_t buf_size)
{
    NvSocketWrapper::ErrorCode code;
    MccDaemonStatus_t ret = MccDaemonStatusOK;

    (void)memcpy((udp_ep->txRxMsg->GetData())->data(), buf, buf_size);
    code = udp_ep->endPoint->SendMsg(udp_ep->txRxMsg);
    if(NvSocketWrapper::ErrorCode::E_SUCCESS == code) {
        debug_printf("MCC_Daemon: NvSocket: Send Message Success!\n");
        return MccDaemonStatusOK;
    } else {
        ret = GetStatusFromErrorCode(code);
        debug_printf("MCC_Daemon: NvSocket: Send Message Failure! ret is: 0x%x, code is: %d\n",
                      ret, (int)code);
        return ret;
    }
}
