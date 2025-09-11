/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file
 *
 * @brief Command and Control Client Library
 *
 * Control and interact using 'libnvducc'.
 */

#ifndef DUCC_H_
#define DUCC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ducommon.h"
#include "dutransport.h"

/*!
 * @defgroup du_cc_group Command and Control API
 *
 * This file declares APIs for the NVIDIA DRIVE&reg; Update library 'libnvducc.so'.
 *
 * @ingroup drive_update_top
 * @{
 */
/* ---------------------- Types --------------------------------------------- */
/*!
 * Defines the return codes of DRIVE Update C&C library function calls.
 */
typedef uint32_t DUCC_Error;
#define DUCC_Success                    (0x00000000U)
#define ERR_NotImplemented              (0x00010000U)
#define ERR_InvalidArgument             (0x00010001U)
#define ERR_NoPendingIMG                (0x00010002U)
#define ERR_PathNotExist                (0x00010003U)
#define ERR_OperationNotPermitted       (0x00010004U)
#define ERR_BufferTooSmall              (0x00010005U)
#define ERR_OperationRestrictedByConfig (0x00010006U)
#define ERR_Busy                        (0x00010007U)
#define ERR_UnableConnect               (0x00020000U)
#define ERR_ConnectionLost              (0x00020001U)
#define ERR_NoActiveConnection          (0x00020002U)
#define ERR_InvalidResponse             (0x00020003U)

/*!
 * Type of pending actions defined by DRIVE Update C&C interface.
 */
typedef uint32_t DUCC_PendingAction;
#define PA_NONE       (0U)
#define PA_RUNLEVEL   (1U)
#define PA_USERACTION (2U)

/*!
 * States defined by DRIVE Update C&C interface.
 */
typedef uint32_t DUCC_State;
/*! \brief DRIVE Update is dormant and not connected. */
#define STATE_DORMANT            (0U)
/*! \brief OTA is unable to connect to the cloud. */
#define STATE_NO_CONNECTIVITY    (1U)
/*! \brief OTA is connected to the cloud. */
#define STATE_UP_TO_DATE         (2U)
/*! \brief OTA is connected to the cloud. There is an update available. Awaiting higher RunLevel to proceed. */
#define STATE_UPDATE_AVAILABLE   (3U)
/*! \brief Update Installation is either in progress or paused awaiting user action or specified RunLevel. */
#define STATE_UPDATE_IN_PROGRESS (4U)
/*! \brief Last update failed and the cloud does not have any alternative updates to try or re-try. Vehicle may require service. */
#define STATE_UPDATE_FAILED      (5U)
/*! \brief Last update failed in a way which DOTA cannot recover. The system is now in an undetermined state, possibly bootable but should be assumed unsafe to use. */
#define STATE_FATAL_ERROR        (6U)
#define NUM_DUCC_STATES          (7U)

/*!
 * \brief Defines the locales used to get content.
 */
typedef uint32_t DUCC_Locale;
/*! \brief Defines the US locale. */
#define DUCC_LOC_US (0U)

/*! \brief Defines the image installation states. */
typedef uint32_t DUCC_Image;
/*! \brief Defines the currently installed image state. */
#define DUCC_IMG_CURRENT (0U)
/*! \brief Defines the image pending installation state. */
#define DUCC_IMG_PENDING (1U)

/*! \brief Indicates no progress or progress not defined. */
#define PR_NONE      (101U)
/*! \brief Indicates progress number when it reaches 100%. */
#define PR_MAX       (100U)
/*! \brief Defines the max length of the file path. */
#define FILEPATH_MAX (512U)

/*!
 * Default option for user actions, which will accept any terms or otherwise
 * continue the installation process whenever applicable.
 */
#define DUCC_USERACTION_DEFAULT_SELECTION (0)

/*!
 * Defines the DRIVE Update C&C handle.
 */
typedef struct DUCC *PDUCC;

