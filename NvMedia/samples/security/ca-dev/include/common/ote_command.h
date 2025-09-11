/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Common Commands</b>
 *
 * @b Description: Declares the common commands in the TLK interface.
 */

/**
 * @defgroup tlk_common_command Commands
 *
 * Defines common functions for Trusted Little Kernel (TLK).
 *
 * @ingroup ote_common
 * @{
 */

#ifndef __OTE_COMMAND_H
#define __OTE_COMMAND_H

#include <stdint.h>
#include <common/ote_common.h>
#include <common/ote_error.h>

/*! Opens a session to a TLK secure service.
*
* Creates a session to a specified TLK secure service.
* This function must be called from the user side or from another TLK service.
*
* \param[out] session A pointer to a TLK secure service session
*     to be initialized by this function.
* \param[in] service The UUID/Service ID of the TLK service to
*     which the caller wants to connect.
* \param[in,out] operation A pointer to parameters
*     that are expected by the TLK service.
*
* \return OTE_SUCCESS Indicates the operation was successful.
*/
te_error_t te_open_session(te_session_t *session, te_service_id_t *service,
		te_operation_t *operation);

/*! Closes an existing open session to a TLK secure service.
*
* \param[in] session A pointer to a TLK secure service session.
*/
void te_close_session(te_session_t *session);

/*! Dynamically creates a new TLK secure service operation object.
*
* This function creates a new \c te_operation_t object on the heap,
* and then initializes it.
*
* \return A pointer to a TLK secure service operation object that
*     was allocated on the stack and initialized.
*/
te_operation_t* te_create_operation(void);

/*! Initializes a TLK operation object.
*
* This function initializes a TLK operation object using the TLK operation
* object pointer parameter passed to it. It is normally called to allocate
* a TLK operation object on the stack.
*
* \param[in,out] te_op A pointer to a TLK operation object.
*/
void te_init_operation(te_operation_t *te_op);

/*! Deinitializes an existing TLK operation object.
*
* Frees internal memory used by an existing TLK operation object.
*
* \param[in] teOp A pointer to a TLK secure service operation object.
*/
void te_deinit_operation(te_operation_t *teOp);

/*! Sends an existing TLK operation object.
*
* Sends the operation object to the TLK service.
* The operation object must have the command ID or necessary parameter setup.
*
* \param[in] session A pointer to an established TLK service session.
* \param[in,out] te_op A \c te_operation_t object with proper parameters set up
*     and a command ID.
*
* \return The TLK secure service results after it services the command.
*/
te_error_t te_launch_operation(te_session_t *session, te_operation_t *te_op);

/*! Sets a command to a TLK secure service operation object.
*
* Sets the command ID to the operation object that will be sent
* to the TLK secure service.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] command A command ID that will be sent to the TLK secure service.
*/
void te_oper_set_command(te_operation_t *te_op, uint32_t command);

/*! Adds a read-only integer parameter to a TLK operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] Int An integer value.
*/
void te_oper_set_param_int_ro(te_operation_t *te_op, uint32_t index,
		uint32_t Int);

/*! Adds a read-write integer parameter to a TLK operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] Int An integer value.
*/
void te_oper_set_param_int_rw(te_operation_t *te_op, uint32_t index,
		uint32_t Int);

/*! Adds a read-only buffer parameter to the operation object.
* The buffer is the parameter at the given \a index in the object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] base The base address to the memory buffer.
* \param[in] len The length of the memory buffer.
*/
void te_oper_set_param_mem_ro(te_operation_t *te_op, uint32_t index,
		const void *base, uint32_t len);

/*! Adds a read-write buffer parameter to the operation object.
* The buffer is the parameter at the given \a index in the object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] base The base address to the memory buffer.
* \param[in] len The length of the memory buffer.
*/
void te_oper_set_param_mem_rw(te_operation_t *te_op, uint32_t index,
		void *base, uint32_t len);

/*! Adds a persistent read-only buffer parameter to the operation object.
* The buffer is the parameter at the given \a index in the object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] base The base address to the memory buffer.
* \param[in] len The length of the memory buffer.
*/
void te_oper_set_param_persist_mem_ro(te_operation_t *te_op, uint32_t index,
		const void *base, uint32_t len);

/*! Adds a persistent read-write buffer parameter to the operation object.
* The buffer is the parameter at the given \a index in the object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[in] base The base address to the memory buffer.
* \param[in] len The length of the memory buffer.
*/
void te_oper_set_param_persist_mem_rw(te_operation_t *te_op, uint32_t index,
		void *base, uint32_t len);

/*! Gets a TLK command from an operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
*
* \return The current command ID from the given operation object.
*/
uint32_t te_oper_get_command(te_operation_t *te_op);

