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
 * @file  sample_duinstaller.c
 * @brief State machine and functionality for sample DU Installer
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "dutransport.h"
#include "dulink.h"
#include "dulog.h"
#include "utils.h"
#include "sample_duinstaller.h"
#include "sample_duinstaller_helpers.h"
#include "duplugin.h"
#include "plugin_common/duinstaller_common.h"
/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <unistd.h>

/* ------------------------ Defines ----------------------------------------- */
#define PLUGIN_DULINK_NAME      "plugin"

/* ------------------------ Global Data Structures -------------------------- */
// List of commands supported by sample installer
const char * SAMPLE_CMDS_TABLE[NUM_SAMPLE_CMDS] =
{
    [SAMPLE_CMD_ID_DEPLOY]      = "deploy",
    [SAMPLE_CMD_ID_COMMIT]      = "commit",
    [SAMPLE_CMD_ID_CLEAR_ERROR] = "clear_error"
};

/* ------------------------ DU Link Callback Contexts ----------------------- */
// Contexts for read only string callbacks
static CTX_LOCK_STR ctxPluginType =
{
    .pStr = PLUGIN_TYPE_INSTALLER,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxVerInstaller =
{
    .pStr = SAMPLE_VERSION,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxState =
{
    .pStr = sample.duInstaller.stateStr,
    .pMutex = &sample.duInstaller.mutex
};

static CTX_LOCK_STR ctxProgress =
{
    .pStr = sample.duInstaller.progressStr,
    .pMutex = &sample.duInstaller.mutex
};

static CTX_LOCK_STR ctxLastDeployedMetadata =
{
    .pStr = sample.duInstaller.lastDeployedMetadata,
    .pMutex = &sample.duInstaller.mutex
};

static CTX_LOCK_STR ctxLastDeployedResult =
{
    .pStr = sample.duInstaller.lastDeployedResult,
    .pMutex = &sample.duInstaller.mutex
};

// Contexts for runlevels
static CTX_LOCK_RL ctxCurrentRl =
{
    .pRl = &sample.duInstaller.currentRl,
    .pMutex = &sample.duInstaller.mutex
};

static CTX_LOCK_RL ctxPendingRl =
{
    .pRl = &sample.duInstaller.pendingRl,
    .pMutex = &sample.duInstaller.mutex
};

// Contexts for .list nodes
static CTX_LIST_STR ctxStateList =
{
    .size = INSTALLER_STATE_MAX,
    .ppStrList = (char const **) INSTALLER_STATES_TABLE
};

static CTX_LIST_STR ctxCmdList =
{
    .size = NUM_SAMPLE_CMDS,
    .ppStrList = (char const **) SAMPLE_CMDS_TABLE
};

/* ------------------------ DU Link Node Definitions ------------------------ */
/// Table containing all directory nodes to export
#define INSTALLER_DULINK_DIRS_MAX (3U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_DIRS_TABLE[INSTALLER_DULINK_DIRS_MAX] =
{
    // last_deployed
    { .pPath = INSTALLER_DIR_LAST_DEPLOYED, .attr = READ_ONLY_DIR_ATTR },
    // ver
    { .pPath = INSTALLER_DIR_VER, .attr = READ_ONLY_DIR_ATTR },
    // ver/cmp
    { .pPath = INSTALLER_DIR_VER_CMP, .attr = READ_ONLY_DIR_ATTR }
};

/// Table containing all file nodes to export
#define INSTALLER_DULINK_FILES_MAX (14U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_FILES_TABLE[INSTALLER_DULINK_FILES_MAX] =
{
    // plugin-type
    { .pPath = INSTALLER_NODE_PLUGIN_TYPE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxPluginType },
    // requested_rl
    { .pPath = INSTALLER_NODE_REQUESTED_RL, .attr = MASTER_ONLY_RW_ATTRIBUTE,
      .cb = requestedRLCB, .pCtx = &sample },
    // current_rl
    { .pPath = INSTALLER_NODE_CURRENT_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxCurrentRl },
    // cmd
    { .pPath = INSTALLER_NODE_CMD, .attr = READ_WRITE_ATTRIBUTE, .cb = cmdCB,
      .pCtx = &sample },
    // cmd.list
    { .pPath = INSTALLER_NODE_CMD_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxCmdList },
    // state
    { .pPath = INSTALLER_NODE_STATE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxState },
    // state.list
    { .pPath = INSTALLER_NODE_STATE_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxStateList },
    // progress
    { .pPath = INSTALLER_NODE_PROGRESS, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxProgress },
    // pending_rl
    { .pPath = INSTALLER_NODE_PENDING_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxPendingRl },
    // persistent_ctx_path
    { .pPath = INSTALLER_NODE_PERSISTENT_CTX_PATH, .attr = READ_WRITE_ATTRIBUTE,
      .cb = installerPersistCtxCB, .pCtx = &sample.duInstaller },
    // ver/installer
    { .pPath = INSTALLER_NODE_VER_INSTALLER, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxVerInstaller },
    // ver/cmp/installer.cmp
    { .pPath = INSTALLER_NODE_VER_CMP_INSTALLER_CMP,
      .attr = READ_WRITE_ATTRIBUTE,
      .cb = verInstallerCmpCB, .pCtx = &sample },
    // last_deployed/metadata
    { .pPath = INSTALLER_NODE_LAST_DEPLOYED_MDATA, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxLastDeployedMetadata },
    // last_deployed/result
    { .pPath = INSTALLER_NODE_LAST_DEPLOYED_RESULT, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxLastDeployedResult },
};

/// Table containing all .notify nodes to export
#define INSTALLER_DULINK_NOTIFY_MAX (4U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_NOTIFY_TABLE[INSTALLER_DULINK_NOTIFY_MAX] =
{
    // current_rl.notify
    { .pPath = INSTALLER_NOTIFY_NODE_CURRENT_RL },
    // pending_rl.notify
    { .pPath = INSTALLER_NOTIFY_NODE_PENDING_RL },
    // state.notify
    { .pPath = INSTALLER_NOTIFY_NODE_STATE },
    // progress.notify
    { .pPath = INSTALLER_NOTIFY_NODE_PROGRESS }
};

/* ------------------------ Private Functions ------------------------------- */
// State transition function definition
static DU_RCODE sampleSetState
(PSAMPLE_INSTALLER pMyInstaller, INSTALLER_STATE state);

/*!
 * Run Installer INSTALLER_STATE_IDLE state
 *
 * INSTALLER_STATE_IDLE state is responsible for waiting until a command is sent to the
 * installer, at which point installer wakes up and moves to INSTALLING state
 *
 * @param[in] pMyInstaller
 *      Pointer to sample installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLING state
 * @return SAMPLE_ERR_GENERIC
 *      Transition to INSTALLING state failed
 */
static DU_RCODE installerStateIdle
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;

    DU_INFO("Installer IDLE state\n");

    // Wait for update to be available (something written to cmd DU Link Node)
    duMutexLock(&pMyInstaller->mutex);
    while (!pMyInstaller->bUpdateInProgress)
    {
        duCondWait(&pMyInstaller->cond, &pMyInstaller->mutex);
    }
    // Requires RL_BG_APPLY to initiate installing state
    if (pMyInstaller->duInstaller.currentRl >= RL_BG_APPLY)
    {
        duRet = sampleSetState(pMyInstaller, INSTALLER_STATE_INSTALLING);
    }
    else
    {
        duRet = setInstallerPendingRl(&pMyInstaller->duInstaller, RL_BG_APPLY);
        if (duRet != DU_OK)
        {
            DU_ERR("Error setting or notifying pending RL\n");
            duMutexUnlock(&pMyInstaller->mutex);
            goto bailout;
        }
        duRet = sampleSetState(pMyInstaller, INSTALLER_STATE_PENDING_RL);
    }
    duMutexUnlock(&pMyInstaller->mutex);

bailout:
    return duRet;
}

/*!
 * Run Installer ERROR state
 *
 * ERROR state is entered when a command fails asynchronously. ERROR state may
 * be cleared when clear_error command is sent to the intaller, which will
 * return the installer to IDLE state
 *
 * @param[in] pMyInstaller
 *      Pointer to sample installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLER_STATE_IDLE state
 * @return SAMPLE_ERR_INVALID_ARGUMENT
 *      Invalid cmd being executed, reenter error state
 */
static DU_RCODE installerStateError
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    int32_t fd;

    DU_INFO("Installer ERROR state\n");

    // Wait for update to be available (something written to cmd DU Link Node)
    duMutexLock(&pMyInstaller->mutex);
    while (!pMyInstaller->bUpdateInProgress)
    {
        DU_INFO("Error occurred: waiting for clear_error cmd\n");
        duCondWait(&pMyInstaller->cond, &pMyInstaller->mutex);
    }

    if (pMyInstaller->cmdId != SAMPLE_CMD_ID_CLEAR_ERROR)
    {
        DU_ERR("Error must be cleared before new command can be executed\n");
        duRet = SAMPLE_ERR_INVALID_ARGUMENT;
    }
    else
    {
        DU_INFO("Clearing ERROR state\n");
        // Open staged file and get file descriptor
        fd = open(pMyInstaller->stagedFilePathBuf, O_RDWR);
        // Check if file exists
        if (fd != -1)
        {
            // Delete staged file by file descriptor
            (void) unlinkat(fd, "", 0);
            // Close file descriptor
            (void) close(fd);
        }
        duRet = sampleSetState(pMyInstaller, INSTALLER_STATE_IDLE);
    }
    duMutexUnlock(&pMyInstaller->mutex);

    return duRet;
}