/*!
 * Defines the DRIVE Update status structure returned from library function call.
 * It consists of 3 components: state, progress and pending action.
 */
typedef struct DUCC_Status
{
    /// Current state of DRIVE Update. It indicates operational state of Drive Update stack.
    DUCC_State         state;
    /// Current progress of DRIVE Update. It indicates progress %, or nothing in progress (when not set).
    uint32_t           progress;
    /// Pending action of DRIVE Update. If set and applicable, it indicates that the installation progress requires an action from vehicle software. The action may be raising the runlevel (allowing higher system impact) or it could be UserAction – a choice/selection which the end users needs to make to continue with the installation.
    DUCC_PendingAction pendingAction;
    /// Pending action argument.
    uint32_t           pendingActionArgs;
} DUCC_Status;

/*!
 * @brief Callback function when status changed.
 *
 * @param[in] pObj(void*)
 *          Pointer to the object registered during initialization.
 *
 * @param[in] pStatus(const_DUCC_Status*)
 *          Current DRIVE Update status.
 *
 * @return None
 */
typedef void (*DUCCCallback)(void *pObj,
                             const DUCC_Status *pStatus);

/*!
 * Defines the entry inside option buffer header.
 */
typedef struct optionEntry
{
    /// ID of userAction, use this to return user selection.
    uint32_t userActionID;
    /// Offset to start of content string.
    uint32_t contentOffset;
    /// Length of content string.
    uint32_t contentLen;
} optionEntry;

/*!
 * Buffer for storing options.
 *
 * The following describes the memory layout of the @c optionBuffer structure:
 * @code
 * +-------------------+
 * |    numEntries     |
 * +-------------------+
 * |                   |
 * |   OptionEntry[]   |
 * |                   |
 * +-------------------+
 * |                   |
 * |                   |
 * |      char[]       |
 * |                   |
 * |                   |
 * +-------------------+
 * |      Unused       |
 * +-------------------+
 * @endcode
 *
 * It contains header of each individual entry at top of the buffer, then area
 * of string which would be pointed by option entry header using offset bytes
 * Since option buffer is caller allocated, it may contains unused bytes at end
 * of the buffer. The structure layout would be initialized by library code.
 *
 * To obtain size needed for buffer, pass NULL to corresponding API call and a
 * minimal size would be filled in size parameter. The minimal size would also
 * be filled in size parameter if the buffer size is too small, under this case
 * ERR_BufferTooSmall would be returned.
 *
 * A null byte in string area does not mark the end of that string.
 */
typedef struct optionBuffer
{
    /// Number of entries in this buffer.
    uint32_t     numEntries;
    /// Pointer to first optionEntry.
    optionEntry *pEntry;
} optionBuffer;

/*!
 * Defines the entry inside version buffer header.
 */
typedef struct versionEntry
{
    /// Offset to start of name string.
    uint32_t nameOffset;
    /// Length of name string.
    uint32_t nameLen;
    /// Offset to start of version string.
    uint32_t versionOffset;
    /// Length of version string.
    uint32_t versionLen;
} versionEntry;

/*!
 * Refer to @ref optionBuffer struct memory layout.
 */
typedef struct versionBuffer
{
    /// Number of entries in this buffer.
    uint32_t      numEntries;
    /// Pointer to first versionEntry.
    versionEntry *pEntry;
} versionBuffer;

/*!
 * Defines the entry inside info buffer header.
 */
typedef struct infoEntry
{
    /// Offset to start of name string.
    uint32_t nameOffset;
    /// Length of name string.
    uint32_t nameLen;
    /// Offset to start of content string.
    uint32_t contentOffset;
    /// Length of content string.
    uint32_t contentLen;
} infoEntry;

/*!
 * Refer to @ref optionBuffer struct memory layout.
 */
typedef struct infoBuffer
{
    /// Number of entries in this buffer.
    uint32_t   numEntries;
    /// Pointer to first infoEntry.
    infoEntry *pEntry;
} infoBuffer;

