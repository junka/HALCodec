/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2017-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: GP Client API</b>
 *
 * @b Description: These functions support the NVIDIA implementation of the GP Client API. Since
 * the implementation is a wrapper around OTE, they largely convert between GP
 * and OTE data types.
 */

/**
 * @defgroup global_platform_client Utility Functions
 *
 * Supports the NVIDIA implementation of the GP Client API.
 * @ingroup grp_tee_lib_nv
 * @{
 */

#ifndef __CLIENT_OTE_WRAPPER_UTILS_H
#define __CLIENT_OTE_WRAPPER_UTILS_H

#include <common/ote_command.h>
#include <common/ote_common.h>
#include <common/ote_error.h>
#include <common/tos_common_utils.h>

#include <tee_client/tee_client_api.h>

/*
 * These functions support the NVIDIA implementation of the GP Client API. Since
 * the implementation is a wrapper around OTE, they largely convert between GP
 * and OTE data types.
 */

/*! Initializes a TEEC_Session.
*
* \param[in] session A pointer to a valid uninitialized TEEC_Session.
*
* \return OTE_SUCCESS Indicates the initialization was successful.
*/
te_error_t init_session(TEEC_Session *session);

/*! Packs TEEC_Operation and commandID to a te_operation_t
*
* \param[in] teec_op A pointer to valid, fully defined TEEC_Operation.
* \param[in] commandId The commandId of the desired TA command.
* \param[out] te_op A pointer to a valid, initialized te_operation_t.
*
* \return OTE_SUCCESS Indicates the conversion was successful.
*/
te_error_t teec_operation_to_te_operation_t(TEEC_Operation *teec_op,
                                                 uint32_t commandId,
                                              te_operation_t *te_op);

/*! Copies back data from te_operation_t to TEEC_Operation
*
* \param[in] te_op A pointer to a te_operation_t which has previously been
*              populated by teec_operation_to_te_operation_t.
* \param[out] teec_op A pointer to the TEEC_Operation from which te_op was
*              converted.
*
* \return OTE_SUCCESS Indicates the copy-back was successful.
*/
te_error_t copy_back_te_operation_t_to_teec_operation(te_operation_t *te_op,
                                                    TEEC_Operation *teec_op);

/*! Converts a TEEC_UUID to a te_service_id_t.
*
* \param[in] teec_id A pointer to a valid TEEC_UUID.
* \param[out] te_id A pointer to valid te_service_id_t.
*
* \return OTE_SUCCESS Indicates the conversion was successful.
*/
te_error_t teec_uuid_to_te_service_id_t(const TEEC_UUID *teec_id, te_service_id_t *te_id);

/*! Returns the TEEC_Result corresponding to a given te_error_t.
*
* \param[in] err The te_error_t which the caller desires to convert into a
*   TEEC_Result.
*
* \return The TEEC_Result corresponding to err.
*
* \return OTE_SUCCESS Indicates the conversion was successful.
*/
TEEC_Result te_error_t_to_teec_result(te_error_t err);

/*! Returns the GP result origin code corresponding to a te_result_origin_t.
*
* \param[in] ro The te_result_origin_t which the caller desires to convert into
*   a GP client result origin code.
*
* \return The uint32_t result origin code corresponding to ro.
*/
uint32_t te_result_origin_t_to_teec_origin(te_result_origin_t ro);

/** @} */
#endif /* __CLIENT_OTE_WRAPPER_UTILS_H */
