/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  duplugin.h
 *
 * @brief Helper interfaces to program an NVIDIA DRIVE&reg; Update plugin.
 *
 */

#ifndef DUPLUGIN_H_
#define DUPLUGIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dulink.h"
#include <pthread.h>

/*!
 * @defgroup du_plugin_group DU-Plugin API
 *
 * Helper interfaces to program an NVIDIA DRIVE&reg; Update plugin.
 *
 * @ingroup drive_update_top
 * @{
 */

/* ------------------------ Defines ----------------------------------------- */

/*! \brief Defines the path to DU Master used for registration */
#define DUMASTER_PATH                 "/master"

/*!
 * Defines the name strings of all the valid DRIVE Update plugin types.
 */
/// installer type plugin name
#define PLUGIN_TYPE_INSTALLER         "installer"
/// controller type plugin name
#define PLUGIN_TYPE_CONTROLLER        "controller"
/// content provider type plugin name
#define PLUGIN_TYPE_CONTENT_PROVIDER  "content_provider"
/// transform type plugin name
#define PLUGIN_TYPE_FILE_TRANSFORM    "file_transform"
/// validator type plugin name
#define PLUGIN_TYPE_VALIDATOR         "validator"
/// metadata provider type plugin name
#define PLUGIN_TYPE_METADATA_PROVIDER "metadata_provider"
/// persistent context store type plugin name
#define PLUGIN_TYPE_PERS_CTX_STORE    "persistent_ctx_store"
/// debugger type plugin name
#define PLUGIN_TYPE_DEBUG             "debug"

/*!
 * Defines the default file names to be exported by all DRIVE Update plugins,
 * including name, parent_path and dulink_ver
 */

/*!
 * Defines the file names to be exported by all DRIVE Update plugins.
 */
/// plugin type file node
#define NODE_PLUGIN_TYPE              "plugin-type"
/// requested runlevel file node
#define NODE_REQUESTED_RL             "requested_rl"
/// current runlevel file node
#define NODE_CURRENT_RL               "current_rl"
/// persistent context path file node
#define NODE_PERSISTENT_CTX_PATH      "persistent_ctx_path"
/// pending runlevel file node
#define NODE_PENDING_RL               "pending_rl"
/// state file node
#define NODE_STATE                    "state"
/// state list file node
#define NODE_STATE_LIST               "state.list"

/*!
 * Defines the file names to be exported by certain DRIVE Update plugin types.
 */
/// progress file node
#define NODE_PROGRESS                 "progress"
/// command file node
#define NODE_CMD                      "cmd"
/// command list file node
#define NODE_CMD_LIST                 "cmd.list"
/// result file node
#define NODE_RESULT                   "result"

/*!
 * Defines the file names of notification nodes to be exported by all DRIVE
 * Update plugins.
 */
/// notify node of current runlevel
#define NOTIFY_NODE_CURRENT_RL        "current_rl.notify"
/// notify node of state
#define NOTIFY_NODE_STATE             "state.notify"
/// notify node of pending runlevel
#define NOTIFY_NODE_PENDING_RL        "pending_rl.notify"

/*!
 * Defines the file names of notification nodes to be exported by certain DRIVE
 * Update plugins.
 */
/// notify node of progress
#define NOTIFY_NODE_PROGRESS          "progress.notify"
/// notify node of result
#define NOTIFY_NODE_RESULT            "result.notify"

/*!
 * Defines the helper macros of various file attributes of DULINK_ATTR type.
 */
/*! \brief Defines the attribute of read only file. */
#define READ_ONLY_ATTR {                                        \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 1U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask = DULINK_PERMISSION_READ,                 \
        [0].denyMask = DULINK_PERMISSION_WRITE,                 \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},        \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of read only directory. */
#define READ_ONLY_DIR_ATTR {                                    \
    .type = DULINK_NODE_TYPE_DIRECTORY,                         \
    .numACL = 1U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask = DULINK_PERMISSION_READ,                 \
        [0].denyMask = DULINK_PERMISSION_WRITE,                 \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},        \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of read and write allowed file. */
