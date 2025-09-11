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
 * @file
 *
 * @brief NVIDIA DRIVE&reg; Update DU-Link library interface.
 *
 * Access DU Link file node interfaces using 'libnvdulink'.
 */

#ifndef DULINK_H_
#define DULINK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ DRIVE Update Includes --------------------------- */
#include "ducommon.h"
#include "dutransport.h"
/*!
 * @defgroup du_link_group DU-Link API
 *
 * NVIDIA DRIVE&reg; Update DU-Link library interface.
 *
 * @ingroup drive_update_top
 * @{
 */
/* ------------------------ Defines------------------------------------------ */

/// Sub-unit ID for internal DU Link functionality.
#define DULINK_SUID_LINK   (0U)
/// Sub-unit ID for callback functionality.
#define DULINK_SUID_CB     (1U)

/// Used to tag DU Link internal error codes.
#define DULINK_ECODE(err)         DU_ECODE(DU_UID_LINK, DULINK_SUID_LINK, (err))
/// Used to tag callback specific error codes.
#define DULINK_CB_ECODE(err)      DU_ECODE(DU_UID_LINK, DULINK_SUID_CB, (err))

// List type of DU Link Errors.
/// DU Link generic error.
#define DULINK_ERR_GENERIC             DULINK_ECODE(1U)
/// DU Link invalid argument error.
#define DULINK_ERR_INVALID_ARGUMENT    DULINK_ECODE(2U)
/// DU Link buffer too small error.
#define DULINK_ERR_BUFFER_TOO_SMALL    DULINK_ECODE(3U)
/// DU Link element or node can not found error.
#define DULINK_ERR_NOT_FOUND           DULINK_ECODE(4U)
/// DU Link operation type or messag type not supportedd error.
#define DULINK_ERR_NOT_SUPPORTED       DULINK_ECODE(5U)
/// DU Link no permission error.
#define DULINK_ERR_NO_PERMISSION       DULINK_ECODE(6U)
/// DU Link allocate error.
#define DULINK_ERR_ALLOC_FAILED        DULINK_ECODE(7U)
/// DU Link resource busy error.
#define DULINK_ERR_BUSY                DULINK_ECODE(8U)
/// DU Link uplink not ready error.
#define DULINK_ERR_UPLINK_NOT_READY    DULINK_ECODE(9U)
/// DU Link router can not routing error.
#define DULINK_ERR_ROUTING             DULINK_ECODE(10U)
/// DU Link timeout error.
#define DULINK_ERR_TIMEOUT             DULINK_ECODE(11U)
/// DU Link transport error.
#define DULINK_ERR_TRANSPORT           DULINK_ECODE(12U)

/// DU Link illegal message error.
#define DULINK_ERR_ILLEGALMSG          DULINK_ECODE(13U)
/// DU Link router routing ret message error.
#define DULINK_ERR_ROUTING_RETMSG      DULINK_ECODE(14U)
/// DU Link no destination error.
#define DULINK_ERR_NO_DST              DULINK_ECODE(15U)
/// DU Link no enough memory error.
#define DULINK_ERR_NO_MEMORY           DULINK_ECODE(16U)
/// DU Link no P2P connection error.
#define DULINK_ERR_NO_P2P              DULINK_ECODE(17U)
/// DU Link no connection error.
#define DULINK_ERR_NO_CONNECT          DULINK_ECODE(18U)
/// DU Link illegal operation error.
#define DULINK_ERR_ILLEGAL_OP          DULINK_ECODE(19U)
/// DU Link initialize error.
#define DULINK_ERR_INIT                DULINK_ECODE(20U)
/// DU Link close error.
#define DULINK_ERR_CLOSE               DULINK_ECODE(21U)
/// DU Link not initialized error.
#define DULINK_ERR_NOT_INIT            DULINK_ECODE(22U)

// Possible return values for DU Link callbacks.
#define DULINK_CB_RCODE                DU_RCODE
#define DULINK_CB_OK                   DU_OK
#define DULINK_CB_ERR_IO               DULINK_CB_ECODE(1U)
#define DULINK_CB_ERR_INVALID_ARGUMENT DULINK_CB_ECODE(2U)
#define DULINK_CB_ERR_SYNTAX           DULINK_CB_ECODE(3U)
#define DULINK_CB_ERR_UNKNOWN          DULINK_CB_ECODE(4U)
#define DULINK_CB_ERR_BUSY_RETRY       DULINK_CB_ECODE(5U)

