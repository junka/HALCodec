/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Task-Loading Interface</b>
 *
 * @b Description: Declares data types and functions for
 *                 the TLK task-loading interface.
 *
 */

/**
 * @defgroup tlk_services_task_load_interface Task Loader
 *
 * Defines Trusted Application (TA) services declarations and functions for
 * task loading and management
 *
 * The calls are restricted to tasks that have INSTALL permission
 * set in the manifest. Calls from other tasks will be rejected.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_TASK_H
#define __OTE_TASK_H

#include <sys/types.h>
#include <common/ote_common.h>

/***/

/*! Holds information that is passed to the ioctl handler
 * for the ::OTE_TASK_OP_MEMORY_REQUEST operation.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	uint32_t	app_handle;
	uintptr_t	app_addr;
	uint32_t	app_size;
} te_app_load_memory_request_args_t;

/*! Allocates TLK kernel memory for task loading.
 *
 * \param [in] app_size Application size in bytes.
 * \param [out] app_addr Contains the task virtual address of the
 *			secure memory buffer allocated by TLK (where
 *			caller can write the application image to).
 * \param [out] app_handle Receives the opaque handle that can be used to
 *			manage app loading. Handle not valid after app loaded
 *			or an error occurs. The handle is a 32-bit random value
 *			set by TLK.
 *
 * \retval OTE_SUCCESS	Indicates the operation was successful.
 *
 * Step 1/3 of task loading API.
 */
te_error_t te_app_request_memory(u_int app_size, uintptr_t *app_addr,
				 uint32_t *app_handle);

/***/

/*! This is compatible with task_info_t, field values extracted from manifest record */
typedef struct {
	te_service_id_t	uuid;
	u_int		manifest_exists;
	u_int		multi_instance;
	u_int		min_stack_size;
	u_int		min_heap_size;
	u_int		map_io_mem_cnt;
	u_int		restrict_access;
	u_int		authorizations;
	u_int		initial_state;
	char		task_name[OTE_TASK_NAME_MAX_LENGTH];
	unsigned char	task_private_data[OTE_TASK_PRIVATE_DATA_LENGTH];
} te_task_info_t;

/*! Holds information passed to the ioctl handler
 * with the ::OTE_TASK_OP_PREPARE operation.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	uint32_t	app_handle;
	te_task_info_t	te_task_info;
} te_app_prepare_args_t;

/*! Requests the TLK to parse the static information
 * from the task image loaded
 * to the TLK shared memory and returns the task configuration information
 * from the manifest section to the caller. The task is looked up by
 * the APP_HANDLE received from the te_app_request_memory().
 *
 * \param [in] app_handle Handle for the copied application image to prepare.
 * \param [out] app_task_info Returns manifest data (see te_task_info_t) of the
 *			     prepared task.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 * Step 2/3 of task loading API.
 */
te_error_t te_app_prepare(uint32_t app_handle, te_task_info_t *app_task_info);

/***/

/*!
 * Holds task restrictions requested by the installer (manifest overrides).
 *
 * @note This does not currently allow to "override" all manifest fields,
 * e.g. the UUID which means that tasks without manifests can not be loaded.
 *
 * In general: only non-security critical field value overrides can currently
 * be requested (more/all fields can be added if required).
 *
 * If the task manifest is flagged immutable, TLK will not modify any manifest
 * values by the override mechanism. In this case the installer may choose
 * to reject immutable tasks if overrides are mandatory (any override values
 * will be ignored in this case). For immutable tasks the APP_RESTRICTIONS
 * parameter must be set to NULL.
 *
 * This record is compatible with task_restrictions_t
 */
typedef struct {
	uint32_t	min_stack_size;
	uint32_t	min_heap_size;
	char		task_name[OTE_TASK_NAME_MAX_LENGTH];
} te_task_restrictions_t;

/** Defines install types for use with te_app_start()
 * and ::te_app_start_args_t.
 */
typedef enum {
	INSTALL_TYPE_NORMAL = 0, /**< Normal install operation. */
	INSTALL_TYPE_REJECT,	/**< Reject (terminate install of a task.) */
	/** Replace a task if it already exists. Sticky tasks cannot
	 * be updated. */
	INSTALL_TYPE_UPDATE,	/* Replace task if it already exists. */
} install_type_t;

/*! Holds information that is passed to the ioctl handler
 * for ::OTE_TASK_OP_START operations.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	uint32_t		app_handle;
	install_type_t		app_install_type;
	/* task manifest restrictions */
	te_task_restrictions_t	app_restrictions;
} te_app_start_args_t;

