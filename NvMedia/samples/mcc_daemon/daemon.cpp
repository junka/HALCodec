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
#include <unistd.h>
#include <pthread.h>

#include "ipc.h"
#include "nvsocket.h"

/* Max Size of actual messages to be transferred */
#define DEFAULT_BUF_SIZE 512

/* Default Timeout for wait event (IPC) and Receive Msg (NvSocket) */
#define DEFAULT_TIMEOUT_US 1000000

/* Default Number of Retry for Init time (NvSocket) */
#define DEFAULT_INIT_NUM_RETRY 120

/* Default Number of Retry for wait event (IPC) and Receive Msg (NvSocket) */
#define DEFAULT_NUM_RETRY 7

#define TACP_CFG_FILENAME "/etc/tacp/tacp.cfg"
/* String size to store parsed lines from config file */
#define MAXBUF                         512
/* String size to store AURIX IP address */
#define MAX_PARAM_LEN                  20U
/* Maximum value for Aurix port */
#define MAX_AURIX_PORT                 65535

typedef struct tacp_config {
    char aurix_ip_addr[MAX_PARAM_LEN];
    int aurix_server_port;
    int aurix_bootchain_port;
    char client_ip_addr[MAX_PARAM_LEN];
} tacp_config_t;

/* Application Context Data Type is used to encapsulate
 * the IPC Context, NvSocket Context and tacp config parameters
 */
typedef struct AppCtx {
    ipc_context_t           ipc_ctx;
    nvsocket_context_t      nvsocket_ctx;
    tacp_config_t           tacp_cfg;

    int                     num_ports;
    int64_t                 timeout_us;
    int                     num_retry;
    int                     buf_size; // buffer size for allocation
} AppCtx_t;

#ifdef __QNX__
/* Redirect stdout/stderr for mcc_daemon to a given path
 * path: New path to use for stdout/stderr
 */
static MccDaemonStatus_t mcc_daemon_set_logfile(const char *path)
{
	FILE *pfile = NULL;
	int ret;

	if (NULL == path) {
		fprintf(stderr, "[ERROR] %s(): <path> required\n", __func__);
		return MccDaemonStatusError;
	}

	pfile = fopen(path, "w");
	if (NULL == pfile) {
		fprintf(stderr, "[ERROR] %s(): fopen() : %s\n", __func__, strerror(errno));
		return MccDaemonStatusError;
	}

	ret = dup2(fileno(pfile), STDOUT_FILENO);
	if (ret == -1) {
		fprintf(stderr, "[ERROR] %s(): dup2(STDOUT) : %s\n", __func__, strerror(errno));
		return MccDaemonStatusError;
	}

	ret = dup2(fileno(pfile), STDERR_FILENO);
	if (ret == -1) {
		fprintf(stderr, "[ERROR] %s(): dup2(STDERR) : %s\n", __func__, strerror(errno));
		return MccDaemonStatusError;
	}

    return MccDaemonStatusOK;
}
#endif