#define READ_WRITE_ATTRIBUTE {                                  \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 1U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask = DULINK_PERMISSION_WRITE | DULINK_PERMISSION_READ, \
        [0].denyMask = DULINK_PERMISSION_NONE,                  \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},        \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of write only file. */
#define WRITE_ONLY_ATTRIBUTE {                                  \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 1U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask = DULINK_PERMISSION_WRITE,                \
        [0].denyMask = DULINK_PERMISSION_READ,                  \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},        \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of the file that can be read and written by
 * DU Master, but cannot be accessed by any other plguins. */
#define MASTER_ONLY_RW_ATTRIBUTE {                              \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/master",                             \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of the file that only DU Master can write but
 * not read, and any other plguins cannot access. */
#define MASTER_ONLY_WRITE_ATTRIBUTE {                           \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_READ,                \
        [1].srcElement = "/master",                             \
        [1].allowMask  = DULINK_PERMISSION_WRITE,               \
        [1].denyMask   = DULINK_PERMISSION_READ,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the attribute of the file that only DU CCheck can read and
 * write, and any other plguins cannot access.
 * Attribute type: "*[-,-], compatchecker[R,W]" */
#define COMPAT_ONLY_RW_ATTR {                                   \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/compatchecker",                      \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}


/*! \brief Defines the file attribute of master read only and others deny write.
 * Attribute type: "*[-,!W], master[R,!W]" */
#define MASTER_ONLY_READ_ATTR                                   \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_WRITE,               \
        [1].srcElement = "/master",                             \
        [1].allowMask  = DULINK_PERMISSION_READ,                \
        [1].denyMask   = DULINK_PERMISSION_WRITE,               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of all plugins can read,
 * DU Master can read and write.
 * Attribute type: "*[R,-], master[R,W]" */
#define ALL_RO_MASTER_RW_ATTR                                   \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_READ,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/master",                             \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of only CCheck can read.
 * Attribute type: "*[-,!W], compatchecker[R,!W]" */
#define CCHECK_ONLY_READ_ATTR                                   \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_WRITE,               \
        [1].srcElement = "/compatchecker",                      \
        [1].allowMask  = DULINK_PERMISSION_READ,                \
        [1].denyMask   = DULINK_PERMISSION_WRITE,               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of only DDU can write.
 * Attribute type: "*[!R,-], /gos-a/ddu[!R,W]" */
#define DDU_ONLY_WRITE_ATTR                                     \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_READ,                \
        [1].srcElement = "/gos-a/ddu",                          \
        [1].allowMask  = DULINK_PERMISSION_WRITE,               \
        [1].denyMask   = DULINK_PERMISSION_READ,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of only DU-CLIENT can read
 * and write, and any other plguins cannot access.
 * Attribute type: "*[-,-], /gos-a/du-client[R,W]" */
#define DUCLIENT_ONLY_RW_ATTR                                   \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_NONE,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/gos-a/du-client",                    \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of of all plugins can read,
 * DU-CLIENT can read and write.
 * Attribute type: "*[R,-], /gos-a/du-client[R,W]" */
#define ALL_RO_DUCLIENT_RW_ATTR                                 \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_READ,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/gos-a/du-client",                    \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the file attribute of of all plugins can read,
 * ctx_store can read and write.
 * Attribute type: "*[R,-], /ctx_store[R,W]" */
#define ALL_RO_CTX_STORE_RW_ATTR                                \
{                                                               \
    .type = DULINK_NODE_TYPE_FILE,                              \
    .numACL = 2U,                                               \
    .acl =                                                      \
    {                                                           \
        [0].srcElement = "/*",                                  \
        [0].allowMask  = DULINK_PERMISSION_READ,                \
        [0].denyMask   = DULINK_PERMISSION_NONE,                \
        [1].srcElement = "/ctx_store",                          \
        [1].allowMask  = DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE, \
        [1].denyMask   = DULINK_PERMISSION_NONE,                \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}},               \
        {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}  \
    }                                                           \
}

/*! \brief Defines the buffer size of the input parameters for DU Transport,
 * used by DULINK_CONNECT_INFO */
#define DUTR_PARAM_BUFLEN (1024)

/* ------------------------ Type Definitions -------------------------------- */
/*!
 * Defines all of the valid DRIVE Update plugin types.
 */
