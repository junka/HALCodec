/*
 * Copyright (c) 2013-2019, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Service Interface</b>
 *
 * @b Description: Declares data types and functions for
 *                 the TLK services.
 *
 */

/**
 * @defgroup tlk_services_interface Interface
 *
 * Defines Trusted Application (TA) services declarations and functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_SERVICE_H
#define __OTE_SERVICE_H

#include <stdio.h>

#include <sys/types.h>
#include <common/ote_common.h>
#include <common/ote_ioctl.h>

#ifdef CONFIG_TRUSTY
#include <trusty_ipc.h>
#endif

#define TE_PRINTF_MAX_SIZE		512
#define MAX_NUM_SUPPORTED_GUESTS	16U
#define DEFAULT_GUEST_ID		0xDEADFEED

/* macros to help manage debug output */
#define LIBTA_CRITICAL(args...)		te_fprintf(TE_CRITICAL, args)
#define LIBTA_ERR(args...)		te_fprintf(TE_ERR, args)
#define LIBTA_INFO(args...)		te_fprintf(TE_INFO, args)
#define LIBTA_SECURE(args...)		te_fprintf(TE_SECURE, args)

void te_exit_service(void);

enum {
	CREATE_INSTANCE		= 1UL,
	DESTROY_INSTANCE	= 2UL,
	OPEN_SESSION		= 3UL,
	CLOSE_SESSION		= 4UL,
	LAUNCH_OPERATION	= 5UL,
	HANDLE_PM_EVENTS	= 6UL,
	HANDLE_DIRECT_EVENT	= 7UL,
};

typedef enum {
	PM_EVENT_RESUME = 1,
	PM_EVENT_SUSPEND = 2,
	PM_EVENT_POWEROFF = 3,
} te_pm_event_t;

typedef enum {
	DIRECT_EVENT_RPMB = 16,
} te_direct_event_t;

/**
 * Holds the layout of the \c te_oper_param_t structures which must
 * match the layout sent in by the non-secure (NS) world via the
 * TrustZone Secure Monitor Call (TZ SMC) path.
 */
typedef struct {
	uint32_t type;
	uint32_t session_id;
	uint32_t command_id;
	cmnptr_t params;
	uint32_t params_size;
	uint32_t dest_uuid[4];
	uint32_t result;
	uint32_t result_origin;
} te_request_t;

typedef struct {
	te_request_t request;
} te_ta_to_ta_request_args_t;

typedef struct {
	uint32_t   type;
	te_error_t result;
	cmnptr_t   context;
	uint32_t   command_id;
	cmnptr_t   params;
	uint32_t   params_size;
} te_entry_point_message_t;

/*
 * Service Interfaces
 */

/*! Initializes the service. */
te_error_t te_init(int argc, char **argv);

/*! Deinitializes the service. */
void te_destroy(void);

/*! Creates a new instance of the service. */
te_error_t te_create_instance_iface(void);

/*! Destroys an instance of the service. */
void te_destroy_instance_iface(void);

/*! Opens a session.
 *
 * \param sctx A pointer to the session.
 * \param oper A pointer to the operation.
 */
te_error_t te_open_session_iface(void **sctx, te_operation_t *oper);

/*! Closes an opened session.
 *
 * \param sctx A pointer to the session to close.
 */
void te_close_session_iface(void* sctx);

/*! Receives an operation.
 *
 * \param sctx A pointer to the session from which to receive
 *     the operation.
 * \param oper A pointer to the operation.
 */
te_error_t te_receive_operation_iface(void *sctx, te_operation_t *oper);

/*! Gets the instance context data. */
void* ote_get_instance_data(void);

/*! Sets an instance context data. */
void ote_set_instance_data(void*sessionContext);

/*! Holds the identity of a client/caller. */
typedef struct
{
	uint32_t login;
	te_service_id_t uuid;
} te_identity_t;

/*! Defines the supported login types. */
enum {
	TE_LOGIN_PUBLIC	= 0,
	TE_LOGIN_TA	= 7,
};

/*! Defines the type of property data. */
enum {
	TE_PROP_DATA_TYPE_UUID		= 1,
	TE_PROP_DATA_TYPE_IDENTITY	= 2,
};

/*! Defines the property data information. */
typedef enum {
	TE_PROPERTY_CURRENT_TA		= 0xFFFFFFFF,
	TE_PROPERTY_CURRENT_CLIENT	= 0xFFFFFFFE,
	TE_PROPERTY_TE_IMPLEMENTATION	= 0xFFFFFFFD,
} te_property_type_t;

/*! Holds data about the TA client. */
typedef struct {
	te_property_type_t prop;		/**< Holds the TE_PROPERTY_* value. */
	uint32_t data_type;			/**< Holds the data type of property. */
	union {
		te_service_id_t uuid;
		te_identity_t identity;
	} value;				/**< Holds the return value. */
	size_t value_size;			/**< Holds the size of return value. */
	te_error_t result;
} te_get_property_args_t;