static int get_tacp_config_info(tacp_config_t *tacp_cfg)
{
    int ret = -1;

    /* read TACP cfg file */
    FILE *const file_ptr = fopen(TACP_CFG_FILENAME, "r");
    if (nullptr != file_ptr)
    {
        bool aurix_ip_found = false;
        bool aurix_server_port_found = false;
        bool aurix_bootchain_port_found = false;
        bool client_ip_found = false;
        long int lport = 0;
        char line[MAXBUF];
        while (nullptr != fgets(&line[0], MAXBUF, file_ptr))
        {
            /* Parse: AURIX_IP_ADDRESS=NN.NN.NN.NNN */
            if (0 == strncmp(&line[0U], "AURIX_IP_ADDRESS", 16U))
            {
                if (0 != strncmp(&line[17U], "", 1U))
                {
                    (void)memcpy(&tacp_cfg->aurix_ip_addr[0], &line[17U], MAX_PARAM_LEN);
                    aurix_ip_found = true;
                }
            }
            /* Parse: AURIX_SERVER_PORT=NNNN */
            else if (0 == strncmp(&line[0U], "AURIX_SERVER_PORT", 17U))
            {
                if (0 != strncmp(&line[18U], "", 1U))
                {
                    errno = 0;
                    lport = strtol(static_cast<const char *>(&line[18U]), NULL, 0);
                    /* strtol might set the errno which needs to be checked.*/
                    if ((errno == 0))
                    {
                        if ((MAX_AURIX_PORT >= lport) && (0 <= lport) )
                        {
                            tacp_cfg->aurix_server_port = static_cast<int>(lport);
                            aurix_server_port_found = true;
                        }
                        else
                        {
                            (void)printf("MCC_Daemon: AURIX_SERVER_PORT out of range..\n");
                        }
                    }
                }
            }
            /* Parse: AURIX_BOOTCHAIN_PORT=NNNN */
            else if (0 == strncmp(&line[0U], "AURIX_BOOTCHAIN_PORT", 20U))
            {
                if (0 != strncmp(&line[21U], "", 1U))
                {
                    errno = 0;
                    lport = strtol(static_cast<const char *>(&line[21U]), NULL, 0);
                    /* strtol might set the errno which needs to be checked.*/
                    if ((errno == 0))
                    {
                        if ((MAX_AURIX_PORT >= lport) && (0 <= lport) )
                        {
                            tacp_cfg->aurix_bootchain_port = static_cast<int>(lport);
                            aurix_bootchain_port_found = true;
                        }
                        else
                        {
                            (void)printf("MCC_Daemon: AURIX_BOOTCHAIN_PORT out of range..\n");
                        }
                    }
                }
            }
            /* Parse: CLIENT_IP_ADDRESS=NN.NN.NN.NNN */
            else if (0 == strncmp(&line[0U], "CLIENT_IP_ADDRESS", 17U))
            {
                if (0 != strncmp(&line[18U], "", 1U))
                {
                    (void)memcpy(&tacp_cfg->client_ip_addr[0], &line[18U], MAX_PARAM_LEN);
                    client_ip_found = true;
                }
            }
            else
            {
                /* continue file search */
            }
            errno = 0;
        }

        (void)fclose(file_ptr);

        ret = !(aurix_ip_found && aurix_server_port_found && aurix_bootchain_port_found && client_ip_found);
    }
    else
    {
        (void)printf("MCC_Daemon: unable to open cfg file\n");
    }

    return ret;
}

/* This function sets Default Values for IPC and NvSocket structures */
static int set_default_params(AppCtx_t *pCtx)
{
    int idx = 0;
    int ret = 0;

    /* Set the 3 NvSciIpc Channel Names */
    idx = 0;
    strncpy(pCtx->ipc_ctx.ep[idx].chname, IPC_CH_NAME_IST, (sizeof(pCtx->ipc_ctx.ep[idx].chname)-1));
    idx++;
    strncpy(pCtx->ipc_ctx.ep[idx].chname, IPC_CH_NAME_DU, (sizeof(pCtx->ipc_ctx.ep[idx].chname)-1));
    idx++;
    strncpy(pCtx->ipc_ctx.ep[idx].chname, IPC_CH_NAME_CIF, (sizeof(pCtx->ipc_ctx.ep[idx].chname)-1));
    idx++;
    pCtx->ipc_ctx.num_eps = idx;

    /* Set the DEFAULT BUF_SIZE for transfer between NvSciIPC and NvSocket */
    pCtx->buf_size = DEFAULT_BUF_SIZE;

    /* Set the DEFAULT TIMEOUT value to avoid indefinite waits in IPC and NvSocket Threads */
    pCtx->timeout_us = DEFAULT_TIMEOUT_US;
    /* Set the DEFAULT NUM RETRY after timeout, to avoid indefinite waits in IPC and NvSocket Threads */
    pCtx->num_retry = DEFAULT_NUM_RETRY;

    ret = get_tacp_config_info(&pCtx->tacp_cfg);
    if (ret != 0) {
        printf("MCC_Daemon: %s: Failed to get tacp config info. ret is: 0x%x\n",
                 __func__, ret);
        goto fail;
    }

    /* Set the 2 NvSocket Port Numbers */
    idx=0;
    pCtx->nvsocket_ctx.udp_ep[idx].portNum = pCtx->tacp_cfg.aurix_server_port;
    idx++;
    pCtx->nvsocket_ctx.udp_ep[idx].portNum = pCtx->tacp_cfg.aurix_bootchain_port;
    idx++;
    pCtx->num_ports = idx;

    return 0;

fail:
    return -1;
}

