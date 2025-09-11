//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// NVIDIA CORPORATION and its licensors retain all intellectual property
// and proprietary rights in and to this software, related documentation
// and any modifications thereto.  Any use, reproduction, disclosure or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA CORPORATION is strictly prohibited.
//

#ifndef TEE_INTERNAL_ERROR_CONVERSION_H
#define TEE_INTERNAL_ERROR_CONVERSION_H

#include "errno.h"
#include "tee_internal/tee_internal_constants.h"

///
/// \brief Return the TEE result version of Linux error
///
static inline TEE_Result LinuxErrorToTeeResult(const int error) {
    TEE_Result result;

    switch (error) {
        case 0:
            result = TEE_SUCCESS;
            break;
        case -EPERM:
            result = TEE_ERROR_SECURITY;
            break;
        case -ENOENT:
            result = TEE_ERROR_ITEM_NOT_FOUND;
            break;
        case -E2BIG:
            result = TEE_ERROR_EXCESS_DATA;
            break;
        case -EAGAIN:
            result = TEE_ERROR_CANCEL;
            break;
        case -ENOMEM:
            result = TEE_ERROR_OUT_OF_MEMORY;
            break;
        case -EACCES:
            result = TEE_ERROR_ACCESS_DENIED;
            break;
        case -EFAULT:
            result = TEE_ERROR_ACCESS_CONFLICT;
            break;
        case -EBUSY:
            result = TEE_ERROR_BUSY;
            break;
        case -EINVAL:
            result = TEE_ERROR_BAD_PARAMETERS;
            break;
        case -ENOSPC:
            result = TEE_ERROR_STORAGE_NO_SPACE;
            break;
        case -ENOSYS:
            result = TEE_ERROR_NOT_IMPLEMENTED;
            break;
        case -ENOMSG:
            result = TEE_ERROR_NO_DATA;
            break;
        case -ENODATA:
            result = TEE_ERROR_NO_DATA;
            break;
        case -EBADMSG:
            result = TEE_ERROR_BAD_FORMAT;
            break;
        case -EOVERFLOW:
            result = TEE_ERROR_OVERFLOW;
            break;
        case -EUSERS:
            result = TEE_ERROR_GENERIC;
            break;
        case -EOPNOTSUPP:
            result = TEE_ERROR_NOT_SUPPORTED;
            break;
        case -ECONNRESET:
            result = TEE_ERROR_COMMUNICATION;
            break;
        case -ENOBUFS:
            result = TEE_ERROR_SHORT_BUFFER;
            break;
        case -ENOTCONN:
            result = TEE_ERROR_BAD_STATE;
            break;
        case -ETIMEDOUT:
            result = TEE_ERROR_TIMEOUT;
            break;
        case -EHOSTDOWN:
            result = TEE_ERROR_TARGET_DEAD;
            break;
        case -EINPROGRESS:
            result = TEE_PENDING;
            break;
        case -ECANCELED:
            result = TEE_ERROR_CANCEL;
            break;
        default:
            result = TEE_ERROR_GENERIC;
            break;
    }

    return result;
}

///
/// \brief Return the Linux error version of TEE result
///
static inline int TeeResultToLinuxError(const TEE_Result result) {
    int error;

    switch (result) {
        case TEE_SUCCESS:
            error = 0;
            break;
        case TEE_ERROR_GENERIC:
            error = -EUSERS;
            break;
        case TEE_ERROR_ACCESS_DENIED:
            error = -EACCES;
            break;
        case TEE_ERROR_CANCEL:
            error = -ECANCELED;
            break;
        case TEE_ERROR_ACCESS_CONFLICT:
            error = -EFAULT;
            break;
        case TEE_ERROR_EXCESS_DATA:
            error = -E2BIG;
            break;
        case TEE_ERROR_BAD_FORMAT:
            error = -EBADMSG;
            break;
        case TEE_ERROR_BAD_PARAMETERS:
            error = -EINVAL;
            break;
        case TEE_ERROR_BAD_STATE:
            error = -ENOTCONN;
            break;
        case TEE_ERROR_ITEM_NOT_FOUND:
            error = -ENOENT;
            break;
        case TEE_ERROR_NOT_IMPLEMENTED:
            error = -ENOSYS;
            break;
        case TEE_ERROR_NOT_SUPPORTED:
            error = -EOPNOTSUPP;
            break;
        case TEE_ERROR_NO_DATA:
            error = -ENODATA;
            break;
        case TEE_ERROR_OUT_OF_MEMORY:
            error = -ENOMEM;
            break;
        case TEE_ERROR_BUSY:
            error = -EBUSY;
            break;
        case TEE_ERROR_COMMUNICATION:
            error = -ECONNRESET;
            break;
        case TEE_ERROR_SECURITY:
            error = -EPERM;
            break;
        case TEE_ERROR_SHORT_BUFFER:
            error = -ENOBUFS;
            break;
        case TEE_PENDING:
            error = -EINPROGRESS;
            break;
        case TEE_ERROR_TIMEOUT:
            error = -ETIMEDOUT;
            break;
        case TEE_ERROR_OVERFLOW:
            error = -EOVERFLOW;
            break;
        case TEE_ERROR_TARGET_DEAD:
            error = -EHOSTDOWN;
            break;
        case TEE_ERROR_STORAGE_NO_SPACE:
            error = -ENOSPC;
            break;
        default:
            error = -EUSERS;
            break;
    }

    return error;
}

#endif // TEE_INTERNAL_ERROR_CONVERSION_H
