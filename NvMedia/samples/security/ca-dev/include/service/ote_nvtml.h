/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * <b> NVIDIA Trusted Operating System Interface: NVIDIA Cryptography</b>
 *
 * @b Description: Declares the cryptography APIs in the TOS.
 */

/**
 * @defgroup tos_services_nvtml Trusted Messaging Service Manager
 *
 * Defines APIs for Trusted Operating System (TOS)
 * authentication and messaging services.
 *
 * @ingroup tos_services
 *
 * @{
 */

#pragma once

/*! Sign content using Tegra Model Private Key stored on device.
 *
 *  \param [in]     input_buf            A pointer to the input buffer, allocated and
 *                                       freed by the callee.
 *
 *  \param [in]     input_buf_len        The length of the input buffer in bytes.
 *
 *  \param [out]    output_buf           A pointer to the output buffer, allocated and
 *                                       freed by the callee. Output buffer must
 *                                       have enough space to fit input data +
 *                                       version + signature
 *
 *  \param [in/out]    output_buf_len    The length of the output buffer in bytes.
 *                                       This parameter must be populated with output buffer size
 *                                       and the API will populate the true length
 *                                       of returned output buffer.
 *
 *  \retval         OTE_SUCCESS              Indicates the operation was successful.
 *  \retval         OTE_ERROR_BAD_PARAMETERS Indicates the params passed were incorrect
 *  \retval         OTE_ERROR_GENERIC        Indicates that there was an unknown exception
 */
te_error_t sign_by_tmk(uint8_t *input_buf, uint32_t input_buf_len, uint8_t *output_buf,
	uint32_t *output_buf_len);

/*! Sign content using Tegra Model Private Key stored on device. The extended version of the API
 *  adds AID to the header
 *
 *  \param [in]     input_buf            A pointer to the input buffer, allocated and
 *                                       freed by the callee.
 *
 *  \param [in]     input_buf_len        The length of the input buffer in bytes.
 *
 *  \param [out]    output_buf           A pointer to the output buffer, allocated and
 *                                       freed by the callee. Output buffer must
 *                                       have enough space to fit input data +
 *                                       version + signature
 *
 *  \param [in/out]    output_buf_len    The length of the output buffer in bytes.
 *                                       This parameter must be populated with output buffer size
 *                                       and the API will populate the true length
 *                                       of returned output buffer.
 *
 *  \retval         OTE_SUCCESS              Indicates the operation was successful.
 *  \retval         OTE_ERROR_BAD_PARAMETERS Indicates the params passed were incorrect
 *  \retval         OTE_ERROR_GENERIC        Indicates that there was an unknown exception
 */
te_error_t sign_by_tmk_ex(uint8_t *input_buf, uint32_t input_buf_len, uint8_t *output_buf,
	uint32_t *output_buf_len);
