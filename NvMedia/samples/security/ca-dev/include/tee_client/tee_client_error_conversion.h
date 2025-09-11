//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// NVIDIA CORPORATION and its licensors retain all intellectual property
// and proprietary rights in and to this software, related documentation
// and any modifications thereto.  Any use, reproduction, disclosure or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA CORPORATION is strictly prohibited.
//

#ifndef TEE_CLIENT_ERROR_CONVERSION_H
#define TEE_CLIENT_ERROR_CONVERSION_H

#include "errno.h"
#include "tee_client/tee_client_constants.h"

///
/// \brief Return the TEE client result version of Linux error
///
static inline TEEC_Result LinuxErrorToTeecResult(const int error) {
    TEEC_Result result;

    switch (error) {
        case 0:
            result = TEEC_SUCCESS;
            break;
        case -EPERM:
            result = TEEC_ERROR_SECURITY;
            break;
        case -ENOENT:
            result = TEEC_ERROR_ITEM_NOT_FOUND;
            break;
        case -E2BIG:
            result = TEEC_ERROR_EXCESS_DATA;
            break;
        case -EAGAIN:
            result = TEEC_ERROR_CANCEL;
            break;
        case -ENOMEM:
            result = TEEC_ERROR_OUT_OF_MEMORY;
            break;
        case -EACCES:
            result = TEEC_ERROR_ACCESS_DENIED;
            break;
        case -EFAULT:
            result = TEEC_ERROR_ACCESS_CONFLICT;
            break;
        case -EBUSY:
            result = TEEC_ERROR_BUSY;
            break;
        case -EINVAL:
            result = TEEC_ERROR_BAD_PARAMETERS;
            break;
        case -ENOSPC:
            result = TEEC_ERROR_OUT_OF_MEMORY;
            break;
        case -ENOSYS:
            result = TEEC_ERROR_NOT_IMPLEMENTED;
            break;
        case -ENOMSG:
            result = TEEC_ERROR_NO_DATA;
            break;
        case -ENODATA:
            result = TEEC_ERROR_NO_DATA;
            break;
        case -EBADMSG:
            result = TEEC_ERROR_BAD_FORMAT;
            break;
        case -EOVERFLOW:
            result = TEEC_ERROR_BAD_PARAMETERS;
            break;
        case -EUSERS:
            result = TEEC_ERROR_GENERIC;
            break;
        case -EOPNOTSUPP:
            result = TEEC_ERROR_NOT_SUPPORTED;
            break;
        case -ECONNRESET:
            result = TEEC_ERROR_COMMUNICATION;
            break;
        case -ENOBUFS:
            result = TEEC_ERROR_SHORT_BUFFER;
            break;
        case -ENOTCONN:
            result = TEEC_ERROR_BAD_STATE;
            break;
        case -ETIMEDOUT:
            result = TEEC_ERROR_BUSY;
            break;
        case -EHOSTDOWN:
            result = TEEC_ERROR_BAD_STATE;
            break;
        case -EINPROGRESS:
            result = TEEC_ERROR_BUSY;
            break;
        case -ECANCELED:
            result = TEEC_ERROR_CANCEL;
            break;
        default:
            result = TEEC_ERROR_GENERIC;
            break;
    }

    return result;
}

///
/// \brief Return the Linux error version of TEE client result
///
static inline int TeecResultToLinuxError(const TEEC_Result result) {
    int error;

    switch (result) {
        case TEEC_SUCCESS:
            error = 0;
            break;
        case TEEC_ERROR_GENERIC:
            error = -EUSERS;
            break;
        case TEEC_ERROR_ACCESS_DENIED:
            error = -EACCES;
            break;
        case TEEC_ERROR_CANCEL:
            error = -ECANCELED;
            break;
        case TEEC_ERROR_ACCESS_CONFLICT:
            error = -EFAULT;
            break;
        case TEEC_ERROR_EXCESS_DATA:
            error = -E2BIG;
            break;
        case TEEC_ERROR_BAD_FORMAT:
            error = -EBADMSG;
            break;
        case TEEC_ERROR_BAD_PARAMETERS:
            error = -EINVAL;
            break;
        case TEEC_ERROR_BAD_STATE:
            error = -ENOTCONN;
            break;
        case TEEC_ERROR_ITEM_NOT_FOUND:
            error = -ENOENT;
            break;
        case TEEC_ERROR_NOT_IMPLEMENTED:
            error = -ENOSYS;
            break;
        case TEEC_ERROR_NOT_SUPPORTED:
            error = -EOPNOTSUPP;
            break;
        case TEEC_ERROR_NO_DATA:
            error = -ENODATA;
            break;
        case TEEC_ERROR_OUT_OF_MEMORY:
            error = -ENOMEM;
            break;
        case TEEC_ERROR_BUSY:
            error = -EBUSY;
            break;
        case TEEC_ERROR_COMMUNICATION:
            error = -ECONNRESET;
            break;
        case TEEC_ERROR_SECURITY:
            error = -EPERM;
            break;
        case TEEC_ERROR_SHORT_BUFFER:
            error = -ENOBUFS;
            break;
        default:
            error = -EUSERS;
            break;
    }

    return error;
}

#endif // TEE_CLIENT_ERROR_CONVERSION_H