#define DULINK_CB_ERR_NOT_SUPPORTED    DULINK_CB_ECODE(6U)
#define DULINK_CB_ERR_NO_NODE          DULINK_CB_ECODE(7U)
/*!
 * Used to check if DU_RCODE return is from a DU Link callback. Non-zero value
 * indicates return value comes from callback, whereas zero indicates that a
 * DU Link internal error has occurred.
 */
#define DULINK_IS_CB_ERR(rcode)    (DU_ECODE_GET_SUID(rcode) == DULINK_SUID_CB)

/*!
 * Get DULINK_CB_RCODE value from a DU_RCODE return value.
 */
#define DULINK_GET_CB_ERR(rcode)   (DU_ECODE_GET_ERR(rcode))

/// DU Link version string.
#define DULINK_VERSION_STR "2"
/*!
 * Maximum string length of a DU Link path.
 *
 * Any bytes except \0 and / are allowed in path.
 */
#define DULINK_MAX_PATH              (256U)
/// Maximum allowed DU-Link node name.
#define DULINK_MAX_NODE_NAME         (DULINK_MAX_PATH)
/// Maximum allowed child nodes within each DU-Link node.
#define DULINK_MAX_CHILDNODE         (256U)
/// Maximum number of nodes within DU-Link instance.
#define DULINK_MAX_NODES             (256U)
/// Maximum number of ACLs of a single node.
#define DULINK_MAX_ACL               (16U)
/// Maximum allowed number of notify nodes.
#define DULINK_MAX_NOTIFY_NODES      (16U)
/// Maximum number of nodes a notify node can signal.
#define DULINK_MAX_NODES_TO_NOTIFY   (8U)
/// Maximum data length to read or write by notify node.
#define DULINK_MAX_NOTIFY_MSG_LEN    (1024U)
/// Maximum data buffer size that one DU-Link transaction can handle.
#define DULINK_MAX_DATA_SIZE         (1024U * 1024U)

#define DULINK_PERMISSION_NONE       (0x00U)
/// Represents read and size permission.
#define DULINK_PERMISSION_READ       (0x01U)
/// Represents write permission.
#define DULINK_PERMISSION_WRITE      (0x02U)
/// Represents write permission mask.
#define DULINK_PERMISSION_WRITE_MASK (0xFDU)
// Remaining Bits are reserved and must be 0.
/// Close all connection.
#define DULINK_CLOSE_ALL             (0xFFFFFFFFU)

/* ------------------------ Types ------------------------------------------- */

/*!
 * Type of DU Link node.
 */
typedef enum DULINK_NODE_TYPE
{
    /// Element type node. There is one element node per instance of DU Link.
    DULINK_NODE_TYPE_ELEMENT = 0,
    /// Directory node which can store file nodes beneath it.
    DULINK_NODE_TYPE_DIRECTORY,
    /// File type node. These are the most common type of node and are
    /// associated with a user defined callback function for READ, WRITE,
    /// SIZE operations.
    DULINK_NODE_TYPE_FILE
} DULINK_NODE_TYPE;

/*!
 * Type of DU Link connection.
 */
typedef enum DULINK_CONNECTION_TYPE
{
    DULINK_CONNECT_UPLINK = 0,
    DULINK_CONNECT_DOWNLINK,
    DULINK_CONNECT_P2P
} DULINK_CONNECTION_TYPE;


/*!
 * Access Control List Entry
 *
 * DULINK_ACL_ENTRY stores set of permissions controlling access to the node
 * from a single source node.
 */
