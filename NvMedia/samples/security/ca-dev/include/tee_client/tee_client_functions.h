/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Client Functions</b>
 *
 * @b These functions initialize new TEE contexts, and form connections
 * between client applications and the TEE identified by the new
 * string identifier name.
 */

/**
 * @defgroup global_platform_functions TEE Client Functions
 * These functions initialize new TEE contexts, and form connections
 * between client applications and the TEE identified by the new
 * string identifier name.
 * @{
 * @ingroup global_platform_codes
 */

#ifndef TEE_CLIENT_FUNCTIONS_H
#define TEE_CLIENT_FUNCTIONS_H

/**
 * Initializes a new TEE context, forming a connection
 * between the client application and the TEE identified by the
 * string identifier name.
 *
 * If the client application passes a NULL name, the
 * Implementation must select a default TEE to connect to. The
 * supported name strings, the mapping of these names to a specific
 * TEE, and the nature of the default TEE are implementation-defined.
 * The caller must pass a pointer to a valid TEEC context in context.
 * The implementation must assume that all fields of the @c TEEC_Context
 * structure are in an undefined state.
 *
 * The client application is the TOS client. Client can open session with
 * single/multiple TA with single/multiple times.
 *
 * @param name A pointer to a zero-terminated string that describes
 * the TEE. If this parameter is set to NULL the Implementation MUST
 * select a default TEE.
 * @param context A pointer to a TEEC_Context structure that must be
 * initialized by the implementation.
 *
 * @return TEEC_SUCCESS if the initialization was successful or an
 * error code otherwise. Client must check the return result and it
 * should open session only when this function returns TEEC_SUCCESS.
 */
TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context);

/**
 * Finalizes an initialized TEE context, closing the
 * connection between the client application and the TEE.
 *
 * The client application must only call this function when all
 * sessions inside this TEE context have been closed and all shared
 * memory blocks have been released. The implementation of this
 * function must not be able to fail: after this function returns the
 * client application must be able to consider that the context has
 * been closed. The function implementation must do nothing if
 * context is NULL.
 *
 * @param context A pointer to an initialized TEEC_Context structure
 * to be finalized.
 */
void TEEC_FinalizeContext(TEEC_Context* context);


/**
 * Registers a block of existing client application memory as a block
 * of shared memory within the scope of the specified TEE context, in
 * accordance with parameters set by the client application inside the
 * sharedMem structure.
 *
 * The parameter context must point to an initialized TEE context.
 *
 * The parameter sharedMem must point to the Shared Memory structure
 * defining the memory region to register. The client application must
 * have populated the following fields of the Shared Memory structure
 * before calling this function:
 * - The buffer field must point to the memory region to be shared,
 *   and must not be NULL.
 * - The size field must contain the size of the buffer, in
 *   bytes. Zero is a valid length for a buffer.
 * - The flags field indicates the intended directions of data flow
 *   between the client application and the TEE.
 * - The implementation must assume that all other fields in the
 *   Shared Memory structure have undefined content.
 *
 * An implementation may put a hard limit on the size of a single
 * Shared Memory block, defined by the constant
 * TEEC_CONFIG_SHAREDMEM_MAX_SIZE. This function may fail to register
 * a block smaller than this limit due to a low resource condition
 * encountered at runtime, however.
 *
 * The buffer, size, and flags fields of the sharedMem structure must
 * be set in accordance with the specification described above
 *
 * @param context A pointer to an initialized TEE context
 * @param sharedMem A pointer to a Shared Memory structure to register:
 *
 * @return TEEC_SUCCESS if the registration was successful or
 * TEEC_ERROR_OUT_OF_MEMORY if the registration could not be completed
 * because of a lack of resources, or another error code for if
 * registration was not successful for some other reason.
 */
TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context* context,
                               TEEC_SharedMemory* sharedMem);