/*!
 * Gets the service ID for the current Trusted Application (TA).
 *
 * \param[out] value A pointer to ::te_service_id_t, which holds the service ID.
 *
 * \return OTE_SUCCESS to indicate the operation was successful.
 */
te_error_t te_get_current_ta_uuid(te_service_id_t *value);

/*!
 * Gets the current client's identity only if it is a secure TA.
 *
 * \param[out] value A pointer to ::te_identity_t, which holds the client's identity.
 *
 * \return OTE_SUCCESS to indicate that the operation was successful.
 */
te_error_t te_get_client_ta_identity(te_identity_t *value);

/*!
 * Gets the current client's identity.
 *
 * \param[out] value A pointer to ::te_identity_t, which holds the client's
 *                   identity.
 *
 * \return OTE_SUCCESS to indicate that the operation was successful.
 */
te_error_t te_get_client_identity(te_identity_t *value);

/*!
 * Converts a UUID to string format.
 *
 * \param[in] uuid  A pointer to the UUID to convert.
 *
 * \return  A pointer to the UUID in string format.
 */
char *uuid_to_str(te_service_id_t *uuid);

/*!
 * Validates the client's identity.
 *
 * \param[in] service_id  A pointer to a service ID structure to be used to
 *                        validate the identity.
 *
 * \return OTE_SUCCESS to indicate that the operation was successful.
 */
te_error_t check_client_uuid(te_service_id_t *service_id);

/*!
 * Validates client's login scheme with the expected login type.
 *
 * \param[in]  login        The expected login type.
 * \param[out] client_id_p  A pointer to the location where the login type is
 *                          stored if login is successful.
 * \return OTE_SUCCESS to indicate that the operation was successful.
 */
te_error_t check_client_login(uint32_t login, te_identity_t *client_id_p);

/*!
 * Returns the current guest id requesting service from this TA.
 *
 * \param[out] guest_id  Guest id which is requesting service from the TA.
 *                       In the native non-hypervisor case, 0 is returned.
 *
 * \return OTE_SUCCESS to indicate that the operation was successful.
 * \return OTE_ERROR_BAD_PARAMETERS to indicate guest_id pointer is NULL.
 */
te_error_t te_get_current_guest_id(uint32_t *guest_id);

#define DEVICE_UID_SIZE_BYTES		16

/*! Holds the device unique ID. */
typedef struct {
	uint8_t id[DEVICE_UID_SIZE_BYTES];
} te_device_unique_id;

/*! Gets the device's unique ID.
 *
 * \param[out] uid  A pointer to a location where the device's unique ID is to
 *                  be stored.
 */
te_error_t te_get_device_unique_id(te_device_unique_id *uid);

/*! Gets whether the device has DSI panel attached or not. */
te_error_t te_get_dsi_panel_config(bool *res);

#ifdef CONFIG_TRUSTY

/* Holds the root of trust for Trusty */
#define RSA2K_NUM_BYTES   256

typedef struct {
	uint8_t dtb_pub_key[RSA2K_NUM_BYTES];
	uint8_t boot_pub_key[RSA2K_NUM_BYTES];
	uint8_t verified_boot_state;
} te_root_of_trust;

#else

#define VERIFIED_BOOT_KEY_SIZE_BYTES	256

/*! Holds the root of trust for TLK. */
typedef struct {
	uint8_t verified_boot_key[VERIFIED_BOOT_KEY_SIZE_BYTES];
	uint8_t verified_boot_state;
} te_root_of_trust;

#endif

/* Verified boot state definitions */
#define VERIFIED_BOOT_UNKNOWN_STATE 0
#define VERIFIED_BOOT_RED_STATE 1
#define VERIFIED_BOOT_YELLOW_STATE 2
#define VERIFIED_BOOT_GREEN_STATE 3
#define VERIFIED_BOOT_ORANGE_STATE 4
#define VERIFIED_BOOT_FLAGS_MASK (1 << 7)
#define VERIFIED_BOOT_IS_AVB_2_FLAG (1 << 7)

/*! Gets the root of trust.
 *
 * \param [out] r_o_t  A pointer to the root of trust.
 */
te_error_t te_get_root_of_trust (te_root_of_trust *r_o_t);

/*! Holds the panic information. */
#define OTE_PANIC_MSG_MAX_SIZE	128
typedef struct {
	char msg[OTE_PANIC_MSG_MAX_SIZE+1];
} te_panic_args_t;

/*! Panics the system.
 *
 * This call does not return.
 *
 * \param msg  A pointer to a string containing a panic message.
 */
void te_panic(char *msg) __attribute__ ((noreturn));

/*! Defines the maximum length of the "[task_name] " prefix for the te_fprintf() task log entries.
 */
#define OTE_TE_FPRINTF_PREFIX_MAX_LENGTH (OTE_TASK_NAME_MAX_LENGTH + 4)

/*! Sets a printable prefix string that te_fprintf() outputs in front of
 * every log message from this task.
 *
 * The OTE library automatically sets a "[task_name] " log prefix based on the task name set
 * in the task manifest (if the manifest defines a task name).
 *
 * @param [in] prefix The string to use for the prefix or NULL for no prefix.
 *                    The maximum length of \a prefix is ::OTE_TE_FPRINTF_PREFIX_MAX_LENGTH.
 *                    A NULL value cancels the log prefix; a non-null string changes the prefix.
 */
