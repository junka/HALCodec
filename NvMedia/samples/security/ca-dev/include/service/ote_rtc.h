/*
 * Copyright (c) 2013-2018, NVIDIA CORPORATION. All rights reserved.
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
 * <b> NVIDIA Trusted Little Kernel Interface: Real-Time Clock (RTC) Services</b>
 *
 * @b Description: Declares common declarations and functions for
 *                 the TLK RTC service.
 *
 */

/**
 * @defgroup tlk_services_rtc RTC Service
 *
 * Trusted Little Kernel (TLK) real-time clock (RTC) services.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_RTC_H
#define __OTE_RTC_H

#include <common/ote_error.h>

/*! \brief Initializes the RTC hardware.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_rtc_init(void);

/*! \brief Resets the RTC hardware and erases any previous keys.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_rtc_deinit(void);

/*! \brief Gets the RTC in seconds from the RTC hardware.
 *
 * \param [out] rtc A pointer to the RTC.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_rtc_get_time_secs(uint32_t *rtc);

/*! \brief Gets the RTC in milliseconds from the RTC hardware.
 *
 * \param [out] rtc A pointer to the RTC.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_rtc_get_time_msecs(uint64_t *rtc);

/*! \brief Gets the RTC in microseconds from the RTC hardware.
 *
 * \param [out] rtc A pointer to the collect the data.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_rtc_get_time_usecs(uint64_t *rtc);

/** @} */
#endif
