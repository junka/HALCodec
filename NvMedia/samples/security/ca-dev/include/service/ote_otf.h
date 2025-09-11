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
 * <b> NVIDIA Trusted Little Kernel Interface: On-the-Fly (OTF) Decoder Service</b>
 *
 * @b Description: Declares functions for the TLK OTF decoder service.
 *
 */

/**
 * @defgroup tlk_services_otf OTF Decoder Service
 *
 * Defines Trusted Little Kernel (TLK) on-the-fly (OTF)
 * decoder services functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_OTF_H
#define __OTE_OTF_H

#include <common/ote_error.h>

/*! \brief Initializes the on-the-fly (OTF) hardware.
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of
 *     resources.
 * \retval OTE_ERROR_COMMUNICATION Indicates that communication failed.
 */
te_error_t ote_otf_init(te_session_t **otfSession);

/*! \brief Resets the OTF hardware and erases any previous keys.
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 */
te_error_t ote_otf_deinit(te_session_t **otfSession);

/*! \brief Sets the key to be used by the OTF hardware.
 *
 * \param [in] buffer A pointer to the key.
 * \param [in] len The length of the buffer in bytes.
 * \param [in,out] keySlot A pointer to the key to be used.
 * \param [in] otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 * \retval OTE_ERROR_BAD_PARAMETERS Indicates input parameters were invalid.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of resources.
 * \retval OTE_ERROR_COMMUNICATION Indicates that communication failed.
 */
te_error_t ote_otf_setkey(void *buffer, uint32_t len,
		uint32_t *keySlot, te_session_t * otfSession);

/*! \brief Sets the key to be used by the OTF hardware at the specified slot.
 *
 * \param [in] buffer A pointer to the key.
 * \param [in] len The length of the buffer in bytes.
 * \param [in] keySlot Key slot to be used.
 * \param [in] otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 * \retval OTE_ERROR_BAD_PARAMETERS Indicates input parameters were invalid.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of resources.
 * \retval OTE_ERROR_COMMUNICATION Indicates that communication failed.
 */
te_error_t ote_otf_set_key_at(void *buffer, uint32_t len,
		uint32_t keySlot, te_session_t * otfSession);

/*! \brief Erases keys from the OTF hardware.
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of resources.
 * \retval OTE_ERROR_COMMUNICATION Indicates that communication failed. */
te_error_t ote_otf_erasekey(te_session_t *otfSession);

/*! \brief Reprograms all keys in OTF hardware
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 */
te_error_t ote_otf_reprogram_keys(te_session_t *otfSession);

/*! \brief Resets the secure timer.
 *
 * \param otfSession A pointer to the OTF session.
 * \param is_valid_policy status of policy check from HDCP TA
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of resources.*/
te_error_t ote_reset_secure_timer(te_session_t *otfsession,
		int32_t is_valid_polcy);

/*! \brief Enables VPR policy for the DSI Panel
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 */
te_error_t ote_otf_set_dsi_panel_vpr_policy(te_session_t *otfSession);

/*! \brief Runs the OTF Session Management Unit Tests
 *
 * \param otfSession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.
 */
te_error_t ote_otf_run_session_unit_tests(te_session_t *otfSession);

/*! \brief Gets validity of a keyslot in a particular session.
 *
 * \param keySlot The keyslot to be queried.
 * \param keyProvisioned A pointer to the validity result.
 * \param otfsession A pointer to the OTF session.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates the session object was invalid.*/
te_error_t ote_otf_is_key_provisioned(uint32_t keySlot, uint32_t *keyProvisioned,
					te_session_t *otfSession);
/** @} */
#endif