/*!
 * Run Installer PENDING_COMMIT state
 *
 * PENDING_COMMIT state is responsible for waiting until a commit command
 * is sent to the installer, at which point installer wakes up and moves
 * to INSTALLING state, where it can commit files previously staged by
 * the deploy command
 *
 * @param[in] pMyInstaller
 *      Pointer to sample installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLING or PENDING_RL state
 * @return SAMPLE_ERR_GENERIC
 *      Transition to another state failed
 */
static DU_RCODE installerStatePendingCommit
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;

    DU_INFO("Installer PENDING_COMMIT state\n");

    // Wait for update to be available (something written to cmd DU Link Node)
    duMutexLock(&pMyInstaller->mutex);
    while (!pMyInstaller->bUpdateInProgress)
    {
        duCondWait(&pMyInstaller->cond, &pMyInstaller->mutex);
    }

    switch (pMyInstaller->cmdId)
    {
        case SAMPLE_CMD_ID_COMMIT:
        {
            DU_INFO("Committing previous deployment\n");
            if (execCommitCmd(pMyInstaller) != DU_OK)
            {
                DU_ERR("Commit command failed\n");
                duRet = SAMPLE_ERR_GENERIC;
            }
            else
            {
                duRet = sampleSetState(pMyInstaller, INSTALLER_STATE_IDLE);
            }
            break;
        }
        default:
        {
            DU_ERR("Unexpected cmd, fail\n");
            duRet = SAMPLE_ERR_NOT_SUPPORTED;
            break;
        }
    }

    duMutexUnlock(&pMyInstaller->mutex);
    return duRet;
}