typedef struct DULINK_ACL_ENTRY
{
    ///
    /// Absolute path of element that this access control take effect.
    ///
    /// Specially, if the input string ends with '/' followed by '*',
    /// permission control will apply to all child elements.
    ///
    /// Example:
    ///
    ///     /master would only apply permission control to actions from
    ///     master element.
    ///
    ///     /master/ * would only apply permission control to all sub element
    ///     under master element.
    ///
    char    srcElement[DULINK_MAX_PATH];
    ///
    /// Bitmask of allowed operation, this has lower priority of denied
    /// operation. Operation would only be allowed if allowMask is set and
    /// denyMask is not set.
    ///
    uint8_t allowMask; // DULINK_PERMISSION_READ | DULINK_PERMISSION_WRITE
    ///
    /// Bitmask of denied operation, setting this would force disallow operation
    /// specified in srcElement.
    ///
    uint8_t denyMask;
} DULINK_ACL_ENTRY, *PDULINK_ACL_ENTRY;

/*!
 * Attribute of an DU-Link node
 *
 * DULINK_ATTR stores information about a node, including its type and all of
 * the access permissions allowed by other DU Link elements.
 */
typedef struct DULINK_ATTR
{
    /// Type of this DU-Link node: file, directory, or element.
    DULINK_NODE_TYPE type;
    /// Number of valid ACL entries stored in ACL table.
    /// This number should always be at least 1
    uint32_t         numACL;
    ///
    /// Content of Access Control List. It will have at least one entry.
    ///
    /// The first entry must have sourcePath of length 2, with first character
    /// '/' followed by '*' which represents global ACL behavior
    ///
    /// Entries are applied on top of each other, one element can be present in
    /// different ACL entry.
    ///
    /// One element would allow to access if either of the entries covered it
    /// allow access AND none of the entries covered it deny access
    ///
    DULINK_ACL_ENTRY acl[DULINK_MAX_ACL];
} DULINK_ATTR, *PDULINK_ATTR;

/*!
 * Type of Callback operation
 */
typedef uint32_t DULINK_CB_OPERATION;
/// Represents READ operation to be run in callback function
/// for dulinkRead()
#define DULINK_CB_READ  (0U)
/// Represents WRITE operation to be run in callback function
/// for dulinkWrite()
#define DULINK_CB_WRITE (1U)
/// Represents SIZE operation to be run in callback function
/// for dulinkGetSize()
#define DULINK_CB_SIZE  (2U)
/// Reserved for internal usage and would not be passed to callback function
#define DULINK_CB_ATTR  (3U)

/*!
 * @brief Callback function registered to handle IO operations of file.
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed.
 *
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request.
 *
 * @param[in] pCtx(void*)
 *          Pointer to user application context. This pointer will be set
 * during export file operation and will be passed into the callback by DU Link
 * each time the callback is executed.
 *
 * @param[in] offset(uint64_t)
 *          Offset of IO operation. This parameter would not be valid for
 * DULINK_CB_SIZE operation.
 *
 * @param[in] length(uint64_t)
 *          Size wanted to read/write. This parameter would not be valid for
 * DULINK_CB_SIZE operation.
 *
 * @param[in,out] pBuf
 *          Pointer to buffer provided for read/write operation. This
 * parameter would not be valid for DULINK_CB_SIZE operation.
 *
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it would be one of:
 *        - DULINK_CB_READ
 *        - DULINK_CB_WRITE
 *        - DULINK_CB_SIZE
 *
 * @param[out] pRetVal(uint64_t*)
 *          Return value from callback:
 *        - for operation DULINK_CB_READ Actual bytes read
 *        - for operation DULINK_CB_WRITE Actual bytes write
 *        - for operation DULINK_CB_SIZE size of the file
 *        .
 *          DU Link honors retVal to check how many bytes actually processed when
 * doing read/write operations. Correct number must be returned or otherwise may
 * lead to dead loop.
 *
 * @return DULINK_CB_OK
 *          Upon success.
 *
 * @return DULINK_CB_ERR_*
 *          Error has occurred. Possible errors must come from DULINK_CB_ERR.
 */
typedef DULINK_CB_RCODE (*DULINK_CB)(const char* pRequestPath,
                         const char* pOriginPath, void *pCtx, uint64_t offset,
                         uint64_t length, void* pBuf,
                         DULINK_CB_OPERATION operation, uint64_t *pRetVal);