/*! Gets the number of parameters from an operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
*
* \return The number of parameters from the given operation object.
*/
uint32_t te_oper_get_num_params(te_operation_t *te_op);

/*! Gets the parameter type of a parameter.
*
* Gets the parameter type of a parameter, given its index and operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[out] type The type of parameter.
*
* @retval OTE_SUCCESS Indicates the \a te_op parameter was found and the \a type
*     parameter was returned.
* @retval OTE_ERROR_ITEM_NOT_FOUND Indicates the \a te_op parameter was not found
*     or the \a type parameter was not returned.
*/
te_error_t te_oper_get_param_type(te_operation_t *te_op, uint32_t index,
		te_oper_param_type_t *type);

/*! Gets an integer parameter from a given TLK operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[out] Int A pointer to an integer value.
*
* @retval OTE_SUCCESS Indicates the \a te_op parameter was found and the
*     \a Int parameter was returned.
* @retval OTE_ERROR_ITEM_NOT_FOUND Indicates the \a index was not found or the
*     \a Int parameter was not an integer.
*
*/
te_error_t te_oper_get_param_int(te_operation_t *te_op, uint32_t index,
		uint32_t *Int);

/*! Gets a memory buffer parameter from a given TLK operation object.
*
* \param[in] te_op A pointer to a TLK operation object.
* \param[in] index An index value used to retrieve the parameter.
* \param[out] base The base address of buffer.
* \param[out] len The length of memory buffer.
*
* @retval OTE_SUCCESS Indicates the \a te_op parameter was found and the
*     \a index was returned.
* @retval OTE_ERROR_ITEM_NOT_FOUND Indicates the \a index was not found or
*     the \a te_op parameter was not a memory buffer.
*/
te_error_t te_oper_get_param_mem(te_operation_t *te_op, uint32_t index,
		void **base, uint32_t *len);

/*!\brief Get a mem parameter
*
* Get a read-only memory buffer parameter from given operation
*
* \param[in] te_op A valid te_operation_t pointer
* \param[in] index A index value used to retrieve the parameter
* \param[out] base The base address of buffer
* \param[out] len The length of memory buffer
*
* \return Returns OTE_SUCCESS if parameter is found and type is returned.
* Else return OTE_ERROR_ITEM_NOT_FOUND if index is not found, or paramter not memory buffer.
*/
te_error_t te_oper_get_param_mem_ro(te_operation_t *te_op, uint32_t index,
		const void **base, uint32_t *len);

/*!\brief Get a mem parameter
*
* Get a writeable memory buffer parameter from given operation
*
* \param[in] te_op A valid te_operation_t pointer
* \param[in] index A index value used to retrieve the parameter
* \param[out] base The base address of buffer
* \param[out] len The length of memory buffer
*
* \return Returns OTE_SUCCESS if parameter is found and type is returned.
* Else return OTE_ERROR_ITEM_NOT_FOUND if index is not found, or paramter not memory buffer.
*/
te_error_t te_oper_get_param_mem_rw(te_operation_t *te_op, uint32_t index,
		void **base, uint32_t *len);

/*!\brief Get a mem parameter
*
* Get a read-only memory buffer parameter from given operation that persists
* until the session is closed
*
* \param[in] te_op A valid te_operation_t pointer
* \param[in] index A index value used to retrieve the parameter
* \param[out] base The base address of buffer
* \param[out] len The length of memory buffer
*
* \return Returns OTE_SUCCESS if parameter is found and type is returned.
* Else return OTE_ERROR_ITEM_NOT_FOUND if index is not found, or paramter not memory buffer.
*/
te_error_t te_oper_get_param_persist_mem_ro(te_operation_t *te_op,
		uint32_t index, const void **base, uint32_t *len);

/*!\brief Get a mem parameter
*
* Get a writeable memory buffer parameter from given operation that persists
* until the session is closed
*
* \param[in] te_op A valid te_operation_t pointer
* \param[in] index A index value used to retrieve the parameter
* \param[out] base The base address of buffer
* \param[out] len The length of memory buffer
*
* \return Returns OTE_SUCCESS if parameter is found and type is returned.
* Else return OTE_ERROR_ITEM_NOT_FOUND if index is not found, or paramter not memory buffer.
*/
te_error_t te_oper_get_param_persist_mem_rw(te_operation_t *te_op,
		uint32_t index, void **base, uint32_t *len);

/*! Resets the data in an operation object.
*
* When you call functions that set values within an operation object
* (for example te_oper_set_param_mem_rw()), the TLK interface allocates
* internal memory for those values.
* If you need to re-use an operation with new values,
* call \c %te_operation_reset() before setting the new values.
*
* \param[in] te_op A pointer to a TLK operation object.
*/
void te_operation_reset(te_operation_t *te_op);

/** @} */

#endif
