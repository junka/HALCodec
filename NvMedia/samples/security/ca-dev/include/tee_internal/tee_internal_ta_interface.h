/*
 * Copyright (c) 2010-2023 GlobalPlatform Inc. All Rights Reserved.
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
 * @brief <b>GlobalPlatform: Tee Internal TA Interface</b>
 *
 * @b Description: Describes Tee internal TA interface.
 */

/**
 * @defgroup global_platform_ta_interface TEE Internal TA Interface
 *
 * Describes TEE internal TA interface.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_TA_INTERFACE_H
#define TEE_INTERNAL_TA_INTERFACE_H

#include <tee_internal/tee_internal_datatypes.h>

/**
 * @brief The function TA_CreateEntryPoint is the TA’s
 * second initialization function, called by the framework
 * when the single thread starts execution. It is called after main().
 *
 * (QNX PDK Only) Error behavior:
 *       implementation-defined
 *
 * (QNX PDK Only) Side effects:
 *      implementation-defined. Global data can be initialized.
 *
 * @return Whether the TA is successfully initialized.
 * @retval TEE_SUCCESS the TA is successfully initialized.
 * @retval impl_defined the TA cannot be be initialized.
 */
TEE_Result TA_CreateEntryPoint(void);

#ifdef CONFIG_TRUSTY
/**
 * @brief (Linux PDK Only) The function TA_DestroyEntryPoint is the
 * TA's destructor, which the Framework calls when the instance is being
 * destroyed.
 *
 * When the function TA_DestroyEntryPoint is called, the Framework
 * guarantees that no client session is currently open. Once the call
 * to TA_DestroyEntryPoint has been completed, no other entry point of
 * this instance is ever called again.
 *
 * Note that when this function is called, all resources opened by the
 * instance are still available. It is only after the function returns
 * that the implementation must start automatically reclaiming
 * resources left opened.
 *
 * This function can return no success or error code. After this
 * function returns the Implementation MUST consider the instance
 * destroyed and MUST reclaim all resources left open by the instance.
 */
void TA_DestroyEntryPoint(void);
#endif

/**
 * @brief The framework calls the function TA_OpenSessionEntryPoint when a
 * client requests to open a session with the TA.
 *
 * The client can specify parameters in an open operation which are
 * passed to the TA instance in the arguments
 * paramTypes and params. These arguments can also be used by the
 * TA instance to transfer response data back to the
 * client.
 *
 * If this function returns TEE_SUCCESS, the client is connected to a
 * TA instance and can invoke TA
 * commands. When the client disconnects, the framework
 * eventually calls the TA_CloseSessionEntryPoint entry point.
 * If the function returns any error, the framework rejects the
 * connection and returns the error code to the client.
 * The return origin is then set to TEE_ORIGIN_TRUSTED_APP.
 *
 * The TA instance can register a session data
 * pointer by setting *sessionContext. The value of this pointer is
 * not interpreted by the framework, and is simply passed back to
 * TA_InvokeCommandEntryPoint and TA_CloseSessionEntryPoint functions
 * within this session. Note that *sessionContext
 * may be set with a pointer to a memory allocated by the trusted
 * application instance or with anything else, such as an integer, a
 * handle, etc. The framework does not automatically free
 * *sessionContext when the session is closed; the TA
 * instance is responsible for freeing memory if required.
 * If the call to open session returns
 * TEE_SUCCESS, the client must consider the session as successfully
 * opened and explicitly close it if necessary.
 *
 * (QNX PDK Only) Error behavior:
 *       implementation-defined
 *
 * (QNX PDK Only) Side effects:
 *       implementation-defined. Resources might be allocated.
 *
 * @param[in] paramTypes The types of the four parameters.
 * @param[in,out] params A pointer to an array of four parameters.
 * @param[out] sessionContext A pointer to a variable that can be filled by
 *        the TA instance with an opaque void* data
 *        pointer.
 * @return if the session is successfully opened
 * @retval TEE_SUCCESS the session is successfully opened
 * @retval impl_defined the session could not be opened
 */
TEE_Result TA_OpenSessionEntryPoint(const uint32_t paramTypes,
                                    TEE_Param * const params,
                                  void ** const sessionContext);

/**
 * @brief The framework calls the function TA_CloseSessionEntryPoint
 * to close a client session.
 *
 * (QNX PDK Only) Error behavior:
 *       implementation-defined
 *
 *
 * (QNX PDK Only) Side effects:
 *     the TA implementation is responsible for freeing any resources consumed
 *     by the session. Note that the TA cannot refuse to close a session, but
 *     can hold the closing until it returns from TA_CloseSessionEntryPoint.
 *     This is why this function cannot return an error code.
 *
 * @param[in] sessionContext The value of the void* opaque data pointer
 *        set by the TA in the function
 *        TA_OpenSessionEntryPoint for this session.
 */
void TA_CloseSessionEntryPoint(void * const sessionContext);

/**
 * @brief The framework calls the function TA_InvokeCommandEntryPoint when
 * the client invokes a command within the given session.
 *
 * The TA can access the parameters sent by the client
 * through the paramTypes and params arguments. It can also use these
 * arguments to transfer response data back to the client.
 *
 * If the function returns any error, the return origin is then set
 * to TEE_ORIGIN_TRUSTED_APP.
 *
 * (QNX PDK Only) Error behavior:
 *       implementation-defined
 *
 * (QNX PDK Only) Side effects:
 *       implementation-defined. Resources might be updated.
 *
 * @param[in] sessionContext The value of the void* opaque data pointer
 *        set by the TA in the function
 *        TA_OpenSessionEntryPoint.
 * @param[in] commandID A TA-specific code that
 *        identifies the command to be invoked.
 * @param[in] paramTypes The types of the four parameters.
 * @param[in,out] params A pointer to an array of four parameters.
 * @return if the session is successfully opened
 * @retval TEE_SUCCESS the session is successfully opened
 * @retval impl_defined the session could not be opened
 */
TEE_Result TA_InvokeCommandEntryPoint(void * const sessionContext,
                                        const uint32_t commandID,
                                       const uint32_t paramTypes,
                                       TEE_Param* const params);

/** @} */
#endif /* TEE_INTERNAL_TA_INTERFACE_H */