/**
 * Allocates a new block of memory as a block of shared memory within
 * the scope of the specified TEE context, in accordance with the
 * parameters which have been set by the client application inside the
 * sharedMem structure.
 *
 * The context parameter must point to an initialized TEE context.
 *
 * The sharedMem parameter must point to the shared memory structure
 * defining the region to allocate.
 *
 * Client applications must have populated the following fields of the
 * shared memory structure. The size field must contain the desired
 * size of the buffer, in bytes. The size is allowed to be zero. In
 * this case memory is allocated and the pointer written in to the
 * buffer field on return must not be NULL but must never be
 * de-referenced by the client application. In this case however, the
 * shared memory block can be used in Registered Memory References.
 *
 * The flags field indicates the allowed directions of data flow
 * between the client application and the TEE.
 *
 * The implementation must assume that all other fields in the shared
 * memory structure have undefined content.
 *
 * An implementation may put a hard limit on the size of a single
 * shared memory block, defined by the constant
 * TEEC_CONFIG_SHAREDMEM_MAX_SIZE. However it must be noted that this
 * function may fail to allocate a block of smaller than this limit
 * due to low resource scenarios encountered at runtime.
 *
 * If this function returns any code other than TEEC_SUCCESS the
 * implementation must have set the buffer field of sharedMem to NULL.
 *
 * Before calling this function, the client application must set the
 * size and flags fields. On return, for a successful allocation the
 * implementation must have set the pointer buffer to the address of
 * the allocated block, otherwise it must set buffer to NULL.
 *
 * @param context A pointer to an initialized TEE context.
 * @param sharedMem A pointer to a shared memory structure to allocate.
 *
 * @return TEEC_SUCCESS if the allocation was successful or
 * TEEC_ERROR_OUT_OF_MEMORY if the registration could not be completed
 * because of a lack of resources, or another error code for if
 * registration was not successful for some other reason.
 */
TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context,
                                      TEEC_SharedMemory* sharedMem);

/**
 * Deregisters or deallocates a previously initialized block of
 * shared memory.
 *
 * For a memory buffer allocated using TEEC_AllocateSharedMemory the
 * implementation must free the underlying memory and the client
 * application must NOT access this region after this function has
 * been called. In this case the implementation must set the buffer
 * and size fields of the sharedMem structure to NULL and 0
 * respectively before returning.
 *
 * For memory registered using TEEC_RegisterSharedMemory the
 * implementation must deregister the underlying memory from the TEE,
 * but the memory region remains available to the client application
 * for other purposes as the memory is owned by it.
 *
 * The implementation must do nothing if the sharedMem parameter is
 * NULL.
 *
 * @param sharedMem A pointer to a valid shared memory structure.
 */
void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem);

/**
 * Opens a new session between the client application and the
 * specified trusted application.
 *
 * The implementation must assume that all fields of this session
 * structure are in an undefined state. When this function returns
 * TEEC_SUCCESS the implementation must have populated this structure
 * with any information necessary for subsequent operations within the
 * session. The target trusted application is identified by a UUID
 * passed in the parameter destination.
 *
 * The session may be opened using a specific connection method that can
 * carry additional connection data, such as data about the user or
 * user-group running the client application, or about the Client
 * Application itself. This allows the trusted application to implement
 * access control methods which separate functionality or data accesses
 * for different actors in the rich environment outside of the TEE.
 * Standard connection methods are defined in section 4.4.5, but there
 * may be implementation-defined login methods in addition to these core
 * types. The additional data associated with each connection method is
 * passed in via the pointer connectionData. For the core login types
 * the following connection data is required:
 *
 * TEEC_LOGIN_PUBLIC: connectionData SHOULD be NULL.
 * TEEC_LOGIN_USER: connectionData SHOULD be NULL.
 * TEEC_LOGIN_GROUP: connectionData must point to a uint32_t which
 *   contains the group which this client application wants to connect
 *   as. The implementation is responsible for securely ensuring that
 *   the Client Application instance is actually a member of this
 *   group.
 * TEEC_LOGIN_APPLICATION: connectionData SHOULD be NULL.
 * TEEC_LOGIN_USER_APPLICATION: connectionData SHOULD be NULL.
 * TEEC_LOGIN_GROUP_APPLICATION: connectionData must point to a
 *   uint32_t which contains the group which this client application
 *   wants to connect as. The implementation is responsible for
 *   securely ensuring that the Client Application instance is
 *   actually a member of this group.
 *
 * @note This API intentionally omits any form of support for static
 * login credentials, such as PIN or password entry. The login methods
 * supported in the API are only those which have been identified as
 * requiring support by the rich operating environment. If a trusted
 * application requires static login credentials, then they can be
 * passed by the client application using the standard shared memory
 * mechanisms for data exchange.
 *
 * An open-session operation may optionally carry an operation payload,
 * and may also be cancellable. When the payload is present the parameter
 * operation must point to a TEEC_Operation structure populated by the
 * client application. If operation is NULL then no data buffers are
 * exchanged with the trusted application, and the operation cannot be
 * cancelled by the client application.
 *
 * The result of this function is returned both in the function
 * TEEC_Result return code and the return origin, stored in the variable
 * pointed to by returnOrigin:
 *
 * - If the return code is TEEC_ERROR_CANCEL, the operation was
 *   cancelled before it reached the trusted Application.
 * - If the return origin is TEEC_ORIGIN_TRUSTED_APP, the meaning of the
 *   return code depends on the protocol between the client application
 *   and the trusted application. However, if TEEC_SUCCESS is returned,
 *   it always means that the session was successfully opened and if the
 *   function returns a code different from TEEC_SUCCESS, it means that
 *   the session opening failed.
 *
 * @param context A pointer to an initialized TEE context.
 * @param session A pointer to a session structure to open.
 * @param destination A pointer to a structure containing the UUID of
 * the destination trusted application.
 * @param connectionMethod The method of connection to use.
 * @param connectionData Any data required to support the connection
 * method chosen.
 * @param operation A pointer to an operation containing a set of
 * parameters to exchange with the trusted application, or NULL if no
 * parameters are to be exchanged or if the operation cannot be
 * cancelled. Refer to TEEC_InvokeCommand for more information.
 * @param returnOrigin A pointer to a variable containing the return
 * origin. This field may be NULL if the return origin is not needed.
 *
 * @return TEEC_SUCCESS if successful, or an error code if return is
 * different from TEEC_ORIGIN_TRUSTED_APP, or a return code defined by
 * the protocol between the client application and the trusted
 * application if the returnOrigin is equal to
 * TEEC_ORIGIN_TRUSTED_APP.
 */
