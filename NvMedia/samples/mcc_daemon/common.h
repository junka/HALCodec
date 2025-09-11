/*
 * Copyright (c) 2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef MCC_DAEMON_COMMON_H__
#define MCC_DAEMON_COMMON_H__

/* Header File for MCC Error codes and Logging */

/* Macro to define max number of NvSciIpc channels */
#define MAX_NUM_CHANNELS (3)

/* Macro to define max number of NvSocket ports */
#define MAX_NUM_PORTS (2)

/* Different MccDaemon Status/Error Codes */
typedef enum MccDaemonStatus
{
    MccDaemonStatusOK                = 0x00000000,
    MccDaemonStatusNotImplemented    = 0x00000001,
    MccDaemonStatusOOM               = 0x00000002,
    MccDaemonStatusPending           = 0x00000003,
    MccDaemonStatusNotReady          = 0x00000004,
    MccDaemonStatusTimeout           = 0x00000005,
    MccDaemonStatusError             = 0x00000006,
    MccDaemonStatusBadParameter      = 0x00000007,
} MccDaemonStatus_t;

#ifdef MCC_VERBOSE
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...) (void)(0)
#endif

#endif /*MCC_DAEMON_COMMON_H__*/