/* ------------------------ Function Declarations --------------------------- */
/*!
 * @brief Initialize the DU Link element.
 *
 * This function should be called once and only once per element during program
 * execution and would set element name and parent path of DU Link element.
 *
 * Call any other API before calling this function would result in an error.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pName(const_char*)
 *          Name of this element.
 *
 * @param[in] pParentPath(const_char*)
 *          Parent path of this element.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_GENERIC
 *          Other errors.
 *
 * pre None
 *
 * usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: No
 */
DU_RCODE dulinkInit
(
    const char *pName,
    const char *pParentPath
);

/*!
 * @brief Register a connection.
 *
 * This function can be called to register a connection to DU Link element, it
 * can be either UPLINK connection, DOWNLINK connection or P2P connection.
 *
 * Only one UPLINK would be allowed per element. In MVP version only P2P
 * connection is supported.
 *
 * The connection parameter is same as dutrOpen, however no handle would be
 * returned and the handle would be stored internally in DU-Link.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pRemotePath(const_char*)
 *          Absolute path of node being connected. Once connection is
 * established, this will be verified by reading the remote node's parent path
 * and name to ensure the connection has been made to the expected node.
 * Verifying remote path will be skipped for P2P connection type.
 *
 * @param[in] trType(DUTR_TR_TYPE)
 *          Type of back-end transport protocol used.
 *
 * @param[in] seType(DUTR_SEC_TYPE)
 *          Type of security protocol.
 *
 * @param[in] pTrParam(const_void*)
 *          Pointer to the argument of selected transport protocol. For instance
 * if trType is DUTR_TR_TYPE_IVC, then it shall point to the structure
 * DUTR_IVC_PARAM.
 *
 * @param[in] pSeParam(const_void*)
 *          Pointer to the argument of selected security protocol. For instance
 * if seType is DUTR_SEC_TYPE_TLS, then it shall point to the structure
 * DUTR_SEC_TLS_PARAM.
 *
 * @param[in] connType(DULINK_CONNECTION_TYPE)
 *          Type of connection.
 *
 * @param[out] pConnRefId(uint32_t*)
 *          Reference ID can be used to close connection.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_NOT_SUPPORTED
 *          Connection not yet supported.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Remote path is too long.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Error from DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Other errors.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkOpen
(
    const char             *pRemotePath,
    DUTR_TR_TYPE            trType,
    DUTR_SEC_TYPE           seType,
    const void             *pTrParam,
    const void             *pSeParam,
    DULINK_CONNECTION_TYPE  connType,
    uint32_t               *pConnRefId
);

/*!
 * @brief Close DU Link connection.
 *
 * Calling this function would disconnect a particular connection or all
 * connection established.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] connRefId(uint32_t)
 *          Reference ID returned from dulinkOpen, or DULINK_CLOSE_ALL.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_NO_PERMISSION
 *          Connection already closed/not yet initialized.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Error from DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Other errors.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: Yes
 */
DU_RCODE dulinkClose
(
    uint32_t connRefId
);