TEEC_Result TEEC_OpenSession(TEEC_Context* context,
                             TEEC_Session* session,
                             const TEEC_UUID* destination,
                             uint32_t connectionMethod,
                             const void* connectionData,
                             TEEC_Operation* operation,
                             uint32_t* returnOrigin);

/**
 * Closes a session which has been opened with a trusted
 * application.

 * All commands within the session must have completed before calling
 * this function. The implementation must do nothing if the session
 * parameter is NULL. The implementation of this function must NOT be
 * able to fail: after this function returns, the client application
 * must be able to consider that the session has been closed.
 *
 * @param session The session to close.
 */
void TEEC_CloseSession(TEEC_Session* session);


/**
 * Invokes a command within the specified session.
 *
 * The commandID parameter indicates which of the exposed trusted
 * application functions should be invoked. The supported command
 * identifiers are defined by the protocol of the trusted application.
 *
 * A Command may optionally carry an operation payload. When the
 * payload is present, the parameter operation must point to a
 * TEEC_Operation structure populated by the client application. If
 * operation is NULL then no parameters are exchanged with the trusted
 * application, and only the Command ID is exchanged.
 *
 * The operation structure is also used to manage cancellation of the
 * command. If cancellation is required then the operation pointer
 * must be non-NULL and the client application must have zeroed the
 * started field of the operation structure before calling this
 * function. The operation structure may contain no parameters if no
 * data payload is to be exchanged.
 *
 * Operation payload handling occurs in the following sequence:
 *
 * -# Each parameter in the operation payload is examined. If the
 *    parameter is a temporary memory reference, then it is registered for
 *    the duration of the operation in accordance with the fields set in
 *    the TEEC_TempMemoryReference structure and the data flow direction
 *    specified in the parameter type. Refer to the
 *    TEEC_RegisterSharedMemory function for error conditions which can be
 *    triggered during temporary registration of a memory region.
 * -# The contents of all the memory regions which are exchanged with the
 *    TEE are synchronized.
 * -# The fields of all value parameters tagged as input are read by the
 *    implementation. This applies to parameters of type TEEC_VALUE_INPUT
 *    or TEEC_VALUE_INOUT.
 * -# The operation is issued to the trusted application. During the
 *    execution of the command, the trusted application may read the data
 *    held within the memory referred to by input Memory References. It may
 *    also write data in to the memory referred to by output Memory
 *    References, but these modifications are not guaranteed to be
 *    observable by the client application until the command completes.
 * -# After the Command has completed, the implementation must update the
 *    size field of the Memory Reference structures flagged as output:
 *    -# For Memory References that are non-null and marked as output, the
 *       updated size field may be less than or equal to original size
 *       field. In this case this indicates the number of bytes actually
 *       written by the trusted application, and the implementation must
 *       synchronize this region with the client application memory space.
 *    -# For all memory references marked as output, the updated size field
 *       may be larger than the original size field. For null memory
 *       references, a required buffer size may be specified by the trusted
 *       Application. In these cases the passed output buffer is too small
 *       or absent, and the returned size indicates the size of the output
 *       buffer which is necessary for the operation to succeed. In these
 *       cases the implementation should not synchronize any shared data
 *       with the client application.
 * -# When the command completes, the implementation must update the fields
 *    of all Value Parameters tagged as output, i.e., of type
 *    TEEC_VALUE_OUTPUT or TEEC_VALUE_INOUT.
 * -# All memory regions temporarily registered at the beginning of
 *    the function are deregistered as if the function
 *    TEEC_ReleaseSharedMemory was called on each of them.
 * -# Control is passed back to the calling client application code.
 *
 * The result of this function is returned both in the function TEEC_Result
 * return code and the return origin, stored in the variable pointed to by
 * returnOrigin:
 * - If the return code is TEEC_ERROR_CANCEL then it means that the
 *   operation was cancelled before it reached the trusted application.
 * - If the return origin is TEEC_ORIGIN_TRUSTED_APP, then the meaning
 *   of the return code is determined by the protocol exposed by the
 *   trusted application. The trusted application can indicate success
 *   in the protocol with TEEC_SUCCESS (0). This allows a
 *   determination of success or failure without looking at the return
 *   origin.
 *
 * @param session The open session in which the command is invoked.
 * @param commandID The identifier of the command within the trusted
 * application to invoke. The meaning of each command identifier must
 * be defined in the protocol exposed by the trusted application.
 * @param operation A pointer to a client application initialized
 * TEEC_Operation structure, or NULL if there is no payload to send or
 * if the command does not need to support cancellation.
 * @param returnOrigin A pointer to a variable containing the return origin.
 * This field may be NULL if the return origin is not needed.
 * @return an error code if the return origin is different from
 * TEEC_ORIGIN_TRUSTED_APP, or a return code defined by the trusted
 * application protocol.
 */
