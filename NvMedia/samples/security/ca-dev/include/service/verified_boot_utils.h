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
 * <b> NVIDIA Trusted OS Interface: Verified Boot Utils</b>
 *
 * @b Description: Declares common verified boot utilities like function to
 *                 derive master key using Root of trust.
 *
 */

/**
 * @defgroup tos_services_verified_boot_utils
 *
 * Trusted OS Verified Boot Utils.
 *
 * @ingroup tos_services
 *
 * @{
 */

#ifndef __TOS_VERIFIED_BOOT_UTILS_H
#define __TOS_VERIFIED_BOOT_UTILS_H

#include <common/ote_error.h>

/*! \brief Derive Master key.
 *
 * This method uses root of trust from the crypto service and verified boot
 * parameters from kernel to derive an intermediate key using AES CBC 256.
 * This intermediate key is used in HKDF along with derivationData to derive
 * the master key.
 *
 * \param [out] resultKey               Buffer to hold the master key
 * \param [in]  resultKeySize           Length of resultKey in Bytes
 * \param [in]  derivationData          String which holds info for HKDF
 * \param [in]  derivationDataSize      Length of derivationData in Bytes
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t derive_master_key(uint8_t* resultKey, size_t resultKeySize,
		const uint8_t* derivationData, size_t derivationDataSize);
/** @} */
#endif