typedef enum DUPLUGIN_TYPE
{
    CONTROLLER = 0U,
    INSTALLER,
    CONTENT_PROVIDER,
    FILE_TRANSFORM,
    VALIDATOR,
    METADATA_PROVIDER,
    DUPLUGIN_TYPE_MAX
} DUPLUGIN_TYPE;

/*!
 * Role of DU LINK connection.
 */
typedef enum DULINK_CONNECTION_ROLE
{
    /// Router
    DULINK_CONNECT_ROUTER = 0,
    /// Plugin
    DULINK_CONNECT_PLUGIN
} DULINK_CONNECTION_ROLE;

/*!
 * Data structure to define DU Link node to be exported by a DRIVE Update plugin
 * via exportDULinkNodes.
 */
typedef struct DULINK_EXPORT_REQS
{
    /// DU Link path to be exported
    const char        *pPath;
    /// Callback function to export
    const DULINK_CB    cb;
    /// DU Link Attribute of node being exported
    DULINK_ATTR        attr;
    /// Context for to callback function
    void              *pCtx;
} DULINK_EXPORT_REQS, *PDULINK_EXPORT_REQS;

/*!
 * Data structure to enable access in a DU Link callback to a string requiring
 * thread access protection.
 */
typedef struct CTX_LOCK_STR
{
    /// String to be passed to callback context
    const char *pStr;
    /// Will be locked before reading from pStr
    pthread_mutex_t *pMutex;
} CTX_LOCK_STR, *PCTX_LOCK_STR;

/*!
 * Data structure to enable access in a DU Link callback to a uint8_t requiring
 * thread access protection.
 */
typedef struct CTX_LOCK_UINT8
{
    /// Integer to be passed to callback context
    uint8_t *pInt;
    /// Will be locked before reading from pStr
    pthread_mutex_t *pMutex;
} CTX_LOCK_UINT8, *PCTX_LOCK_UINT8;

/*!
 * Data structure to enable access in a DU Link callback to a DU_RUN_LEVEL
 * requiring thread access protection.
 */
typedef struct CTX_LOCK_RL
{
    /// Runlevel to be passed to callback context
    DU_RUN_LEVEL *pRl;
    /// Will be locked before reading from pStr
    pthread_mutex_t *pMutex;
} CTX_LOCK_RL, *PCTX_LOCK_RL;

/*!
 * Data structure to enable reading a list of values in a DU Link callback.
 */
typedef struct CTX_LIST_STR
{
    /// Number of items in the list
    const uint8_t size;
    /// Pointer to array of strings to iterate
    const char * const *ppStrList;
} CTX_LIST_STR, *PCTX_LIST_STR;

/*!
 * Data structure to hold information for single DU Link connection.
 */
typedef struct DULINK_CONNECT_INFO
{
    /// DU Transport type used to initialize the connection
    DUTR_TR_TYPE           trType;
    /// DU Transport security type used to initialize the connection
    DUTR_SEC_TYPE          seType;
    /// DU Link connection type
    DULINK_CONNECTION_TYPE connType;
    /// DU Link connection role
    DULINK_CONNECTION_ROLE connRole;
    /// Used to store transport params to pass into dulinkOpen
    uint64_t                trParamBuf[DUTR_PARAM_BUFLEN / sizeof(uint64_t)];
    /// Used to store security params to pass into dulinkOpen
    uint64_t                secParamBuf[DUTR_PARAM_BUFLEN / sizeof(uint64_t)];
    /// RefID of connection returned by DU Link
    uint32_t               refId;
    /// Remote path of connection being opened
    char                   remotePath[DULINK_MAX_PATH];
} DULINK_CONNECT_INFO, *PDULINK_CONNECT_INFO;