/*! Starts the loaded and prepared application.
 *
 * \param [in] app_handle Opaque handle for the application to start
 *			 After this call returns the app_handle is no longer valid.
 * \param [in] install_type Specifies whether to reject the task,
 *             perform a normal task install, or update an installation.
 *             Updating an installation installs a new task in place of
 *             an old task and switches the old tasks's UUID to the new
 *             task. If the new task cannot be installed, the old task
 *             resumes operation.
 * \param [in] app_restrictions Specifies overrides for a subset of task properties in
 *             manifest (unless the manifest is declared immutable) or NULL
 *             for no overrides. Zero field values are ignored in overrides.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 * @note Currently this does not support "creating" a missing manifest
 * for a task that does not have it in the binary ELF image,
 * so also all loaded task
 * images must contain a manifest.
 *
 * Step 3/3 of task loading API.
 */
te_error_t te_app_start(uint32_t app_handle,
			install_type_t install_type,
			te_task_restrictions_t *app_restrictions);

/***/

/*! Holds information that is passed to the ioctl handler
 * for ::OTE_TASK_OP_LIST operations.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	uint32_t	app_index;
	/* response fields */
	uint32_t	app_type;
	uint32_t	app_state;
	te_task_info_t  app_info;
} te_app_list_args_t;

/*! Holds list command returned information. */
typedef struct te_list_apps {
	uint32_t	al_index;
	/* response fields */
	uint32_t	al_type;
	uint32_t	al_state;
	te_task_info_t  al_info;
} te_list_apps_t;

/*! Returns the UUID and optional name of current tasks by index.
 *
 * \param [in,out] app_list Indicates which task (0..N) UUID/NAME to look for.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *	   OTE_ERROR_ITEM_NOT_FOUND indicates task at al_index is not valid.
 *
 * al_name is a zero terminated possibly empty c-string.
 */
te_error_t te_list_apps(te_list_apps_t *app_list);

/***/

/*!
 * Type for the ioctl handler
 *
 * Info request types:
 */
typedef enum {
	OTE_GET_TASK_INFO_REQUEST_INDEX,
	OTE_GET_TASK_INFO_REQUEST_UUID,
	OTE_GET_TASK_INFO_REQUEST_SELF,
} te_get_info_type_t;

typedef struct {
	te_get_info_type_t	gti_request_type;
	union {
		uint32_t	gtiu_index;
		te_service_id_t	gtiu_uuid;
	};
	/* response fields */
	uint32_t		gti_state;
	uint32_t		gti_type;
	te_task_info_t		gti_info;
} te_get_task_info_t;

/*! Gets the task config info for any task in the system.
 *
 * For loaded tasks the installer gets this info also with other means, but
 * for static tasks (like the installer itself) this function gets the
 * manifest information, particularly the private_data field which
 * in the installer case configured the trust anchor (digest of root cert
 * data blob).
 *
 * This function also returns current task state info in
 * the \c gti_state and \c gti_type
 * fields of ::te_get_task_info_t.
 *
 * \param [in,out] task_info Request the manifest information of any task (static or loaded)
 *			gti_request_type and the union gtiu_* fields [in] identify the task
 *			and gti_info [out] contains the te_task_info_t response.
 */
te_error_t te_task_get_info(te_get_task_info_t *task_info);

/***/

typedef struct {
	/* map index (in) */
	uint32_t		map_index;

	/* response fields */
	uint32_t		map_id;
	uint32_t		map_offset; /* make 64 bit when rest of code supports it */
	uint32_t		map_size;
} te_memory_mapping_t;

typedef struct {
	te_get_info_type_t	gmt_request_type;
	union {
		uint32_t	gmtu_index;
		te_service_id_t	gmtu_uuid;
	};
	te_memory_mapping_t       gmt_map;
} te_get_task_mapping_t;

/*! Holds information that is passed to the ioctl handler
 * for the ::OTE_TASK_OP_PENDING_MAPPING operation.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	uint32_t	    pm_handle;
	te_memory_mapping_t pm_map;
} te_get_pending_map_args_t;

/*! Gets the specified manifest address mapping from the task.
 *
 * \param[in,out] task_mapping A pointer to manifest mapping information
 *          of any task (static or loaded). The
 *			\c gmt_request_type and the union \c gtiu_* fields [in] identify the task
 *			and \c gmt_map field map_index specifies the mapping (0..N).
 *
 *			This function's response is in the \a task_mapping fields:
 *
 *			- \c map_id	specifies to the mapping ID.
 *			- \c map_offset specifies to the physical address to map.
 *			- \c map_size specifies size of mapped area.
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_task_get_mapping(te_get_task_mapping_t *task_mapping);

/*! Gets the specified manifest address mapping from task by its installation handle
 * before the task is installed (this is an installer only API).
 *
 * \param [in] app_handle The handle for the pending task.
 * \param [in,out] app_map A pointer to task mapping information.
 *			\c app_map->map_index is the index [0..N] of the fetched mapping.
 *
 *			This function's response is in the \a app_map fields:
 *
 *			- \c map_id	specifies to the mapping ID.
 *			- \c map_offset specifies to the physical address to map.
 *			- \c map_size specifies size of mapped area.
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_get_pending_task_mapping(uint32_t app_handle,
				       te_memory_mapping_t *app_map);

/***/

