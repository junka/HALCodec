/*
 * Copyright (c) 2016-2017 NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */
#ifndef __OTE_SERVICE_UTILS_H
#define __OTE_SERVICE_UTILS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <trusty_std.h>
#include <errno.h>

#include <common/ote_command.h>
#include <common/ote_common.h>
#include <common/serialization/context.h>

/*
 * With the current architecture, maximum 2 buffers can be exchanged over a
 * channel at a time. One buffer for OTE header(te_operation_container_t)
 * and other for actual data.
 */
#define MAX_NUM_RECV_BUFFERS    2

/* Need to change if we are passing buffers with size more than one page */
#define MAX_RECV_BUFFER_SIZE    4096

typedef void (*event_handler_t) (const uevent_t *ev);
typedef struct {
        event_handler_t handler;
        void* ctx;
} event_cookie_t;

typedef struct {
        uint32_t handle;
        uint32_t num_sessions;
        char name[MAX_PORT_NAME_LENGTH];
        event_cookie_t *cookie;
} port_state_t;

typedef struct {
        uint32_t handle;
        void *ctx; /*private context of TA*/
        event_cookie_t *cookie;
        port_state_t *port_state;
        bool is_session_opened;
        te_service_id_t peer_uuid;
} chan_state_t;

/*!Receives the te_operation object from client
 *
 * \param[in] chan_handle Handle to the channel established between
 *     TA and client
 * \param[in,out] operation A pointer to the operation object
 *     that is expected to be received on this channel
 * \param[in] sc serialization context used to store the received serialization
 *     buffer
 * \param[in] ns_client True value of this parameter indicates that the
 *            chan_handle belongs to a NS world application
 * \return NO_ERROR Indicates the operation was successful.
 */
int tipc_recv_op(int chan_handle, te_operation_t* operation, serialized_context_t *sc, bool ns_client);

/*!Send the te_operation object to client
 *
 * \param[in] chan_handle Handle to the channel established between
 *     TA and client
 * \param[in] operation A pointer to the operation object
 *     that need to be sent to client
 * \param[in] sc serialization context used to store the constructed serialization
 *     buffer
 * \return NO_ERROR Indicates the operation was successful.
 */
int tipc_send_op(int chan_handle, te_operation_t* operation, serialized_context_t *sc);

#endif