TEEC_Result TEEC_InvokeCommand(TEEC_Session* session,
                               uint32_t commandID,
                               TEEC_Operation* operation,
                               uint32_t* returnOrigin);

/**
 * Requests the cancellation of a pending open session operation
 * or a Command invocation operation.
 *
 * As this is a synchronous API, this function must be called from a
 * thread other than the one executing the TEEC_OpenSession or
 * TEEC_InvokeCommand function.
 *
 * This function sends a cancellation signal to the TEE and returns
 * immediately; the operation is not guaranteed to have been cancelled
 * when this function returns. In addition, the cancellation request
 * is just a hint. the TEE or the trusted application may ignore the
 * cancellation request.
 *
 * It is valid to call this function using a TEEC_Operation structure
 * any time after the client application has set the started field of
 * an operation structure to zero. In particular, an operation can be
 * cancelled before it is actually invoked, during invocation, and
 * after invocation. Note that the client application must reset the
 * started field to zero each time an operation structure is used or
 * re-used to open a session or invoke a command if the new operation
 * is to be cancellable.
 *
 * Client applications must NOT reuse the operation structure for
 * another operation until the cancelled command has actually returned
 * in the thread executing the TEEC_OpenSession or TEEC_InvokeCommand
 * function.
 *
 * <b>Detecting Cancellation</b>
 *
 * In some implementations it may be possible for part of the
 * infrastructure to cancel the operation before it reaches the
 * trusted application. In these cases the return origin returned by
 * TEEC_OpenSession or TEEC_InvokeCommand must be either
 * TEEC_ORIGIN_API, TEEC_ORIGIN_COMMS, or TEEC_ORIGIN_TEE, and the
 * return code must be TEEC_ERROR_CANCEL.
 *
 * If the cancellation request is handled by the trusted application
 * itself then the return origin returned by TEEC_OpenSession or
 * TEEC_InvokeCommand must be TEE_ORIGIN_TRUSTED_APP, and the return
 * code is defined by the trusted application as protocol. If
 * possible, trusted applications use TEEC_ERROR_CANCEL for their
 * return code, but it is accepted that this is not always possible
 * due to onflicts with existing return code definitions in other
 * standards.
 *
 * @param operation A pointer to a client application instantiated
 * operation structure.
 */
void TEEC_RequestCancellation(TEEC_Operation* operation);

/**
 * Format the params from FOUR desired types
 */
#define TEEC_PARAM_TYPES(t0, t1, t2, t3) \
    TEEC_PARAM_TYPES_8(t0, t1, t2, t3, TEEC_NONE, TEEC_NONE, TEEC_NONE, \
    TEEC_NONE)

/**
 * @brief Helper macro to construct param type
 */
#define TEEC_PARAM_TYPES_8(t0, t1, t2, t3, t4, t5, t6, t7) \
      (((uint32_t)t0) | (((uint32_t)t1) << 4U) | (((uint32_t)t2) << 8U) | \
      (((uint32_t)t3) << 12U) | (((uint32_t)t4) << 16U) | \
      (((uint32_t)t5) << 20U) | (((uint32_t)t6) << 24U)| (((uint32_t)t7) << 28U))

/* Get the ith type from the param */
#define TEEC_PARAM_TYPE_GET(t, i) \
    (((t) >> (i * 4)) & 0xF)

/** @} */
#endif /* TEE_CLIENT_FUNCTIONS_H */