/*! Gets the current task name from its own manifest record.
 *
 * \param [out] name Copy task name from manifest record.
 * \param [in,out] len_p Contains the max length of the name
 *                      on input and the length of the
 *			copied manifest name.
 *
  * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_task_get_name_self(char *name, uint32_t *len_p);

/***/

typedef struct {
	uint32_t si_type;
} te_system_info_args_t;

/*! Outputs task load system info to console, for test/debug purposes.
 * DEBUG API only for information.
 */
te_error_t te_task_system_info(uint32_t type);

/***/

typedef enum {
	OTE_APP_ID_INDEX,
	OTE_APP_ID_UUID,
} te_app_id_t;

/*! Holds information that is passed to the ioctl handler
 * for the ::OTE_TASK_OP_UNLOAD operation.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	te_app_id_t tid_type;
	union {
		uint32_t	tid_index;
		te_service_id_t	tid_uuid;
	};
} te_app_unload_args_t;

typedef struct {
	te_app_id_t aid_type;
	union {
		uint32_t	aid_index;
		te_service_id_t	aid_uuid;
	};
} te_app_unload_t;

/*! Unloads a previously loaded application.
 *
 * \param [in] arg_unload Identifies the application to unload (XX now contains UUID)
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_ITEM_NOT_FOUND Indicates a task with
 *         the specified uuid did not exist.
 */
te_error_t te_app_unload(te_app_unload_t *arg_unload);

/*****/
/*! Holds information that is passed to the ioctl handler
 * for the ::OTE_TASK_OP_BLOCK operation.
 * @sa ::te_task_request_args_t
 */
typedef struct {
	te_app_id_t tid_type;
	union {
		uint32_t	tid_index;
		te_service_id_t	tid_uuid;
	};
} te_app_block_args_t;

typedef struct {
	te_app_id_t aid_type;
	union {
		uint32_t	aid_index;
		te_service_id_t	aid_uuid;
	};
} te_app_block_t;

/*! Blocks a task.
 *
 * \param[in] app Identifies the application to block by INDEX or UUID.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_ITEM_NOT_FOUND indicates specified task was not found.
 *	   + others...
 */
te_error_t te_app_block(te_app_block_t *app);

/*! Unblocks a previously blocked application.
 *
 * \param [in] app Identifies the application to unblock by INDEX or UUID.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_ITEM_NOT_FOUND indicates specified task was not found.
 *	   + others...
 */
te_error_t te_app_unblock(te_app_block_t *app);

/*****/

/*! Defines opcodes for ::OTE_IOCTL_TASK_REQUEST ioctl argument
 *  for the above requests.
 */
typedef enum {
	OTE_TASK_OP_UNKNOWN,
	OTE_TASK_OP_MEMORY_REQUEST,
	OTE_TASK_OP_PREPARE,
	OTE_TASK_OP_START,
	OTE_TASK_OP_LIST,
	OTE_TASK_OP_GET_TASK_INFO,
	OTE_TASK_OP_SYSTEM_INFO,
	OTE_TASK_OP_PENDING_MAPPING,
	OTE_TASK_OP_UNLOAD,
	OTE_TASK_OP_BLOCK,
	OTE_TASK_OP_UNBLOCK,
	OTE_TASK_OP_GET_MAPPING,
} te_task_opcode_t;

/***/

/*! Holds the task-loading ioctl argument used with
 * ::OTE_IOCTL_TASK_REQUEST commands.
 * The \a ::te_task_request_args_t::ia_opcode field
 * specifies the operation, which
 * in turn indicates which argument from the union fields applies.
 * For example, if \a ::te_task_request_args_t::ia_opcode
 * is ::OTE_TASK_OP_MEMORY_REQUEST, then
 * \a ::te_task_request_args_t::ia_load_memory_request
 * provides the memory request.
 */
typedef struct te_task_request_args_s {
	te_task_opcode_t		ia_opcode;
	union {
		te_app_load_memory_request_args_t ia_load_memory_request;
		te_app_prepare_args_t	ia_prepare;
		te_get_pending_map_args_t ia_pending_mapping;
		te_app_start_args_t	ia_start;
		te_app_list_args_t	ia_list;
		te_get_task_info_t	ia_get_task_info;
		te_get_task_mapping_t	ia_get_task_mapping;
		te_app_unload_args_t	ia_app_unload;
		te_app_block_args_t	ia_app_block;
		te_system_info_args_t	ia_system_info;
	};
} te_task_request_args_t;

/** @} */

#endif /* __OTE_TASK_H */
