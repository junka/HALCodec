/*
 * Copyright (c) 2019-2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file
 *
 * @brief NVIDIA DRIVE&reg; Update DU-Transport library interface.
 */

#ifndef DUTRANSPORT_H_
#define DUTRANSPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Global Includes --------------------------------- */
#include <net/if.h>

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"

/*!
 * @defgroup du_transport_group Transport API
 *
 * NVIDIA DRIVE&reg; Update DU-Transport library interface.
 *
 * @ingroup drive_update_top
 * @{
 */
/* ------------------------ Defines------------------------------------------ */

/*! \brief Defines the max DU-Transport name string size. */
#define DUTR_NAME_MAX   (256U)

/* ------------------------ Types ------------------------------------------- */

/*!
 * Defines DU-Transport back-end transport protocols.
 */
typedef enum DUTR_TR_TYPE
{
    /*! IVC based inter-VM communication. Deprecated soon */
    DUTR_TR_TYPE_IVC      = 0,
    /*! NVSCIIPC lib. */
    DUTR_TR_TYPE_NVSCI,
    /*! TCP based data transport, available on standard build only. */
    DUTR_TR_TYPE_TCP,
} DUTR_TR_TYPE;

/*!
 * Defines DU-Transport security protocols.
 */
typedef enum DUTR_SEC_TYPE
{
    /*! Security type not yet defined. */
    DUTR_SEC_TYPE_NONE = 0,
    /*! TLS security type. */
    DUTR_SEC_TYPE_TLS,
} DUTR_SEC_TYPE;

/*!
 * Defines DU-Transport security parameters.
 */
typedef uint32_t DUTR_SEC_NONE_PARAM;
typedef uint32_t DUTR_SEC_TLS_PARAM;

typedef void *PDUTR_SEC_PARAM;

/*!
 * Defines DU-Transport IVC parameters.
 */
typedef struct DUTR_IVC_PARAM
{
    /// The name string to identify an IVC channel.
    char name[DUTR_NAME_MAX];
} DUTR_IVC_PARAM, *PDUTR_IVC_PARAM;

/*!
 * Defines DU-Transport NVSCI parameters.
 */
typedef struct DUTR_NVSCI_PARAM
{
    /// The name string to identify an NVSCI endpoint.
    char ep[2][DUTR_NAME_MAX];
} DUTR_NVSCI_PARAM, *PDUTR_NVSCI_PARAM;

/*!
 * Defines the role of TCP connection.
 */
typedef enum DUTR_TCP_ROLE
{
    /// The server waiting for client connection.
    DUTR_TCP_SERVER = 0,
    /// The client to connect to remote server.
    DUTR_TCP_CLIENT,
    /// Invalid type
    DUTR_TCP_INVALID,
} DUTR_TCP_ROLE;

/*!
 * Defines the IPV4 IP address string size.
 */
#define DUTR_IPV4_ADDR_STR_SIZE (16U)
/*!
 * Defines the structure of TCP level address.
 */
typedef struct DUTR_TCP_IF_PARAM
{
    /// IPV4 IP address string.
    char      ip[DUTR_IPV4_ADDR_STR_SIZE];
    /// Port to be used.
    uint16_t  port;
} DUTR_TCP_IF_PARAM, *PDUTR_TCP_IF_PARAM;

/*!
 * Defines the interface name string size.
 */
#define DUTR_IFNAME_MAX IFNAMSIZ
/*!
 * Defines the parameter for TCP transport protocol.
 */
typedef struct DUTR_TCP_PARAM
{
    /// String of interface name.
    char               ifName[DUTR_IFNAME_MAX];
    /// The address bound to a local socket.
    DUTR_TCP_IF_PARAM  local;
    /// The remote server address to connect, used only for DUTR_TCP_CLIENT.
    DUTR_TCP_IF_PARAM  remote;
    /// Specify if it is the server or client.
    DUTR_TCP_ROLE      role;
} DUTR_TCP_PARAM, *PDUTR_TCP_PARAM;

/*!
 * Holds the pointer to parameter of DU-Transport back-end protocols.
 */
typedef void *PDUTR_TR_PARAM;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // DUTRANSPORT_H_