/*!
 * @brief Read content from a DU-Link node.
 *
 * This function can be called to read content of a DU-Link file or obtain a
 * NULL separated list of sub nodes under particular element/directory.
 *
 * This call is a blocking call. DU Link would do framing and do multiple DU TR
 * transmission if needed.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to node wanted to read. This can be an absolute path
 * start with "/" or relative path under this element
 *
 * @param[in] offset(uint64_t)
 *          Offset of read operation
 *
 * @param[in] length(uint64_t)
 *          Size wanted to read.
 *
 * @param[out] pBuf(void*)
 *          Pointer to caller allocated buffer provided for data fill in.
 *
 * @param[out] pRetLen(uint64_t*)
 *          Size of data actually read.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_ROUTING
 *          Could not find open connection to requested path.
 *
 * @return DULINK_ERR_UPLINK_NOT_READY
 *          Connection to requested path is not ready.
 *
 * @return DULINK_ERR_ALLOC_FAILED
 *          Not enough memory or memory allocation failed with DU_TR.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Transportation error happened in DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Failed to fetch maximum buffer size in DU_TR.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Request/Origin path is too long.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return @ref DULINK_ERR_NOT_FOUND
 *          Requested node is not found.
 *
 * @return  DULINK_ERR_NO_PERMISSION
 *          Permission check failed.
 *
 * @return Callback errors (DULINK_IS_CB_ERR would return true)
 *          Error from callback, use DULINK_GET_CB_ERR to extract them.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes, with the following conditions:
 *     - User defined callback is thread-safe
 *   - Re-entrant: Yes, with the following conditions:
 *     - User defined callback is re-entrant
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DU_RCODE dulinkRead
(
    const char *pPath,
    uint64_t    offset,
    uint64_t    length,
    void       *pBuf,
    uint64_t   *pRetLen
);

/*!
 * @brief Write content to a DU-Link file.
 *
 * This function can be called to write content to a DU-Link file. Calling this
 * function on an element or directory would result in an error.
 *
 * This call is a blocking call. DU Link would do framing and do multiple DU TR
 * transmission if needed.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(char*)
 *          Path to file wanted to write. This can be an absolute path
 * start with "/" or relative path under this element.
 *
 * @param[in] offset(uint64_t)
 *          Offset of write operation.
 *
 * @param[in] length(uint64_t)
 *          Size wanted to write.
 *
 * @param[in] pBuf(void*)
 *          Pointer to caller allocated buffer provided for data.
 *
 * @param[out] pRetLen(uint64_t*)
 *          Size of data actually written.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_ROUTING
 *          Could not find open connection to requested path.
 *
 * @return DULINK_ERR_UPLINK_NOT_READY
 *          Connection to requested path is not ready.
 *
 * @return DULINK_ERR_ALLOC_FAILED
 *          Not enough memory or memory allocation failed with DU_TR.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Transportation error happened in DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Failed to fetch maximum buffer size in DU_TR.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Request/Origin path is too long.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested node is not found.
 *
 * @return DULINK_ERR_NO_PERMISSION
 *          Permission check failed.
 *
 * @return Callback errors (DULINK_IS_CB_ERR would return true)
 *          Error from callback, use DULINK_GET_CB_ERR to extract them.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes, with the following conditions:
 *     - User defined callback is thread-safe
 *   - Re-entrant: Yes, with the following conditions:
 *     - User defined callback is re-entrant
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
DU_RCODE dulinkWrite
(
    const char *pPath,
    uint64_t    offset,
    uint64_t    length,
    void       *pBuf,
    uint64_t   *pRetLen
);

/*!
 * @brief Get size needed for reading content from DU-Link node.
 *
 * This function would return size of a DU-Link file, or buffer size needed for
 * NULL separated list of sub nodes under particular element/directory.
 *
 * This call is a blocking call.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to node wanted to get size. This can be an absolute path
 * start with "/" or relative path under this element.
 *
 * @param[out] pSize(uint64_t*)
 *          Size obtained.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_ROUTING
 *          Could not find open connection to requested path.
 *
 * @return DULINK_ERR_UPLINK_NOT_READY
 *          Connection to requested path is not ready.
 *
 * @return DULINK_ERR_ALLOC_FAILED
 *          Not enough memory or memory allocation failed with DU_TR.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Transportation error happened in DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Failed to fetch maximum buffer size in DU_TR.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Request/Origin path is too long.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested node is not found.
 *
 * @return DULINK_ERR_NO_PERMISSION
 *          Permission check failed.
 *
 * @return Callback errors (DULINK_IS_CB_ERR would return true)
 *          Error from callback, use DULINK_GET_CB_ERR to extract them.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkGetSize
(
    const char *pPath,
    uint64_t   *pSize
);

/*!
 * @brief Get attribute of a DU-Link node.
 *
 * This function can be called to obtain attribute of a DU-Link node. The
 * caller would obtain a copy of the attribute.
 *
 * This call is a blocking call.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to node wanted to obtain attribute. This can be an absolute
 * path start with "/" or relative path under this element.
 *
 * @param[out] pAttribute(PDULINK_ATTR)
 *          Pointer to caller allocated output buffer.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_ROUTING
 *          Could not find open connection to requested path.
 *
 * @return DULINK_ERR_UPLINK_NOT_READY
 *          Connection to requested path is not ready.
 *
 * @return DULINK_ERR_ALLOC_FAILED
 *          Not enough memory or memory allocation failed with DU_TR.
 *
 * @return DULINK_ERR_TRANSPORT
 *          Transportation error happened in DU_TR.
 *
 * @return DULINK_ERR_GENERIC
 *          Failed to fetch maximum buffer size in DU_TR.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Request/Origin path is too long.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Invalid input argument.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested node is not found.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkGetAttribute
(
    const char   *pPath,
    PDULINK_ATTR  pAttribute
);

/*!
 * @brief Register a directory to DU LINK.
 *
 * This function can be called to register a directory. Attribute needs to be
 * provided to set correct access control.
 *
 * This function would return error if Attribute is incorrect
 * (ex. type is not directory or numACL is 0).
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to registered directory. Path would be relative to
 * current element.
 *
 * @param[in] pAttribute(const_PDULINK_ATTR)
 *          Pointer to attribute buffer. Caller can deallocate the buffer after
 * calling this function.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested parent node is not found.
 *
 * @return DULINK_ERR_NOT_SUPPORTED
 *          Trying to export a directory with same name as existing node.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Node table is full, no more node can be added.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkExportDirectory
(
    const char         *pPath,
    const DULINK_ATTR  *pAttribute
);

/*!
 * @brief Register a file to DU LINK.
 *
 * This function can be called to register a file. Attribute needs to be
 * provided to set correct access control.
 *
 * This function would return error if Attribute is incorrect
 * (ex. type is not file or numACL is 0).
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to registered file. Path would be relative to current element.
 *
 * @param[in] pAttribute(const_PDULINK_ATTR)
 *          Pointer to attribute buffer. Caller can deallocate the buffer after
 * calling this function.
 *
 * @param[in] callback(DULINK_CB)
 *          Callback function used for IO operation.

 * @param[in] pCtx(void*)
 *          Pointer to user application context. This pointer will be passed
 * into the callback each time it is executed.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested parent node is not found.
 *
 * @return DULINK_ERR_NOT_SUPPORTED
 *          Trying to export a file with same name as existing node.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Node table is full, no more node can be added.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkExportFile
(
    const char         *pPath,
    const DULINK_ATTR  *pAttribute,
    DULINK_CB           callback,
    void               *pCtx
);

/*!
 * @brief Unregister a directory from DU LINK.
 *
 * Directory must not have child nodes.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to directory wanted to unregister. Path would be relative to
 * current element.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested directory is not found.
 *
 * @return DULINK_ERR_NOT_SUPPORTED
 *          Trying to remove a non-empty directory.
 *
 * @return DULINK_ERR_GENERIC
 *          Internal data corruption.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkUnlinkDirectory
(
    const char *pPath
);

/*!
 * @brief Unregister a file from DU LINK
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pPath(const_char*)
 *          Path to file wanted to unregister. Path would be relative to
 * current element.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Requested file is not found.
 *
 * @return DULINK_ERR_GENERIC
 *          Internal data corruption.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkUnlinkFile
(
    const char *pPath
);

/*!
 * @brief Create a node to notify other DU Link nodes when change has occurred.
 *
 * This function will create a new DU Link node for notifying other nodes when a
 * change has occurred within the node being monitored. The new DU Link node
 * must be named with .notify extension, with name as \<MonitorPath>.notify.
 *
 * For example, if exporting a notify node to monitor path /myElement/state, the
 * notify node should be named as /myElement/state.notify. Note that the node
 * being monitored must already exist before calling dulinkExportNotifyNode.
 * In this case, /myElement/state must exist before calling this function with
 * /myElement/state.notify.
 *
 * The new DU Link notify node will be readable and writable by the same set
 * of nodes that have permission to read the monitor node.
 *
 * A notify node can be written with dulinkWrite to add or remove nodes from
 * the list of nodes to notify when triggerNotify is called. To add a node to
 * the list, "+<NODE_PATH>" should be written to the .notify node. To remove a
 * node from the notify list, "-<NODE_PATH>" should be written.
 *
 * When a node is added to the notify list, it will immediately be notified
 * of the current value of the node being monitored.
 *
 * Notice that, the internal buffer used to read from node being monitored
 * has size of DULINK_MAX_NOTIFY_MSG_LEN. Data longer than this may truncated
 * when notifying.
 *
 * Calling dulinkRead on the .notify node will return a NULL separated list of
 * all nodes currently in the notify list. dulinkGetSize will return the length
 * of this list.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pNotifyPath(const_char*)
 *          Path of .notify node to export. pNotifyPath should be the path of
 * an already existing local node with addition of ".notify" suffix.
 *          For example, if a node called state should get a notify
 * node, pNotifyPath should be "/myElement/state.notify".
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Corresponding monitor node for notify node is not found.
 *
 * @return DULINK_ERR_NOT_SUPPORTED
 *          Trying to export a node with same name as existing node.
 *
 * @return DULINK_ERR_BUFFER_TOO_SMALL
 *          Node table is full, no more nodes can be added.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkExportNotifyNode
(
    const char *pNotifyPath
);

/*!
 * @brief Trigger notification to all nodes listening to a notify node.
 *
 * This function will trigger an exported notify node to update all listening
 * nodes of the latest state of the monitor node.
 *
 * When this function is called, the value provided would be forwarded to
 * listeners via dulinkWrite.
 *
 * This function requires that the notify node passed into pNotifyPath
 * is currently exported under the current local DU Link element.
 *
 * This function is a blocking call, it would not return until the broadcast is
 * done.
 *
 * Notice that, the internal buffer used to read from node being monitored
 * has size of DULINK_MAX_NOTIFY_MSG_LEN. Data longer than this may truncated
 * when notifying.
 *
 * language C
 *
 * interface DU-Link Interface Spec
 *
 * @param[in] pNotifyPath(const_char*)
 *          Path of exported notify node to trigger.
 *
 * @param[in] pBuf(void*)
 *          Content wants to be broadcasted.
 *
 * @param[in] bufSize(uint64_t)
 *          Size of input buffer.
 *
 * @return DU_OK
 *          Upon success.
 *
 * @return DULINK_ERR_INVALID_ARGUMENT
 *          Input argument is invalid.
 *
 * @return DULINK_ERR_NOT_FOUND
 *          Corresponding monitor node is not found.
 *
 * pre
 *   dulinkInit should already be called
 *
 * usage
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
DU_RCODE dulinkTriggerNotify
(
    const char *pNotifyPath,
    void       *pBuf,
    uint64_t    bufSize
);

#ifdef NON_SAFETY_BUILD
/*!
 * @brief Get DU Link data structure size before call dulinkSetInitParams
 *
 * @param[out] pDulinkConnectionSize(uint32_t*)
 *          Pointer to memory store sizeof PDULINK_CONNECTION
 *
 * @param[out] pDulinkWorkerSize(uint32_t*)
 *          Pointer to memory store sizeof PDULINK_WORKER
 *
 * @param[out] pDulinkWaitObjSize(uint32_t*)
 *          Pointer to memory store sizeof PDULINK_WAIT_OBJ
 *
 * @return DU_OK
 *          Params size is successfully get
 *
 * @return DULINK_ERR_GENERIC
 *          Can not get params size
 *
 * usage
 * - This API available on standard build ONLY.
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
DU_RCODE dulinkGetParamsSize
(
    uint32_t *pDulinkConnectionSize,
    uint32_t *pDulinkWorkerSize,
    uint32_t *pDulinkWaitObjSize
);

/*!
 * @brief Set DU Link Init params
 *
 * @param[in] numConnection(uint32_t)
 *          Max number of connections allowed in this dulink element
 *
 * @param[in] pConnectionTable(void*)
 *          Pointer of connections table in this dulink element
 *
 * @param[in] numWorker(uint32_t)
 *          Max number of workers allowed in this dulink element
 *
 * @param[in] pWorkerTable(void*)
 *          Pointer of workers table in this dulink element
 *
 * @param[in] pWaitingTable(void*)
 *          Pointer of wait object table in this dulink element
 *
 * @return DU_OK
 *          Param is successfully set
 *
 * @return DULINK_ERR_GENERIC
 *          DU Link is already initalized and can't change params
 *
 * usage
 * - This API available on standard build ONLY.
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
DU_RCODE dulinkSetInitParams
(
    uint32_t    numConnection,
    void       *pConnectionTable,
    uint32_t    numWorker,
    void       *pWorkerTable,
    void       *pWaitingTable
);
#endif

/** @} */

#ifdef __cplusplus
}
#endif

#endif
