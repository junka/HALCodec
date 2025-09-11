/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NV_TE_PSCFW_CONSTANTS_H
#define NV_TE_PSCFW_CONSTANTS_H


//Pscfw Service commands.
typedef enum {

    /// \brief Invoke a PSC task via mailbox. Wait for task to complete
    /// and return status to caller.
    /// \param[in] params[0].value.a [input]: Opcode of the task
    /// \param[in] params[0].value.b [input]: Command ID of the task
    /// \param[in] params[1].value   = Address in shared memory buffer where
    ///                     paramTypes and GP params for the PSC task is
    ///                     written. The first 4 bytes of the buffer
    ///                     should be populated with paramTypes for the
    ///                     PSC task and the remaining contain GP params
    ///                     for the PSC task. \n
    ///           value.a = Lower 4 bytes of buffer address \n
    ///           value.b = Higher 4 bytes of buffer address
    /// \param[in] params[2].value.a = Size of buffer pointed by params[1]
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_GENERIC if PSC task fails sanity check.
    /// \return The error code returned by the PSC task, if the task fails.
    PSCFW_SERVICE_SEND          = 0x01,

    /// \brief Wait for previous PSC task to complete,
    /// read and clear mailbox OUT registers, and return status to caller.
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \return  TEE_SUCCESS if success.
    /// \return Return result of the previous PSC task, if task not success.
    PSCFW_SERVICE_RECV_ONLY     = 0x02,

    TOS_MISRA_CPP_PD_0_1_5_TID_555
} NV_TE_PscfwServiceOperation;

#endif /* NV_TE_PSCFW_CONSTANTS_H */