/*!
 * Run Installer PENDING_RL state
 *
 * PENDING_RL state is responsible for blocking until installer's runlevel is
 * set high enough to continue update installation
 *
 * @param[in] pMyInstaller
 *      Pointer to sample installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, RL set high enough to continue
 * @return SAMPLE_ERR_GENERIC
 *      Transition to INSTALLING state failed
 */
static DU_RCODE installerStatePendingRL
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    char     rlBuf[DU_RUNLEVEL_STR_MAX_SIZE];

    DU_INFO("Installer PENDING_RL state\n");

    duMutexLock(&pMyInstaller->mutex);
    while (pMyInstaller->duInstaller.pendingRl < RL_INVALID &&
           pMyInstaller->duInstaller.currentRl <
           pMyInstaller->duInstaller.pendingRl)
    {
        runlevelUintToStr(pMyInstaller->duInstaller.pendingRl,
            rlBuf, sizeof(rlBuf));
        DU_INFO("Waiting on runlevel %s\n", rlBuf);
        duCondWait(&pMyInstaller->cond, &pMyInstaller->mutex);
    }
    duRet = setInstallerPendingRl(&pMyInstaller->duInstaller, RL_INVALID);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to clear pending rl\n");
        goto bailout;
    }
    duRet = sampleSetState(pMyInstaller, INSTALLER_STATE_INSTALLING);

