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
 * @file [ du_mcc_lib.c ]
 * @brief <b> du_mcc library source file</b>
 *
 * Contains API definition of du_mcc library.
 */

/*==================[inclusions]==============================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <du_mcc.h>
#include <du_mcc_utils.h>

/*==================[macros]==================================================*/

/** interface IDs for TEGRA_CONTROL block
 */
#define TEGRA_REBOOT                    0x1U
#define MCU_RESET                       0x3U

/** interface IDs for BOOT_CHAIN block
 */
#define SET_DEFAULT_BOOT_CHAIN          0x1U
#define SET_NEXT_BOOT_CHAIN             0x2U
#define GET_DEFAULT_BOOT_CHAIN          0x3U
#define GET_ACTIVE_BOOT_CHAIN           0x4U

/** common interface block types
 */
#define TEGRA_CONTROL                   0x11U
#define BOOT_CHAIN                      0x14U

#define AURIX_RECOVERY_MODE            0x45U
#define CHECKSUM_SIZE                  1U

/** Maximum allowed delay (in seconds) value for tegra reboot/ aurix reset
 */
#define MAX_DELAY      10U

#define CIF_IPC_CHNAME    "nvmcc_cif_ipc_1"
#define DU_IPC_CHNAME     "nvmcc_du_ipc_1"

/***************************************************************/
/* Interface APIs for Library init and DeInit */
/***************************************************************/

/** Initialize DU MCC lib instance */
void* DU_MCC_Init(int argc, char *argv[])
{
    DU_MCC_Struct *pHandle = NULL;

    int ipc_ret = 0;

    pHandle = (DU_MCC_Struct *)calloc(1, sizeof(DU_MCC_Struct));
    if(pHandle == NULL) {
        printf("Failed to allocate memory for handle\n");
        goto fail;
    }

    char ipc_chname[NVSCIIPC_MAX_ENDPOINT_NAME];    /* endpoint name */
    int n = snprintf(ipc_chname, NVSCIIPC_MAX_ENDPOINT_NAME-1, "%s", DU_IPC_CHNAME);
    if ((argc == 1) && (argv != NULL)) {
        char *pType = argv[0];
        char *str1 = "COMMONIF_APP";
        if (0 == strncmp(pType, str1, strlen(str1))) {
            n = snprintf(ipc_chname, NVSCIIPC_MAX_ENDPOINT_NAME-1, "%s", CIF_IPC_CHNAME);
        }
    }

    if(n < 0) {
        printf("Failed to write ipc chname to local string\n");
        goto fail;
    }

    ipc_ret = DU_MCC_InitResources(ipc_chname,
                                 &(pHandle->eventLoopService),
                                 &(pHandle->hIpc),
                                 &(pHandle->eventNotifier));
    if (ipc_ret != DU_MCC_E_OK) {
        printf("Failed to initialize IPC resource\n");
        goto fail;
    }

    return (void*)pHandle;

fail:
    if(pHandle != NULL) {
        free(pHandle);
        pHandle = NULL;
    }

    return (void*)pHandle;
}

/** DeInit DU MCC lib instance */
DU_MCC_Ret_Type DU_MCC_DeInit(void* handle)
{
    DU_MCC_Struct *pHandle = (DU_MCC_Struct*)handle;

    if(handle == NULL) {
        printf("Failed to Deinit DU_MCC. Invalid handle\n");
        return DU_MCC_E_NOK;
    }

    DU_MCC_ReleaseResources(pHandle->eventLoopService,
                               pHandle->hIpc,
                               pHandle->eventNotifier);
    free(pHandle);
    return DU_MCC_E_OK;
}

/***************************************************************/
/* Interface APIs for reset and recovery */
/***************************************************************/

/**
 * DU_MCC_TegraReboot() is the interface for requesting reboot of Tegra
 *
 * This function requests MCU to Reboot Tegra.
 * An option is provided to hold Tegra in reset state.
 *
 * @param Tegra_id: Tegra ID for Tegra x1 or x2
 * @param timeout : Timeout value in Seconds, MCU shall wait before performing Tegra reset.
 * @return : success/failure in Tegra Reboot
 */
DU_MCC_Ret_Type DU_MCC_TegraReboot(
    void* handle,
    DU_MCC_Tegra_Device_ID Tegra_id,
    U8 timeout)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 8U;

    if ((DU_MCC_TEGRA_INVALID_ID > Tegra_id) &&
        (MAX_DELAY >= timeout))
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = TEGRA_CONTROL;
        send_msg[1U] = TEGRA_REBOOT;
        send_msg[2U] = 0U;
        send_msg[3U] = 3U;
        send_msg[4U] = (U8)Tegra_id;
        send_msg[5U] = (U8)0;
        send_msg[6U] = timeout;

        send_msg[7U]= calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size, NULL, 0U);
    }

    return ret;
}

