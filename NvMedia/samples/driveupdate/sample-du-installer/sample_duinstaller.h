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
 * @file  sample_duinstaller.h
 * @brief Data structures and definitions for Sample DU Installer
 *
 * This header is meant to act as a starting point for developers creating their
 * own installer components based on the Drive Update installer spec. It
 * provides a sample implementation of a DU Installer based on the spec.
 */

#ifndef SAMPLE_DUINSTALLER_H_
#define SAMPLE_DUINSTALLER_H_

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "plugin_common/duinstaller_common.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <pthread.h>

/* ------------------------ Installer Defines ------------------------------- */
#define SAMPLE_VERSION              "1"

// Error Codes
#define SAMPLE_OK                   (DU_OK)
#define SAMPLE_ERR_GENERIC          (1)
#define SAMPLE_ERR_INVALID_ARGUMENT (2)
#define SAMPLE_ERR_NOT_FOUND        (3)
#define SAMPLE_ERR_BUFFER_TOO_SMALL (4)
#define SAMPLE_ERR_NOT_SUPPORTED    (5)
#define SAMPLE_ERR_DULINK           (6)

#define SAMPLE_CMD_ID_DEPLOY        (0)
#define SAMPLE_CMD_ID_COMMIT        (1)
#define SAMPLE_CMD_ID_CLEAR_ERROR   (2)
#define NUM_SAMPLE_CMDS             (3)

#define INSTALL_PATH_MAX_LEN        (512)

/* ------------------------ Installer Data Structures ----------------------- */
typedef struct SAMPLE_INSTALLER SAMPLE_INSTALLER, *PSAMPLE_INSTALLER;

/// Used to define state transition functions for sample installer
typedef DU_RCODE (*InstallerState)(PSAMPLE_INSTALLER pInstaller);

struct SAMPLE_INSTALLER
{
    /// Contains data structures required by all installers
    DU_INSTALLER duInstaller;
    /// Marker to be set when a plugin action or installer reports a failure
    bool bUpdateFailed;
    /// Flag to signal that an update is currently in progress
    bool bUpdateInProgress;
    /// Flag to tell INSTALLING state whether update is already initialized
    bool bCmdInitialized;
    /// Mutex lock to prevent race condition when accessed by DU Link callbacks
    pthread_mutex_t mutex;
    /// Condition variable to wait on if all workers are blocked
    pthread_cond_t cond;
    /// State function to be next executed by sample installer
    InstallerState stateFunc;
    /// Current command being deployed, exposed via cmd node
    char cmd[DUINSTALLER_MAX_CMD_LEN];
    /// ID of cmd in installer cmds table
    uint8_t cmdId;
    /// DU Link path of file to read for installation
    char filePathBuf[DULINK_MAX_PATH];
    /// File name to use when saving file during installation
    char savenameBuf[DULINK_MAX_PATH];
    /// Full path of .staged file saved during deployment
    char stagedFilePathBuf[INSTALL_PATH_MAX_LEN];
    /// Installation directory path. Installed files will be saved here
    char installDir[INSTALL_PATH_MAX_LEN];
    /// Buffer to store installer.cmp result
    char installerCmpBuf[(MAX_INSTALLER_VER_LENGTH * 2) + 1];
};

/// Global sample installer object;
extern SAMPLE_INSTALLER sample;

/// Table to store possible commands for sample to execute
extern const char * SAMPLE_CMDS_TABLE[NUM_SAMPLE_CMDS];
/* ------------------------ Installer Required Callbacks -------------------- */
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
);

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
);

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
);

/* ------------------------ Public Functions -------------------------------- */
/*!
 * Initialize installer data structures and export DU Link Nodes
 *
 * @param[in] pInstaller
 *      Pointer to uninitialized sample installer data structure
 *
 * @return DU_OK
 *      Initialized successfully
 * @return SAMPLE_ERR_GENERIC
 *      Failed to initialize installer
 */
DU_RCODE sampleInstallerInit
(
    PSAMPLE_INSTALLER pInstaller
);

/*!
 * Main loop for execution of installer context
 *
 * @param[in] pInstaller
 *      Pointer to initialized sample installer data structure
 *
 * @return DU_OK
 *      Scheduler loop completed and exiting normally
 * @return SAMPLE_ERR_GENERIC
 *      Scheduler has encountered and and cannot recover
 */
DU_RCODE sampleInstallerRun
(
    PSAMPLE_INSTALLER pInstaller
);

#endif