/* -------------------- Function Declarations ------------------------------ */
/*!
 * @brief Initialize C&C connection to the DRIVE Update Master.
 *
 * This function should be only called once before closing the connection.
 * This function is not thread safe.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] trType(DUTR_TR_TYPE)
 *          Type of back-end transport protocol used.
 *
 * @param[in] seType(DUTR_SEC_TYPE)
 *          Type of security protocol.
 *
 * @param[in] pTrParam(const_PDUTR_TR_PARAM)
 *          Pointer to the argument of selected transport protocol. For instance,
 *          if @a trType is @ref DUTR_TR_TYPE_IVC, then it shall point to the structure
 *          @ref DUTR_IVC_PARAM.
 *
 * @param[in] pSeParam(const_PDUTR_SEC_PARAM)
 *          Pointer to the argument of selected security protocol. For instance,
 *          if @a seType is @ref DUTR_SEC_TYPE_TLS, then it shall point to the structure
 *          @ref DUTR_SEC_TLS_PARAM.
 *
 * @param[in,out] ppDucc(PDUCC*)
 *          Pointer to initialized DUCC handle.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_InvalidArgument
 *          NULL argument was passed.
 *          DU Tranasport failed to init the connection.
 *              - Connection details was not valid.
 *              - Another application already using the connection.
 *
 * @return @ref ERR_OperationNotPermitted
 *          DUCC already initialized.
 *
 * @return @ref ERR_UnableConnect
 *          Could not start listener thread.
 *
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges:
 *   - NVSCIIPC permissions
 *   - nvdriveupdate_ivc_2 IVC channel
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
DUCC_Error DUCC_Init
(
    DUTR_TR_TYPE            trType,
    DUTR_SEC_TYPE           seType,
    const PDUTR_TR_PARAM    pTrParam,
    const PDUTR_SEC_PARAM   pSeParam,
    PDUCC                  *ppDucc
);

/*!
 * @brief Return current C&C API version.
 *
 * Currently, this API will be returning version 1.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @param[in,out] pVersion(uint32_t*)
 *          Returned version number, this variable is caller allocated.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_InvalidArgument
 *          NULL argument was passed.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DUCC_Error DUCC_Get_API_Version
(
    PDUCC     pDuccInfo,
    uint32_t *pVersion
);

/*!
 * @brief Return current active run level of DRIVE Update.
 *
 * This may be different from most recently requested run level.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @param[in,out] pRunLevel(DU_RUN_LEVEL*)
 *          Returned run level, this variable is caller allocated.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_InvalidArgument
 *          NULL argument was passed.
 *
 * @return @ref ERR_NoActiveConnection
 *          DUCC is not initialized.
 *          Connection was aborted.
 *
 * @return @ref ERR_Busy
 *          DUCC is currently running other command.
 *
 * @return @ref ERR_BufferTooSmall
 *          Failed to allocate buffer for sending data.
 *          Failed to receive return data.
 *
 * @return @ref ERR_InvalidResponse
 *          Invalid Response returned from remote.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges:
 *   - NVSCIIPC permissions
 *   - nvdriveupdate_ivc_2 IVC channel
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DUCC_Error DUCC_Get_Current_RunLevel
(
    PDUCC          pDuccInfo,
    DU_RUN_LEVEL  *pRunLevel
);

/*!
 * @brief Request DRIVE Update to switch to another run level.
 *
 * This function is non-blocking. It doesn't ensure switch to requested run
 * level immediately.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @param[in] reqRunLevel(DU_RUN_LEVEL)
 *          Requested run level.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_InvalidArgument
 *          Runlevel passed was invalid.
 *
 * @return @ref ERR_NoActiveConnection
 *          DUCC is not initialized.
 *          Connection was aborted.
 *
 * @return @ref ERR_Busy
 *          DUCC is currently running other command.
 *
 * @return @ref ERR_BufferTooSmall
 *          Failed to allocate buffer for sending data.
 *          Failed to receive return data.
 *
 * @return @ref ERR_OperationNotPermitted
 *          Run level can't be set.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges:
 *   - NVSCIIPC permissions
 *   - nvdriveupdate_ivc_2 IVC channel
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DUCC_Error DUCC_Request_RunLevel
(
    PDUCC         pDuccInfo,
    DU_RUN_LEVEL  reqRunLevel
);

/*!
 * @brief Retrieve current DRIVE Update status.
 *
 * Notice if progress is returned as PR_NONE it means the progress is undefined.
 * Otherwise, it should be an unsigned int between 0 and PR_MAX.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @param[in,out] pStatus(DUCC_Status*)
 *          Current DRIVE Update status, this struct is caller allocated.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_InvalidArgument
 *          NULL argument was passed.
 *
 * @return @ref ERR_NoActiveConnection
 *          DUCC is not initialized.
 *          Connection was aborted.
 *
 * @return @ref ERR_Busy
 *          DUCC is currently running other command.
 *
 * @return @ref ERR_BufferTooSmall
 *          Failed to allocate buffer for sending data.
 *          Failed to receive return data.
 *
 * @return @ref ERR_InvalidResponse
 *          Invalid Response returned from remote.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges:
 *   - NVSCIIPC permissions
 *   - nvdriveupdate_ivc_2 IVC channel
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DUCC_Error DUCC_Get_Current_Status
(
    PDUCC        pDuccInfo,
    DUCC_Status *pStatus
);

/*!
 * @brief Retrieves all known version information about currently installed
 * image or the image pending installation.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @param[in] image(DUCC_Image)
 *          Specifies which version info to be retrieved.
 *
 * @param[in, out] pVersionBuf(versionBuffer*)
 *          Caller allocated buffer to hold retrieved version information. It
 *          would contain a series of versionEntry to get possible version
 *          name and its value.
 *          Can be NULL to get requested buffer size.
 *
 * @param[in, out] pVersionBufsize(uint32_t*)
 *          Size of passed in buffer. If @a pVersionBuf is passed as NULL or this
 *          number is too small, the minimal size of buffer needed is returned.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @return @ref ERR_BufferTooSmall
 *          @a pVersionBuf is passed as NULL or pVersionBufsize was too small.
 *
 * @return @ref ERR_InvalidArgument
 *          NULL argument was passed.
 *
 * @return @ref ERR_NotImplemented
 *          DUCC_IMG_PENDING was passed.
 *
 * @return @ref ERR_NoActiveConnection
 *          DUCC is not initialized.
 *          Connection was aborted.
 *
 * @return @ref ERR_Busy
 *          DUCC is currently running other command.
 *
 * @return @ref ERR_BufferTooSmall
 *          Failed to allocate buffer for sending data.
 *          Failed to receive return data.
 *          Result buffer was too small.
 *
 * @return @ref ERR_InvalidResponse
 *          Invalid Response returned from remote.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges:
 *   - NVSCIIPC permissions
 *   - nvdriveupdate_ivc_2 IVC channel
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DUCC_Error DUCC_Get_Versions
(
    PDUCC          pDuccInfo,
    DUCC_Image     image,
    versionBuffer *pVersionBuf,
    uint32_t      *pVersionBufsize
);

/*!
 * @brief Close an active C&C connection.
 *
 * @interface DU-Command_and_Control Interface Spec
 *
 * @param[in] pDuccInfo(PDUCC)
 *          Pointer to DUCC handle.
 *
 * @return @ref DUCC_Success
 *          Upon success.
 *
 * @pre
 *   DUCC_Init should already be called
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: Yes
 */
DUCC_Error DUCC_Close
(
    PDUCC pDuccInfo
);
/** @} */
#ifdef __cplusplus
}
#endif

#endif