void te_fprintf_set_prefix(const char *prefix);

/*! Prints out the list of parameters for debugging.
*
* Prints out the list of parameters with the parameter content.
*
* \param[in] param A pointer to a TLK operation.
*/
void te_oper_dump_param(te_oper_param_t *param);

/*! Prints out the list of parameters for debugging.
*
* Prints out the list of parameters with the parameter content.
*
* \param[in] te_op A pointer to a TLK operation.
*/
void te_oper_dump_param_list(te_operation_t *te_op);

typedef struct {
	te_pm_event_t event_id;
	uint32_t cmd;
	void *buffer;
	uint32_t len;
} ta_event_args_t;

typedef te_error_t (*ta_event_handler_t)(ta_event_args_t *args);

/* Use events_mask to only register for selected events.  */
te_error_t te_register_ta_event_handler(ta_event_handler_t handler,
		uint32_t events_mask);

/*! Checks if the client TA is the owner of the keyslot it wants
 * to write to.
 * \param keyslot_type  The keyslot type: RSA or AES.
 * \param keyslot_idx   Index of the keyslot.
 * \param uuid          A pointer to the client TA's UUID.
 * \return OTE_SUCCESS if the owner is the TA, or OTE_ERROR_ACCESS_DENIED if
 *  the owner is someone else.
 */
te_error_t te_check_se_keyslot_owner(uint32_t keyslot_type, uint32_t keyslot_idx,
			te_service_id_t *uuid);
/*! Returns the index of a free SE keyslot if one is available.
 * \param keyslot_type  Keyslot type: RSA or AES.
 * \param keyslot_idx   A pointer to a location where a free keyslot index
 *                      should be stored.
 * \return OTE_SUCCESS if the operation was successful, or
 *   OTE_ERROR_ITEM_NOT_FOUND if no keyslot was available.
 */
te_error_t te_request_se_keyslot(uint32_t keyslot_type, uint32_t *keyslot_idx);

/*! Releases a keyslot. A TA normally releases a keyslot when done writing to
 *  it.
 * \param keyslot_type  Keyslot type: RSA or AES.
 * \param keyslot_idx   Index of the keyslot.
 * \return OTE_SUCCESS to indicate that the operation was successful.
 */
te_error_t te_release_se_keyslot(uint32_t keyslot_type, uint32_t keyslot_idx);

#define LOG_PREFIX_CRITICAL	'C'
#define LOG_PREFIX_INFO		'I'
#define LOG_PREFIX_ERR		'E'
#define LOG_PREFIX_SECURE	'S'

/*! Maps the fd level to a corresponding logger prefix character
 * \param fd File Descriptor
 * \param log_level pointer to a character
 */
void convert_fd_to_log_prefix(int fd, char *log_level);

/* Defines rollback checks supported by TLK */
typedef enum {
	TE_ROLLBACK_EKS2_VERSION = 1,
} te_rollback_fields;

/*
 * Defines rollback message format.
 * @rollback_field [in] type of rollback to check against
 * @enabled [out] gets the enabled field in rollback structure
 * @version [out] gets the rollback version
 * @value [out] value of the field in bootloader rollback structure
 */
typedef struct {
	te_rollback_fields rollback_field;
	uint8_t enabled;
	uint8_t version;
	uint32_t value;
} te_rollback_args_t;

/*
 * Makes an IOCTL request to the kernel to verify a rollback value.
 * @param rb_args  A rollback value to be passed to the kernel.
 */
te_error_t te_rollback_check(te_rollback_args_t *rb_args);

/*!
 * general-purpose speculation barrier
 *
 * Any memory read that is sequenced after the speculation barrier will not
 * speculatively execute until all conditions evaluated before the barrier have
 * been architecturally resolved.
 */
#define ENABLE_SPECULATION_BARRIER
#ifdef ENABLE_SPECULATION_BARRIER
static inline void te_speculation_barrier(void)
{
	__asm__ volatile("dsb  sy\n\t"
			"isb"
			::: "memory");

	return;
}
#else
static inline void te_speculation_barrier(void)
{
	return;
}
#endif

/** @} */

#ifdef CONFIG_TRUSTY

/**
 * Defines a log tag that is prefixed to each \c te_fprintf call for better
 * debugging. Each TA must define its own tag to see the prefix.
 */
extern const char *TRUSTY_LOG_TAG;

/* Entry point for Trusty TAs. Every TA is expected to invoke this in their
 * main function.
 * \uuid[in]: UUID of the TA
 * \flags[in]: This value should be a combination of the following values:
 *    IPC_PORT_ALLOW_NS_CONNECT - allows a connection from other secure apps
 *    IPC_PORT_ALLOW_TA_CONNECT- allows a connection from the non-secure world
 */
int te_main(te_service_id_t uuid, uint32_t flags);
#endif

#endif