/* ------------------------ Common Helper Functions ------------------------- */
/*!
 * @brief Export DU Link directories, files and notify nodes.
 *
 * This function takes a list of DULINK_EXPORT_REQS for each type and will
 * export the corresponding DU Link node type based on which list it is in.
 *
 * @param[in] pDulinkDirs(const_DULINK_EXPORT_REQS*)
 *      List of export requirements for DU Link directories. If NULL, numDirs
 *      must be 0.
 *
 * @param[in] numDirs(uint8_t)
 *      Number of dirs in @a pDulinkDirs.
 *
 * @param[in] pDulinkFiles(const_DULINK_EXPORT_REQS*)
 *      List of export requirements for DU Link files. If NULL, numFiles must
 *      be 0.
 *
 * @param[in] numFiles(uint8_t)
 *      Number of files in @a pDulinkFiles.
 *
 * @param[in] pDulinkNotifyNodes(const_DULINK_EXPORT_REQS*)
 *      List of export requirements for DU Link notify nodes. If NULL,
 *      @a numNotifyNodes must be 0.
 *
 * @param[in] numNotifyNodes(uint8_t)
 *      Number of notify nodes in @a pDulinkNotifyNodes.
 *
 * @return DU_OK
 *      All nodes exported successfully.
 *
 * @return DUCOMMON_ERR_GENERIC
 *      Failed to export DU Link Nodes.
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
 *   - Runtime: Yes
 *   - De-Init: No
 */
DU_RCODE exportDULinkNodes
(
    const DULINK_EXPORT_REQS *pDulinkDirs,
    uint8_t                   numDirs,
    const DULINK_EXPORT_REQS *pDulinkFiles,
    uint8_t                   numFiles,
    const DULINK_EXPORT_REQS *pDulinkNotifyNodes,
    uint8_t                   numNotifyNodes
);

/*!
 * @brief Register a DU Link plugin to DU Master.
 *
 * This will tell DU Master to set up listeners for progress.notify,
 * current_rl.notify, pending_rl.notify, and state.notify on the plugin
 * registering to master.
 *
 * @param[in] pMasterPath(const_char*)
 *      DU Link path to DU Master element.
 *
 * @return DU_OK
 *      Successfully registered to DU Master.
 *
 * @return DUCOMMON_ERR_INVALID_ARGUMENT
 *      Invalid path to DU Master.
 *
 * @return DUCOMMON_ERR_GENERIC
 *      Failed to register to DU Master.
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
DU_RCODE registerToMaster
(
    const char *pMasterPath
);

/*!
 * @brief Replace $CONTENT_ROOT$ in path string to actual content.
 *
 * @param[in, out] pStr(char*)
 *      Path string to process.
 *
 * @param[in] pContRootVal(const_char*)
 *      Actual content root value.
 *
 * @param[in] bufLen(uint64_t)
 *      Maximum length of input/output buffer.
 *
 * @return DU_OK
 *      String successfully converted.
 *
 * @return DUCOMMON_ERR_BUFFER_TOO_SMALL
 *      Buffer size not enough for actual content.
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
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
DU_RCODE replaceContentRoot
(
    char       *pStr,
    const char *pContRootVal,
    uint64_t    bufLen
);

/*!
 * @brief Get plugin parent path.
 *
 * For plugins in the same VM, it should use the same parent path as there
 * should only be one router in a VM.
 *
 * @param[in, out] pBuf(char*)
 *      Buffer to store parent path.
 *
 * @param[in] bufLen(uint64_t)
 *      Parent buffer length.
 *
 * @return DU_OK
 *      Buffer is filled with parent path.
 *
 * @return DUCOMMON_ERR_GENERIC
 *      Error happens during read of device tree.
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
 *   - De-Init: No
 */
DU_RCODE getPluginParent
(
    char     *pBuf,
    uint32_t  bufLen
);

/*!
 * @brief Get plugin connection info for DU-Link open.
 *
 * Helper function to fetch connection info for each plugin. This would read
 * router configurations from device tree and convert it into the connection
 * configurations of this plugin.
 *
 * @param[in] pPluginName(const_char*)
 *      Name of the plugin.
 *
 * @param[in, out] pInfo(PDULINK_CONNECT_INFO)
 *      To be filled in with fetched connection info to be used by plugin.
 *
 * @return DU_OK
 *      Connection info successfully fetched from device tree.
 *
 * @return DUCOMMON_ERR_NOT_FOUND
 *      Plugin entry is not found, and plugin should exit.
 *
 * @return DUCOMMON_ERR_GENERIC
 *      Other error happened during read of device tree.
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
 *   - De-Init: No
 */
