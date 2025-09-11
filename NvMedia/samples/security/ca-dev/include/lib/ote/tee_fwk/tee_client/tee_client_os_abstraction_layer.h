/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __TEE_CLIENT_OS_ABSTRACTION_LAYER_H
#define __TEE_CLIENT_OS_ABSTRACTION_LAYER_H

#include <common/ote_common.h>

/*
 * This type denotes a TEE Context, the main logical
 * container linking a Client Application with a particular
 * TEE. Its content is entirely implementation-defined.
 */
typedef struct
{
    /* Implementation-Specific Fields */
} TEEC_Context;

/*
 * This type denotes a TEE Session, the logical container
 * linking a Client Application with a particular Trusted
 * Application. Its content is entirely implementation-defined.
 */
typedef struct
{
    /* Implementation-Specific Fields */
    te_session_t te_session;
} TEEC_Session;

#endif /* __TEE_CLIENT_OS_ABSTRACTION_LAYER_H */