static MccDaemonStatus_t process_nvsocket_port(uint8_t *buf_nvsocket2client, ipc_context_t *ipc_ctx, int idx, udp_endpoint_t *udp_ep, int64_t timeout_us, int num_retry, uint32_t buf_size)
{
    MccDaemonStatus_t status;
    int ret = 0;
    int ctr_retry = 0;

    /* 1. Handle Receive from Socket */
    /* Read from NvSocket */
    status = wait_n_recv_nvsocket_msg(udp_ep, timeout_us, buf_nvsocket2client, buf_size);
    if (status == MccDaemonStatusOK) {
        debug_printf("MCC_Daemon: Received Message over NvSocket\n");
        /* Dump bytes data for debug */
        for (int k=0; k<16; k++) {
            debug_printf("%02x ", buf_nvsocket2client[k]);
        }
        debug_printf("\n");
        /* 2. Handle Write to IPC */
        ctr_retry = num_retry;
        status = MccDaemonStatusError;
        while (ctr_retry > 0) {
            status = wait_event(ipc_ctx, idx, NV_SCI_IPC_EVENT_WRITE, timeout_us);
            if (status == MccDaemonStatusOK) {
                break;
            } else if (status != MccDaemonStatusTimeout) {
                debug_printf("MCC_Daemon: %s: Failed to wait for event over ipc. ret is: 0x%x\n",
                         __func__, status);
                usleep(timeout_us);
            }
            ctr_retry--;
        }
        if (status != MccDaemonStatusOK && ctr_retry == 0) {
            printf("MCC_Daemon: %s: Failed to wait for event over ipc after num_retry(=%d) exhausted. ret is: 0x%x\n",
                    __func__, num_retry, status);
            goto fail;
        }
        debug_printf("MCC_Daemon: Sending Response over IPC Endpoint(idx=%d)\n", idx);
        for (int k=0; k<16; k++) {
            debug_printf("%02x ", buf_nvsocket2client[k]);
        }
        debug_printf("\n");
        /* Write to IPC Endpoint */
        ret = write_ipc_rawbuf(ipc_ctx, idx, buf_nvsocket2client, buf_size);
        if (ret != 0) {
            printf("MCC_Daemon: %s: Failed to write buffer over ipc. ret is: 0x%x\n",
                     __func__, ret);
            goto fail;
        }
    } else if (status != MccDaemonStatusTimeout) {
        printf("MCC_Daemon: %s: Failed to wait and receive nvsocket msg. ret is: 0x%x\n",
                 __func__, status);
        goto fail;
    }

fail:
    return status;
}