/**
 * DU_MCC_SystemReset() is the interface to reset Aurix.
 *
 * This function requests MCU FW to reset the Aurix controller.
 * This will result in reset of all the Tegras and the peripherals
 * on the board
 *
 * @param timeDelay : Timeout value in Seconds, MCU shall wait before issuing Tegra reset.
 * @param st_Recovery : status of recovery mode for Tegra-x1.
 *                    DU_MCC_TEGRA_REC_DISABLE : no action
 *                    DU_MCC_TEGRA_REC_ENABLE : Set Tegra-x1 into recovery mode before MCU reset
 * @return : success/failure in Aurix reset / complete board reset
 */
DU_MCC_Ret_Type DU_MCC_SystemReset(
    void* handle,
    U8 timeDelay)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 7U;

    if (MAX_DELAY >= timeDelay)
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = TEGRA_CONTROL;
        send_msg[1U] = MCU_RESET;
        send_msg[2U] = 0U;
        send_msg[3U] = 2U;
        send_msg[4U] = timeDelay;
        send_msg[5U] = 0U;

        send_msg[6U]= calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size, NULL, 0U);
    }

    return ret;
}

/***************************************************************/
/* Interface APIs for GPIO based Boot-chain selection for Tegra A/B*/
/***************************************************************/

/**
 * DU_MCC_SetNext_BootChain() is the interface to request MCU
 * to switch the BootChain of Tegra to default or alternate.
 * (only for the next immediate reboot of Xavier).
 *
 * This function requests MCU to switch the BootChain of Tegra to default or
 * alternate boot-chain, by changing the state of the GPIO line.
 * This will not change the default BootChain in persistent memory.
 * This shall be followed by a MCU_TegraReboot request, to boot the
 * Tegra in the selected BootChain.
 *
 * @param Tegra_id : Id of Tegra x1 or x2 for which the BootChain
 * switching is requested
 * @param DU_MCC_BootChain_ID : A|B|C|D
 * @return : success/failure in set BootChain for next reboot
 */
DU_MCC_Ret_Type DU_MCC_SetNextBootChain(
    void* handle,
    DU_MCC_Tegra_Device_ID Tegra_id,
    DU_MCC_BootChain_ID Boot_id)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 7U;

    if ((DU_MCC_TEGRA_INVALID_ID > Tegra_id) &&
        (DU_MCC_BootChain_INVALID_ID > Boot_id))
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = BOOT_CHAIN;
        send_msg[1U] = SET_NEXT_BOOT_CHAIN;
        send_msg[2U] = 0U;
        send_msg[3U] = 2U;
        send_msg[4U] = (U8)Tegra_id;
        send_msg[5U] = (U8)Boot_id;

        send_msg[6U] = calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size, NULL, 0U);
    }

    return ret;
}

/**
 * DU_MCC_SetDefault_BootChain() is the interface to request MCU
 * to set the default BootChain of the Tegra.
 *
 * This function requests MCU to set the default BootChain selection
 * status in persistent memory to the supplied Boot_id values.
 *
 * This function will change status in persistent memory only,
 * does not set the GPIO line accordingly.
 *
 * MCU will evaluate boot chain configuration from persistent memory
 * to set the GPIO line during every boot-up of Tegra.
 *
 * @param Tegra_id : Id of Tegra x1 or x2
 * @param Boot_id : Id of default BootChain to be set
 * @return : success/failure in set default BootChain
 */
DU_MCC_Ret_Type DU_MCC_SetDefaultBootChain(
    void* handle,
    DU_MCC_Tegra_Device_ID Tegra_id,
    DU_MCC_BootChain_ID Boot_id)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 7U;

    if ((DU_MCC_TEGRA_INVALID_ID > Tegra_id) && (DU_MCC_BootChain_INVALID_ID > Boot_id))
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = BOOT_CHAIN;
        send_msg[1U] = SET_DEFAULT_BOOT_CHAIN;
        send_msg[2U] = 0U;
        send_msg[3U] = 2U;
        send_msg[4U] = (U8)Tegra_id;
        send_msg[5U] = (U8)Boot_id;

        send_msg[6U] = calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size, NULL, 0U);
    }

    return ret;
}

