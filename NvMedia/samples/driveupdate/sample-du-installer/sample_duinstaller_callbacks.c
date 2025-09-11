/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  dusample_installer_callbacks.c
 * @brief DU Sample Installer callback function implementations
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "dulink.h"
#include "dulog.h"
#include "utils.h"
#include "plugin_common/duinstaller_common.h"
#include "sample_duinstaller.h"
#include "sample_duinstaller_helpers.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdlib.h>
#include <ctype.h>

/* ------------------------ Local Helpers ----------------------------------- */
static DU_RCODE sampleVersionCmp
(
    const char *pV1,
    const char *pV2,
    char *pResult
)
{
    uint64_t v1Int;
    uint64_t v2Int;
    uint32_t i;
    char    *pEndPtr;

    if (strlen(pV1) == 0 || strlen(pV2) == 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < strlen(pV1); i++)
    {
        if (!isdigit(pV1[i]))
        {
            return SAMPLE_ERR_INVALID_ARGUMENT;
        }
    }

    for (i = 0; i < strlen(pV2); i++)
    {
        if (!isdigit(pV2[i]))
        {
            return SAMPLE_ERR_INVALID_ARGUMENT;
        }
    }

    errno = 0;
    v1Int = strtoul(pV1, &pEndPtr, 10);
    if (v1Int == UINT64_MAX || errno != 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    v2Int = strtoul(pV2, &pEndPtr, 10);
    if (v2Int == UINT64_MAX || errno != 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    if (v1Int > v2Int)
    {
        *pResult = '>';
    }
    else if (v1Int < v2Int)
    {
        *pResult = '<';
    }
    else
    {
        *pResult = '=';
    }

    return DU_OK;
}

/* ------------------------ Installer Required Callbacks -------------------- */
DU_RCODE requestedRLCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
)
{
    DU_RCODE          duRet = DU_OK;
    PSAMPLE_INSTALLER pInstaller = (PSAMPLE_INSTALLER) pCtx;
    DU_RUN_LEVEL      rl;
    char              tmpBuf[DU_RUNLEVEL_STR_MAX_SIZE] = {0};

    *pRetVal = 0;

    if (offset != 0)
    {
        DU_ERR("Invalid offset to requested_rl\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pInstaller->mutex);
    switch (operation)
    {
        case DULINK_CB_WRITE:
        {
            if (length > DU_RUNLEVEL_STR_MAX_SIZE)
            {
                DU_ERR("Invalid requested_rl\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            strncpy(tmpBuf, pBuf, length);
            rl = runlevelStrToUint(tmpBuf);
            if (rl == RL_INVALID)
            {
                DU_ERR("Invalid requested_rl %s\n", tmpBuf);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            duRet = setInstallerCurrentRl(&pInstaller->duInstaller, rl);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to update current rl");
                duRet = DULINK_CB_ERR_UNKNOWN;
            }
            duCondSignal(&pInstaller->cond);
            *pRetVal = length;
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = 0;
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pInstaller->mutex);

bailout:
    return duRet;
}

DU_RCODE cmdCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
)
{
    DU_RCODE          duRet = DU_OK;
    PSAMPLE_INSTALLER pInstaller = (PSAMPLE_INSTALLER) pCtx;

    *pRetVal = 0;

    if (offset != 0)
    {
        DU_ERR("Invalid offset to cmd\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pInstaller->mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            if (length < strlen(pInstaller->cmd))
            {
                DU_ERR("Not enough space to read cmd\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            strncpy((char *) pBuf, pInstaller->cmd, length);
            *pRetVal = strlen((char *) pBuf);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pInstaller->cmd);
            break;
        }
        case DULINK_CB_WRITE:
        {
            // Command cannot currently be cancelled until failure or completion
            if (pInstaller->bUpdateInProgress)
            {
                DU_ERR("Installer is busy\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            if (sizeof(pInstaller->cmd) <= length)
            {
                DU_ERR("Command exceeds max length\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            strncpy(pInstaller->cmd, (char *) pBuf, length);
            pInstaller->cmd[length] = '\0';

            duRet = parseCmd(pInstaller->cmd, &pInstaller->cmdId,
                pInstaller->filePathBuf, sizeof(pInstaller->filePathBuf),
                pInstaller->savenameBuf, sizeof(pInstaller->savenameBuf));
            if (duRet != DU_OK)
            {
                DU_ERR("Invalid cmd %s\n", pInstaller->cmd);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }

            // Wake up IDLE or PENDING_RL installer
            pInstaller->bUpdateInProgress = true;
            duCondSignal(&pInstaller->cond);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pInstaller->mutex);

bailout:
    return duRet;
}

DU_RCODE verInstallerCmpCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
)
{
    DU_RCODE          duRet = DU_OK;
    PSAMPLE_INSTALLER pSample = (PSAMPLE_INSTALLER) pCtx;
    char              inputVerBuf[MAX_INSTALLER_VER_LENGTH] = {0};
    char              result = '=';
    size_t            inputVerLen;
    size_t            len;

    duMutexLock(&pSample->mutex);
    *pRetVal = 0;

    switch (operation)
    {
        case DULINK_CB_READ:
        {
            if (strlen(pSample->installerCmpBuf) >= length)
            {
                DU_ERR("Not enough space to read version comparison\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            strcpy((char *) pBuf, pSample->installerCmpBuf);
            *pRetVal = strlen((char *) pBuf);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pSample->installerCmpBuf);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (length > MAX_INSTALLER_VER_LENGTH)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            (void) strncpy(inputVerBuf, (char *) pBuf, length);
            if (sampleVersionCmp(inputVerBuf, SAMPLE_VERSION, &result) != DU_OK)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                DU_ERR("Version compare failed, invalid input\n");
                break;
            }
            inputVerLen = strlen(inputVerBuf);
            // Concatenate v1 [<|>|=] v2
            len = strlcpy(pSample->installerCmpBuf, inputVerBuf,
                ((uint64_t) inputVerLen) + 1U);
            if (len > inputVerLen)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                DU_ERR("%s is truncated to %lu\n", pSample->installerCmpBuf,
                    len);
                break;
            }

            pSample->installerCmpBuf[inputVerLen] = result;
            (void) strcpy(pSample->installerCmpBuf + inputVerLen + 1,
                SAMPLE_VERSION);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_UNKNOWN;
            break;
        }
    }
    duMutexUnlock(&pSample->mutex);

    return duRet;
}