static void* ipc_func(void *arg)
{
    AppCtx_t *pCtx                       = (AppCtx_t *)arg;
    ipc_context_t *ipc_ctx               = NULL;
    int ret                              = 0;
    int i                                = 0;
    bool gotEventArr[MAX_NUM_CHANNELS]   = {false};
    MccDaemonStatus_t status             = MccDaemonStatusOK;
    uint8_t *buf_client2nvsocket         = NULL;
    uint8_t *buf_nvsocket2client         = NULL;
    int portIdx                          = 0;
    int ctr_retry                        = 0;

    if (pCtx == NULL) {
        printf("MCC_Daemon: %s: Invalid Params!\n", __func__);
        return NULL;
    }

    ipc_ctx = &pCtx->ipc_ctx;

    udp_endpoint_t *udp_ep_arr[MAX_NUM_PORTS];
    for (i=0; i<pCtx->num_ports; i++) {
        udp_ep_arr[i] = &pCtx->nvsocket_ctx.udp_ep[i];
    }

    buf_client2nvsocket = (uint8_t *)calloc(1, pCtx->buf_size);
    if (buf_client2nvsocket == NULL) {
        printf("MCC_Daemon: %s: Unable to alloc mem for buffer!\n", __func__);
        return NULL;
    }

    buf_nvsocket2client = (uint8_t *)calloc(1, pCtx->buf_size);
    if (buf_nvsocket2client == NULL) {
        printf("MCC_Daemon: %s: Unable to alloc mem for buffer!\n", __func__);
        free(buf_client2nvsocket);
        return NULL;
    }

    while (true) {
        /* 1. Handle Read from IPC */
        status = wait_event_multi_ep(ipc_ctx, NV_SCI_IPC_EVENT_READ, gotEventArr, pCtx->timeout_us);
        if (status == MccDaemonStatusOK) {
            for (i=0; i<ipc_ctx->num_eps; i++) {
                if (gotEventArr[i]) {
                    ret = read_ipc_rawbuf(ipc_ctx, i, buf_client2nvsocket, pCtx->buf_size);
                    if (ret != 0) {
                        printf("MCC_Daemon: %s: Failed to read ipc rawbuffer. ret is: 0x%x\n",
                                 __func__, ret);
                        usleep(pCtx->timeout_us);
                        continue; // Not exiting thread and Checking if any other ipc requests
                    }
                    debug_printf("MCC_Daemon: Received Message over IPC Endpoint(i=%d):\n", i);
                    /* 2. Handle Send to Socket */
                    if (i < pCtx->num_ports) {
                        portIdx = i;
                    } else {
                        portIdx = pCtx->num_ports-1;
                    }
                    ctr_retry = pCtx->num_retry;
                    status = MccDaemonStatusError;
                    while (ctr_retry > 0) {
                        status = send_nvsocket_msg(udp_ep_arr[portIdx], buf_client2nvsocket, pCtx->buf_size);
                        if (status == MccDaemonStatusOK) {
                            debug_printf("MCC_Daemon: Sent Message over NvSocket (IPC=%d, portIdx=%d)\n", i, portIdx);
                            break;
                        } else if (status != MccDaemonStatusTimeout) {
                            debug_printf("MCC_Daemon: %s: Failed to send nvsocket msg. ret is: 0x%x\n",
                                     __func__, status);
                            usleep(pCtx->timeout_us);
                        }
                        ctr_retry--;
                    }
                    if (status != MccDaemonStatusOK && ctr_retry == 0) {
                        printf("MCC_Daemon: %s: Failed to send nvsocket msg after num_retry(=%d) exhausted. ret is: 0x%x\n",
                                __func__, pCtx->num_retry, status);
                        continue; // Not exiting thread and Checking if any other ipc requests
                    }
                    if (!strcmp(ipc_ctx->ep[i].chname, IPC_CH_NAME_DU)
                     || !strcmp(ipc_ctx->ep[i].chname, IPC_CH_NAME_CIF)) {
                        ctr_retry = pCtx->num_retry;
                        status = MccDaemonStatusError;
                        while (ctr_retry > 0) {
                            status = process_nvsocket_port(buf_nvsocket2client,
                                                           ipc_ctx, i,
                                                           udp_ep_arr[portIdx],
                                                           pCtx->timeout_us,
                                                           pCtx->num_retry,
                                                           pCtx->buf_size);
                            if (status == MccDaemonStatusOK) {
                                break;
                            } else if (status != MccDaemonStatusTimeout) {
                                debug_printf("MCC_Daemon: %s: Error in process nvsocket port func. ret is: 0x%x\n",
                                         __func__, status);
                                usleep(pCtx->timeout_us);
                            }
                            ctr_retry--;
                        }
                        if (status != MccDaemonStatusOK && ctr_retry == 0) {
                            printf("MCC_Daemon: %s: Failed to process nvsocket port after num_retry(=%d) exhausted. ret is: 0x%x\n",
                                    __func__, pCtx->num_retry, status);
                            continue; // Not exiting thread and Checking if any other ipc requests
                        }
                    }
                }
            }
        } else if (status != MccDaemonStatusTimeout) {
            printf("MCC_Daemon: %s: Failed to wait for read on ipc. ret is: 0x%x\n",
                     __func__, status);
            usleep(pCtx->timeout_us);
            continue; // Not exiting thread and Checking if any other ipc requests
        }
    }

    free(buf_client2nvsocket);
    buf_client2nvsocket = NULL;
    free(buf_nvsocket2client);
    buf_nvsocket2client = NULL;
    return NULL;
}

