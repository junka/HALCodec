/*
 * Copyright (c) 2013, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Manifest Layout</b>
 *
 * @b Description: Declares the layout of the manifest in the TLK.
 */

/**
 * @defgroup tlk_services_manifest Manifest Layout
 *
 * Trusted Little Kernel (TLK) services manifest layout.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_MANIFEST_H
#define __OTE_MANIFEST_H

#include <stdint.h>
#include <common/ote_common.h>

/*! Holds the manifest structure.
 *
 * The layout of the @c .ote.manifest section in the Trusted Application
 * (TA) is the
 * fixed fields followed by an arbitrary number of configuration options.
 *
 * Fixed fields:
 * - \a name 	     : Informative name of the task (optional).
 * - \a private_data : Each TA specifies how to use this field (optional).
 * - \a uuid	     : Uniquely identifies each TA in the system.
 *
 * Optional 32-bit config options:
 *  - \a config_options[]: Task configuration options set by macros in ::ote_config_key_t.
 */
typedef struct {
	char		name[OTE_TASK_NAME_MAX_LENGTH];
	char		private_data[OTE_TASK_PRIVATE_DATA_LENGTH];
	te_service_id_t uuid;
	uint32_t config_options[];
} OTE_MANIFEST;

typedef enum {
	OTE_CONFIG_KEY_MIN_STACK_SIZE	= 1,
	OTE_CONFIG_KEY_MIN_HEAP_SIZE	= 2,
	OTE_CONFIG_KEY_MAP_MEM		= 3,
	OTE_CONFIG_KEY_RESTRICT_ACCESS	= 4,
	OTE_CONFIG_KEY_AUTHORIZE	= 5,
	OTE_CONFIG_KEY_TASK_ISTATE	= 6,
} ote_config_key_t;

/*! Declares the minimum stack size.
*
* Defines the minimum stack size the TLK service would expect.
*
* \param[in] sz The size of the stack in bytes.
*/
#define OTE_CONFIG_MIN_STACK_SIZE(sz) 		\
	OTE_CONFIG_KEY_MIN_STACK_SIZE, sz

/*! Declares the minimum heap size.
*
* Defines the minimum heap size the TLK service would expect.
*
* \param[in] sz The size of the heap in bytes.
*/
#define OTE_CONFIG_MIN_HEAP_SIZE(sz) 		\
	OTE_CONFIG_KEY_MIN_HEAP_SIZE, sz

/*! Declares the memory address space needed.
*
* Declares the physical memory address space the TLK service
* will require; a mapping will be created for TLK service.
*
* \param[in] id An ID number to later retrieve the mapping.
* \param[in] off Base address of the physical address space.
* \param[in] sz The size of the physical address space.
*/
#define OTE_CONFIG_MAP_MEM(id,off,sz)		\
	OTE_CONFIG_KEY_MAP_MEM, id, off, sz

/*! Defines bit flags for restricting task access by client type.
 *
 * Use these bit flags with ::OTE_CONFIG_RESTRICT_ACCESS
 * to restrict access for the type of client.
 */
enum {
	OTE_RESTRICT_SECURE_TASKS = 1 << 0,
	OTE_RESTRICT_NON_SECURE_APPS = 1 << 1,
};

/*! Declares client types that have restricted access.
 *
 * \param[in] clients A bit field to restrict access for client types.
 */
#define OTE_CONFIG_RESTRICT_ACCESS(clients) \
	OTE_CONFIG_KEY_RESTRICT_ACCESS, clients

/*! Defines special actions that the task can be authorized to perform.
 * These actions are used with the ::OTE_CONFIG_AUTHORIZE macro.
 * By default, the TA is not authorized for any special actions.
 *
 * Currently, this enum defines only the \c OTE_AUTHORIZE_INSTALL bit.
 * However, future releases may add support for other
 * \c OTE_AUTHORIZE_* values. If that happens, you would
 * set the ::OTE_CONFIG_AUTHORIZE \a perm argument with
 * a bit field derived by ORing together the relevant
 * \c OTE_AUTHORIZE_* values defined in this enum.
 */
enum {
	OTE_AUTHORIZE_INSTALL   = 1 << 10, /**< Task is an installer. */
};

/*! Declares special actions that a TA is authorized to perform.
 * @sa ::OTE_AUTHORIZE_INSTALL
 *
 * \param[in] perm A bit field that authorizes special actions
 *                 for the TA.
 */
#define OTE_CONFIG_AUTHORIZE(perm) \
	OTE_CONFIG_KEY_AUTHORIZE, perm

/*! Defines bit flags that set attributes for the installed tasks.
 * These values are used with the ::OTE_CONFIG_TASK_INITIAL_STATE macro.
 * By default all attributes are unset.
 *
 * To set a task's initial state attributes on load,
 * set the ::OTE_CONFIG_KEY_TASK_ISTATE \a state argument with
 * a bit field derived by ORing together the relevant
 * \c OTE_MANIFEST_TASK_ISTATE_* values defined in this
 * enum.
 */
enum {
	OTE_MANIFEST_TASK_ISTATE_IMMUTABLE  = 1 << 0,	/*!< Task manifest cannot be modified by the installer. */
	OTE_MANIFEST_TASK_ISTATE_STICKY     = 1 << 1,	/*!< Task cannot be unloaded. */
	OTE_MANIFEST_TASK_ISTATE_BLOCKED    = 1 << 2,	/*!< Task installed in BLOCKED state. */
};

/*! Declares attributes for tasks, which apply beginning when
 * the task is initially loaded.
 * @sa OTE_MANIFEST_TASK_ISTATE_IMMUTABLE,
 * OTE_MANIFEST_TASK_ISTATE_STICKY, and
 * OTE_MANIFEST_TASK_ISTATE_BLOCKED.
 *
 * \param[in] state A bitfield to set the initial state to blocked.
 */
#define OTE_CONFIG_TASK_INITIAL_STATE(state)	\
	OTE_CONFIG_KEY_TASK_ISTATE, state

/* manifest section attributes */
#define OTE_MANIFEST_ATTRS			\
	__attribute((aligned(4))) __attribute((section(".ote.manifest")))

/** @} */

#endif //__OTE_MANIFEST_H