DU_RCODE getPluginConnInfo
(
    const char           *pPluginName,
    PDULINK_CONNECT_INFO  pInfo
);

/* ------------------------ Shared Callbacks -------------------------------- */
/*!
 * @brief Callback for read only strings in a DU-Link node.
 *
 * This callback is used for simply reading the value stored in a particular
 * string with no additional functionality required. Context passed to this
 * callback should be of CTX_LOCK_STR type. In case the mutex in the passed
 * structure is not NULL, the mutex will be locked surrounding any reading of
 * the string value. DULINK_CB_WRITE operation is not supported by this
 * callback.
 *
 * It can be used as the callback for the following DU-Link nodes:
 * plugin-type, state, progress, ver/installer, last_deployed/metadata,
 * last_deployed/result.
 *
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed.
 *
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request.
 *
 * @param[in] pCtx(void*)
 *          Pointer to CTX_LOCK_STR data structure.
 *
 * @param[in] offset(uint64_t)
 *          Should always be 0, otherwise error.
 *
 * @param[in] length(uint64_t)
 *          Length of buffer to use for reading.
 *
 * @param[in] pBuf(char*)
 *          Pointer to buffer for storing data.
 *
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it must be one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_SIZE
 *
 * @param[out] pRetVal(uint64_t*)
 *          Actual bytes read to buffer for operation DULINK_CB_READ,
 *          and length of the string for operation DULINK_CB_SIZE.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid operation or offset.
 *
 * @return DULINK_CB_ERR_UNKNOWN
 *          Not enough space to store value in buffer.
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
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
DU_RCODE readOnlyStringCB
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

/*!
 * @brief Callback for read-only .list nodes.
 *
 * This callback will iterate through a data structure of %CTX_LIST_STR
 * type and return a NULL separated list of strings containing all values in
 * the list. DULINK_CB_WRITE operation is not supported by this callback.
 *
 * It can be used as the callback for the following DU-Link nodes:
 * cmd.list, state.list.
 *
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed.
 *
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request.
 *
 * @param[in] pCtx(void*)
 *          Pointer to initialized %CTX_LIST_STR data structure.
 *
 * @param[in] offset(uint64_t)
 *          Should always be 0, otherwise error.
 *
 * @param[in] length(uint64_t)
 *          Length of buffer to use for reading.
 *
 * @param[in] pBuf(char*)
 *          Pointer to buffer for storing data.
 *
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it must be one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_SIZE
 *
 * @param[out] pRetVal(uint64_t*)
 *          Actual bytes read to buffer for operation DULINK_CB_READ,
 *          and length of the string for operation DULINK_CB_SIZE.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid operation or offset.
 *
 * @return DULINK_CB_ERR_UNKNOWN
 *          Not enough space to store value in buffer.
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
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
DU_RCODE listNodeCB
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

/*!
 * @brief Callback for read-only runlevel nodes.
 *
 * This callback will return a string representation of runlevel when reading
 * a node's either current or pending runlevel. DULINK_CB_WRITE operation
 * is not supported by this callback.
 *
 * It can be used as the callback for the following DU-Link nodes:
 * current_rl, pending_rl.
 *
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed.
 *
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request.
 *
 * @param[in] pCtx(void*)
 *          Pointer to DU_RUN_LEVEL storing runlevel.
 *
 * @param[in] offset(uint64_t)
 *          Should always be 0, otherwise error.
 *
 * @param[in] length(uint64_t)
 *          Length of buffer to use for reading.
 *
 * @param[in] pBuf(char*)
 *          Pointer to buffer for storing data.
 *
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it must be one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_SIZE
 *
 * @param[out] pRetVal(uint64_t*)
 *          Actual bytes read to buffer for operation DULINK_CB_READ,
 *          and length of the string for operation DULINK_CB_SIZE.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid operation or offset.
 *
 * @return DULINK_CB_ERR_UNKNOWN
 *          Not enough space to store value in buffer.
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
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
DU_RCODE readOnlyRunlevelCB
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

/** @} */
#ifdef __cplusplus
}
#endif

#endif // DUPLUGIN_H_
