/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Internal Client API</b>
 *
 * @b Description: Describes the TEE internal client API.
 */

/**
 * @defgroup global_platform_capi TEE Internal Client API
 *
 * Describes the TEE internal client API.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_CLIENT_API_H
#define TEE_INTERNAL_CLIENT_API_H

#include <tee_internal/tee_internal_datatypes.h>
#include <tee_internal/tee_internal_os_abstraction_layer.h>

/**
 * Opens a new session with a trusted application, or
 * a trusted entity in the rich environment. A dedicated outoing IVC
 * channel must have been established between this TA and the peer.
 *
 * The destination trusted application is identified by its UUID
 * passed in destination. This UUID can be hardcoded in the caller
 * code.
 *
 * An initial set of four parameters can be passed during the
 * operation.
 *
 * The result of this function is returned both in the return value
 * and the return origin, stored in the variable pointed to by
 * returnOrigin:
 *  - If the return origin is different from TEE_ORIGIN_TRUSTED_APP,
 *    then the function has failed before it could reach the target
 *    trusted application, or failed after the target trusted application
 *    returned TEE_SUCCESS in TA_OpenSessionEntryPoint.
 *  - If the return origin is TEE_ORIGIN_TRUSTED_APP, then the meaning
 *    of the return value depends on the protocol exposed by the target
 *    trusted application's TA_OpenSessionEntryPoint implementation.
 *
 * When the session is successfully opened, i.e., when the function
 * returns TEE_SUCCESS, a valid session handle is written into
 * *session. Otherwise, the value TEE_HANDLE_NULL is written into
 * *session.  When a session is to be closed, the client Trusted
 * Application must call the function TEE_CloseTASession with the
 * session handle.
 *
 * (QNX PDK Only) Error behavior:
 *        if there's any error, returns non-zero value.
 *
 * (QNX PDK Only) Side effects:
 *        if there's no error, an session with the target application is opened.
 *
 * @param[in] destination A pointer to a TEE_UUID structure containing the
 *        UUID of the destination trusted application.
 * @param[in] cancellationRequestTimeout Timeout or the
 *        special value TEE_TIMEOUT_INFINITE if there is no timeout.
 *        After the timeout expires, a cancellation request for the
 *        operation must be automatically sent. Unit: milliseconds.
 *
 *        (Linux PDK only) Valid Range: This parameter must be set to
 *        TEE_TIMEOUT_INFINITE
 *
 * @param[in] paramTypes The types of all parameters passed in the
 *        operation.
 * @param[in,out] params The parameters passed in the operation.
 * @param[out] session A pointer to a variable that receives the client
 *        session handle. The pointer must not be NULL. The value is
 *        set to TEE_HANDLE_NULL upon error.
 * @param[out] returnOrigin A pointer to a variable containing the
 *        return origin. This field may be NULL if the return origin
 *        is not needed.
 * @return If the return origin is different from TEE_ORIGIN_TRUSTED_APP, one
 *         of the following error codes is returned:
 *         (1) TEE_ERROR_OUT_OF_MEMORY: if no enough resources are
 *           available to open the session;
 *         (2) TEE_ERROR_ITEM_NOT_FOUND: if no trusted application matches the
 *           requested destination UUID.
 *         (3) TEE_ERROR_ACCESS_DENIED: if access to the destination trusted
 *           application is denied;
 *         (4) TEE_ERROR_EXCESS_DATA: if IPC frame
 *           is not big enough to store the data;
 *         (5) TEE_ERROR_OUT_OF_MEMORY: if target application's server buffer
 *           is not big enough to store the data;
 *         (6) TEE_ERROR_COMMUNICATION: if the IPC communication is reset
 *           by the target trusted application;
 *         (7) TEE_ERROR_CANCEL: if the request timed out;
 *         (8) TEE_ERROR_BUSY: if the server is busy handling previous
 *           cancelled operation;
 *         (9) TEE_ERROR_BAD_PARAMETERS: if input destination or session is
 *           nullptr.
 *         (10) (Linux PDK only) Valid Range: This parameter must be set
 *           to TEE_TIMEOUT_INFINITE
 *
 *         If the return origin is TEE_ORIGIN_TRUSTED_APP, the return code is
 *         defined by the protocol exposed by the destination Trusted
 *         Application's TA_OpenSessionEntryPoint.
 *         In any case, a return code set to TEE_SUCCESS means
 *         that the session was successfully opened and a return code
 *         different from TEE_SUCCESS means that the opening failed.
 */