static void* nvsocket_ist_func(void *arg)
{
    AppCtx_t *pCtx               = (AppCtx_t *)arg;
    int idx                      = 0;
    udp_endpoint_t *udp_ep       = NULL;
    ipc_context_t *ipc_ctx       = NULL;
    uint8_t *buf_nvsocket2client = NULL;
    MccDaemonStatus_t status     = MccDaemonStatusOK;

    if (pCtx == NULL) {
        printf("MCC_Daemon: %s: Invalid Params!\n", __func__);
        return NULL;
    }

    udp_ep  = &pCtx->nvsocket_ctx.udp_ep[idx];
    ipc_ctx = &pCtx->ipc_ctx;

    buf_nvsocket2client = (uint8_t *)calloc(1, pCtx->buf_size);
    if (buf_nvsocket2client == NULL) {
        printf("MCC_Daemon: %s: Unable to alloc mem for buffer!\n", __func__);
        free(buf_nvsocket2client);
        return NULL;
    }

    while (true) {
        status = process_nvsocket_port(buf_nvsocket2client,
                                       ipc_ctx, idx,
                                       udp_ep,
                                       pCtx->timeout_us,
                                       pCtx->num_retry,
                                       pCtx->buf_size);
        if (status != MccDaemonStatusOK) {
            if (status == MccDaemonStatusTimeout) {
                debug_printf("MCC_Daemon: %s: Timeout in process nvsocket port func. ret is: 0x%x\n",
                             __func__, status);
                continue;
            }
            debug_printf("MCC_Daemon: %s: Error in process nvsocket port func. ret is: 0x%x\n",
                         __func__, status);
            usleep(pCtx->timeout_us);
        }
    }

    free(buf_nvsocket2client);
    buf_nvsocket2client = NULL;
    return NULL;
}

int main(int argc, char** argv)
{
    AppCtx_t ctx;
    int ret = 0;
    MccDaemonStatus_t status = MccDaemonStatusOK;
    int i = 0;
    int last_port_idx = -1;
    int ctr_retry = 0;
    pthread_t ipcTid;
    pthread_t nvsocketTid[MAX_NUM_PORTS];

#ifdef __QNX__
    /* For QNX redirecting printfs/stdout/stderr to slog2 */
    mcc_daemon_set_logfile("/dev/slog2/stdout");
#endif

    ret = set_default_params(&ctx);
    if(ret)
    {
        printf("MCC_Daemon: %s: Failed to set default parameters. ret is: 0x%x\n",
                 __func__, ret);
        goto fail;
    }

    for (i=0; i<ctx.num_ports; i++) {
        last_port_idx = i;
        ctr_retry = DEFAULT_INIT_NUM_RETRY;
        status = MccDaemonStatusError;
        while (ctr_retry > 0) {
            status = init_nvsocket_resources(&ctx.tacp_cfg.aurix_ip_addr[0],
                                             &ctx.tacp_cfg.client_ip_addr[0],
                                             &ctx.nvsocket_ctx, i);
            if (status == MccDaemonStatusOK) {
                break;
            } else if (status != MccDaemonStatusTimeout) {
                debug_printf("MCC_Daemon: %s: Failed to init nvsocket resources (idx=%d). status is: %d\n",
                         __func__, i, status);
                usleep(ctx.timeout_us);
            }
            ctr_retry--;
        }
        if (status != MccDaemonStatusOK && ctr_retry == 0) {
            printf("MCC_Daemon: %s: Failed to init nvsocket resources after num_retry(=%d) exhausted. ret is: 0x%x\n",
                    __func__, DEFAULT_INIT_NUM_RETRY, status);
            goto release_nvsocket_res;
        }
    }

    ret = init_ipc_resources(&ctx.ipc_ctx);
    if (ret != 0) {
        printf("MCC_Daemon: %s: Failed to init ipc resources. ret is: 0x%x\n",
                 __func__, ret);
        goto release_nvsocket_res;
    }

    // Note: Assumes that first udp endpoint is of IST
    ret = pthread_create(&nvsocketTid[0], NULL, nvsocket_ist_func, (void *)&ctx);
    if (ret != 0) {
        printf("MCC_Daemon: %s: Failed to create NvSocket thread (0). ret is: 0x%x\n",
                 __func__, ret);
        goto release_nvsocket_res;
    }

    ret = pthread_create(&ipcTid, NULL, ipc_func, (void *)&ctx);
    if (ret != 0) {
        printf("MCC_Daemon: %s: Failed to create Ipc thread. ret is: 0x%x\n",
                 __func__, ret);
        goto release_nvsocket_res;
    }

    pthread_join(nvsocketTid[0], NULL);
    pthread_join(ipcTid, NULL);

    release_ipc_resources(&ctx.ipc_ctx);

    last_port_idx = ctx.num_ports - 1;
release_nvsocket_res:
    for (i=last_port_idx; i>=0; i--) {
        release_nvsocket_resources(&ctx.nvsocket_ctx, i);
    }

fail:
    return ret;
}