bailout:
    duMutexUnlock(&pMyInstaller->mutex);
    return duRet;
}

/*!
 * Run Installer INSTALLING state
 *
 * INSTALLING state is responsible for running an entire update until it either
 * completes or fails. INSTALLING state will transition to IDLE state upon
 * failure or successful completion of an update
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data structure
 *
 * @return DU_OK
 *      Update no longer installing, update completed or failed
 * @return SAMPLE_ERR_GENERIC
 *      Unrecoverable error has occurred during installation
 */
static DU_RCODE installerStateInstalling
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet;

    DU_INFO("Installer INSTALLING state\n");

    switch (pMyInstaller->cmdId)
    {
        case SAMPLE_CMD_ID_DEPLOY:
        {
            DU_INFO("Executing deploy cmd\n");
            if (execDeployCmd(pMyInstaller) != DU_OK)
            {
                DU_ERR("Deploy cmd failed\n");
                duRet = SAMPLE_ERR_GENERIC;
            }
            else
            {
                duMutexLock(&pMyInstaller->mutex);
                duRet = sampleSetState(pMyInstaller,
                    INSTALLER_STATE_PENDING_COMMIT);
                duMutexUnlock(&pMyInstaller->mutex);
            }
            break;
        }
        default:
        {
            DU_ERR("Unexpected command\n");
            duRet = SAMPLE_ERR_NOT_SUPPORTED;
            break;
        }
    }

    return duRet;
}

/*!
 * State transition function, responsible for handling atomic transitions
 * between installer states. Mutex protection should be used around this
 * function
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data structure
 * @param[in] state
 *      Installer state being transitioned to
 *
 * @return DU_OK
 *      Successfully transitioned to new state
 * @return DU_ERR_GENERIC
 *      Unexpected state or transition failed
 *
 */
static DU_RCODE sampleSetState
(
    PSAMPLE_INSTALLER pMyInstaller,
    INSTALLER_STATE   state
)
{
    DU_RCODE         duRet = DU_OK;
    INSTALLER_RESULT result = INSTALLER_RESULT_SUCCESS;

    switch (state)
    {
        case INSTALLER_STATE_IDLE:
        {
            if (pMyInstaller->bUpdateFailed)
            {
                DU_ERR("Installation Failed\n");
                result = INSTALLER_RESULT_FAILED;
            }

            if (setInstallerLastDeployedInfo(&pMyInstaller->duInstaller, result,
                pMyInstaller->filePathBuf, NULL) != DU_OK)
            {
                DU_WARN("Failed to set last deployed info\n");
            }

            memset(pMyInstaller->cmd, 0, sizeof(pMyInstaller->cmd));
            memset(pMyInstaller->filePathBuf, 0,
                sizeof(pMyInstaller->filePathBuf));
            memset(pMyInstaller->savenameBuf, 0,
                sizeof(pMyInstaller->savenameBuf));
            memset(pMyInstaller->stagedFilePathBuf, 0,
                sizeof(pMyInstaller->stagedFilePathBuf));
            memset(pMyInstaller->installDir, 0,
                sizeof(pMyInstaller->installDir));
            pMyInstaller->bUpdateFailed = false;
            pMyInstaller->bUpdateInProgress = false;
            pMyInstaller->stateFunc = installerStateIdle;
            break;
        }
        case INSTALLER_STATE_INSTALLING:
        {
            // Load JSON File and Execute
            pMyInstaller->stateFunc = installerStateInstalling;
            break;
        }
        case INSTALLER_STATE_PENDING_RL:
        {
            // Block until RL Has been correctly set
            pMyInstaller->stateFunc = installerStatePendingRL;
            break;
        }
        case INSTALLER_STATE_PENDING_COMMIT:
        {
            pMyInstaller->stateFunc = installerStatePendingCommit;
            pMyInstaller->bUpdateInProgress = false;
            break;
        }
        case INSTALLER_STATE_ERROR:
        {
            pMyInstaller->stateFunc = installerStateError;
            pMyInstaller->bUpdateInProgress = false;
            pMyInstaller->bUpdateFailed = true;
            break;
        }
        default:
        {
            DU_ERR("Unexpected state!\n");
            duRet = SAMPLE_ERR_GENERIC;
            goto bailout;
        }
    }
    if (setInstallerState(&pMyInstaller->duInstaller, state) != DU_OK)
    {
        DU_WARN("Failed to notify listeners on state change\n");
    }

bailout:
    return duRet;
}

