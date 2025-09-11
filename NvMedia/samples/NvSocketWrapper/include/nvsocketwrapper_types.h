/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NVSOCKETWRAPPER_TYPES_HEADER_INCLUDE
#define NVSOCKETWRAPPER_TYPES_HEADER_INCLUDE

namespace NvSocketWrapper
{
    enum class ErrorCode
    {
        E_SUCCESS,
        E_ENDPOINT,
        E_NOT_RUNNING,
        E_RUNNING,
        E_TIMEOUT,
        E_INTERRUPTED,
        E_SHUTDOWN,
        E_SERVER_TEARDOWN,
        E_CONNECTION_REFUSED
    };

    enum class TPType
    {
        E_UDP,
        E_TCP
    };
}

#endif // NVSOCKETWRAPPER_TYPES_HEADER_INCLUDE