TEE_Result TEE_OpenTASession(
	TEE_UUID *const destination,
	const uint32_t cancellationRequestTimeout,
	const uint32_t paramTypes,
	TEE_Param *const params,
	TEE_TASessionHandle *const session,
	uint32_t *const returnOrigin);

/**
 * Closes a client session. This function should be invoked
 * to close opened sessions to avoid resource leaking.
 *
 * (QNX PDK Only) Error behavior:
 *     (1) if the session is not valid or has been
 *         referred to by any operation handle or object handle,
 *         halts the system;
 *     (2) if there's other error, the errors are neglected.
 *
 * (QNX PDK Only) Side effects:
 *      if the operation runs successfully, the session is closed.
 *
 * @param[in] session: An opened session handle.
 */
void TEE_CloseTASession(const TEE_TASessionHandle session);

/**
 * Invokes a command within a session opened between the client
 * trusted application instance and a destination trusted application
 * or trusted rich environment entity.
 *
 * The parameter session must reference a valid session handle opened
 * by TEE_OpenTASession.
 *
 * Up to four parameters can be passed during the operation.
 *
 * The result of this function is returned both in the return value
 * and the return origin, stored in the variable pointed to by
 * returnOrigin:
 *  - If the return origin is different from TEE_ORIGIN_TRUSTED_APP,
 *    then the function has failed before it could reach the target
 *    trusted application, or failed after the target trusted application
 *    returned TEE_SUCCESS in TA_InvokeCommandEntryPoint.
 *  - If the return origin is TEE_ORIGIN_TRUSTED_APP, then the meaning
 *    of the return value depends on the protocol exposed by the target
 *    trusted application's TA_InvokeCommandEntryPoint implementation.
 *
 * (QNX PDK Only) Error behavior:
 *        if there's any error, returns non-zero value.
 *
 * (QNX PDK Only) Side effects:
 *        if there's no error, a command is invoked with the target application,
 *        and might change the state related to this session inside the target
 *        application.
 *
 * @param[in] session An opened session handle.
 * @param[in] cancellationRequestTimeout Timeout or the
 *        special value TEE_TIMEOUT_INFINITE if there is no timeout.
 *        After the timeout expires, a cancellation request for the
 *        operation must be automatically sent. Unit: milliseconds.
 *
 *        (Linux PDK only) Valid Range: This parameter must be set to
 *        TEE_TIMEOUT_INFINITE.
 *
 * @param[in] commandID The identifier of the Command to invoke. The
 *        meaning of each Command Identifier must be defined in the
 *        protocol exposed by the target trusted application.
 * @param[in] paramTypes The types of all parameters passed in the
 *        operation.
 * @param[in,out] params The parameters passed in the operation.
 * @param[out] returnOrigin A pointer to a variable which will contain the
 *        return origin. This field may be NULL if the return origin
 *        is not needed.
 * @return If the return origin is different from TEE_ORIGIN_TRUSTED_APP, one
 *        of the following error codes can be returned:
 *        (1) TEE_ERROR_OUT_OF_MEMORY: if not enough resources are
 *          available to open the session;
 *        (2) TEE_ERROR_ITEM_NOT_FOUND: if no trusted application matches the
 *          requested destination UUID;
 *        (3) TEE_ERROR_ACCESS_DENIED: if access to the destination trusted
 *          application is denied;
 *        (4) TEE_ERROR_EXCESS_DATA: if IPC frame is not big enough to store the
 *        data;
 *        (5) TEE_ERROR_OUT_OF_MEMORY: if target application's server buffer
 *          is not big enough to store the data;
 *        (6) TEE_ERROR_COMMUNICATION: if the IPC communication is reset by the
 *        target trusted application.
 *        (7) TEE_ERROR_CANCEL: if the request timed out;
 *        (8) TEE_ERROR_BUSY: if the server is busy handling previous cancelled
 *        operation.
 *        (9) (Linux PDK Only) TEE_ERROR_NOT_SUPPORTED: if
 *        cancellationRequestTimeout is not set to TEE_TIMEOUT_INFINITE.
 *
 *        If the return origin is TEE_ORIGIN_TRUSTED_APP, the return code is
 *        defined by the return value of TA_InvokeCommandEntryPoint of the
 *        target trusted application.
 */
TEE_Result TEE_InvokeTACommand(
	const TEE_TASessionHandle session,
	const uint32_t cancellationRequestTimeout,
	const uint32_t commandID,
	const uint32_t paramTypes,
	TEE_Param *const params,
	uint32_t *const returnOrigin);
/** @} */
#endif /* TEE_INTERNAL_CLIENT_API_H */