/**********************************************************************/
/* Interface APIs for getting Boot-chain information for Tegra x1/x2  */
/**********************************************************************/

/**
 * DU_MCC_GetDefault_BootChain() is the interface to request MCU
 * to get default boot chain configuration for Tegra.
 *
 * This function requests MCU to get default boot chain configuration for Tegra.
 * MCU gets the boot chain status by reading default boot chain configuration
 * in persistent memory for supplied Tegra Id.
 *
 * @param Tegra_id: Id of Tegra x1 or x2 for which the Default BootChain
 *                  configuration is requested.
 * @param DefaultBootChain (out): out param to store obtained default
 *        BootChain configuration A|B|C|D
 * @return : success/failure in getting default BootChain configuration.
 */
DU_MCC_Ret_Type DU_MCC_GetDefaultBootChain(
    void* handle,
    DU_MCC_Tegra_Device_ID Tegra_id,
    U8 *DefaultBootChain)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 6U;
    /* buffer to receive 2 bytes of response data,
     * resp_data[0]: Tegra Device Id,
     * resp_data[1]: Boot chain selection.
     */
    U8 resp_data[2U];

    if ((DU_MCC_TEGRA_INVALID_ID > Tegra_id) && (NULL != DefaultBootChain))
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = BOOT_CHAIN;
        send_msg[1U] = GET_DEFAULT_BOOT_CHAIN;
        send_msg[2U] = 0U;
        send_msg[3U] = 1U;
        send_msg[4U] = (U8)Tegra_id;

        send_msg[5U] = calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size,
                resp_data, 2U);
        if (DU_MCC_E_OK == ret)
        {
            /* Check if response is for correct Tegra device id */
            if(resp_data[0U] == (U8)Tegra_id)
            {
                *DefaultBootChain = resp_data[1U];
                /* Validate boot chain config received in response */
                if ((U8)DU_MCC_BootChain_INVALID_ID <= (*DefaultBootChain))
                {
                    ret = DU_MCC_E_NOK;
                }
            }
            else
            {
                ret = DU_MCC_E_NOK;
            }
        }
    }

    return ret;
}

/**
 * DU_MCC_GetActive_BootChain() is the interface to request MCU
 * to get active boot chain selection for Tegra.
 *
 * This function requests MCU to get active boot chain selection for Tegra.
 * MCU gets the boot chain selected while booting the Tegra.
 *
 * @param Tegra_id: Id of Tegra x1 or x2 for which the active BootChain
 *                  selection is requested.
 * @param ActiveBootChain (out): out param to store obtained active
 *        BootChain configuration. = A|B|C|D
 * @return : success/failure in getting active BootChain selection.
 */
DU_MCC_Ret_Type DU_MCC_GetActiveBootChain(
    void* handle,
    DU_MCC_Tegra_Device_ID Tegra_id,
    U8 *ActiveBootChain)
{
    DU_MCC_Ret_Type ret = DU_MCC_E_INVALID_PARAMS;
    U8 send_msg[PACKET_SIZE_FULL];
    const U32 pkt_size = 6U;
    /* buffer to receive 2 bytes of response data,
     * resp_data[0]: Tegra Device Id,
     * resp_data[1]: Boot chain selection.
     */
    U8 resp_data[2U];

    if ((DU_MCC_TEGRA_INVALID_ID > Tegra_id) && (NULL != ActiveBootChain))
    {
        (void)memset(&send_msg, 0x0, sizeof(send_msg));

        send_msg[0U] = BOOT_CHAIN;
        send_msg[1U] = GET_ACTIVE_BOOT_CHAIN;
        send_msg[2U] = 0U;
        send_msg[3U] = 1U;
        send_msg[4U] = (U8)Tegra_id;

        send_msg[5U] = calc_checksum(send_msg, (pkt_size - CHECKSUM_SIZE));

        ret = DU_MCC_SendRecvPacket(handle, send_msg, pkt_size,
                resp_data, 2U);
        if (DU_MCC_E_OK == ret)
        {
            /* Check if response is for correct Tegra device id */
            if(resp_data[0U] == (U8)Tegra_id)
            {
                *ActiveBootChain = resp_data[1U];
                /* Validate boot chain selection received in response */
                if ((U8)DU_MCC_BootChain_INVALID_ID <= (*ActiveBootChain))
                {
                    ret = DU_MCC_E_NOK;
                }
            }
            else
            {
                ret = DU_MCC_E_NOK;
            }
        }
    }

    return ret;
}

/*==================[end of file]=============================================*/