/* ------------------------ Public Functions -------------------------------- */
DU_RCODE sampleInstallerInit
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet;

    DULINK_CONNECT_INFO duConnInfo = {0};

    initDuInstaller(&pMyInstaller->duInstaller);
    if (pthread_mutex_init(&pMyInstaller->mutex, NULL) != 0)
    {
        DU_CRIT("Mutex initialization failed\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    if (pthread_cond_init(&pMyInstaller->cond, NULL) != 0)
    {
        DU_CRIT("Condition var initialization failed\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    // Set installation directory
    if (getcwd(pMyInstaller->installDir, INSTALL_PATH_MAX_LEN) == NULL)
    {
        DU_CRIT("Coult not get local directory path\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    duRet = getPluginConnInfo(PLUGIN_DULINK_NAME, &duConnInfo);
    if (duRet == DUCOMMON_ERR_NOT_FOUND)
    {
        DU_WARN("Can't get plugin info using name %s\n", PLUGIN_DULINK_NAME);
        goto bailout;
    }
    else if (duRet != DU_OK)
    {
        DU_ERR("Failed to get connection info!\n");
        goto bailout;
    }
    else
    {
        DU_DBG("DU_OK on getPluginConnInfo\n");
    }

    duRet = dulinkInit(PLUGIN_DULINK_NAME, duConnInfo.remotePath);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to init DU Link\n");
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = exportDULinkNodes(INSTALLER_DULINK_DIRS_TABLE,
            INSTALLER_DULINK_DIRS_MAX,
            INSTALLER_DULINK_FILES_TABLE, INSTALLER_DULINK_FILES_MAX,
            INSTALLER_DULINK_NOTIFY_TABLE, INSTALLER_DULINK_NOTIFY_MAX);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to export all DU Link nodes\n");
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = dulinkOpen(duConnInfo.remotePath, duConnInfo.trType,
            duConnInfo.seType, (PDUTR_TR_PARAM) &duConnInfo.trParamBuf,
            (PDUTR_SEC_PARAM) &duConnInfo.secParamBuf,
            duConnInfo.connType, &duConnInfo.refId);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to init DU Link open connection %#x\n", duRet);
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = registerToMaster(DUMASTER_PATH);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to register to DU Master\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    duMutexLock(&pMyInstaller->mutex);
    sampleSetState(pMyInstaller, INSTALLER_STATE_IDLE);
    duMutexUnlock(&pMyInstaller->mutex);

bailout:
    return duRet;
}

DU_RCODE sampleInstallerRun
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE status;

    pMyInstaller->stateFunc = installerStateIdle;

    for (;;)
    {
        status = pMyInstaller->stateFunc(pMyInstaller);
        if (status != DU_OK)
        {
            duMutexLock(&pMyInstaller->mutex);
            sampleSetState(pMyInstaller, INSTALLER_STATE_ERROR);
            duMutexUnlock(&pMyInstaller->mutex);
        }
    }

    return DU_OK;
}
