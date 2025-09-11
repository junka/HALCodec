/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/**
 * @file
 * <b> NVIDIA Trusty Interface: Tegra Secure Counter (TSC) Services</b>
 *
 * @b Description: Declares common declarations and functions for
 *                 the Trusty TSC service.
 *
 * @defgroup tlk_services_secure_counter Tegra Secure Counter (TSC) Services
 *
 * Declares the trusty TSC service declarations and functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_TSC_H
#define __OTE_TSC_H

#include <service/ote_rtc.h>

/*! \brief Initializes the TSC hardware.
 *
 * Function to intialize the TSC hardware and open a TSC session.
 * Must be called once before making calls to _get_msecs.
 * Initialization will fail if a session is already open.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_tsc_init(void);

/*! \brief Deinitializes the TSC hardware.
 *
 * Function to deinitialize the TSC hardware and close a TSC session.
 * Must be called once after the desired calls to _get_msecs have been made.
 * Deinitialization will fail if a session is not open.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates that the operation was unsuccessful
 *         (no open session exists)
 */
te_error_t ote_tsc_deinit(void);

/*! Gets the current value of the TSC in milliseconds.
 *
 * Function to access the TSC hardware.
 * The value returned represents the time, in milliseconds, since boot.
 * Any number of calls to _get_msecs is allowed between
 * initialization and deinitialization.
 *
 * \param [out] tsc_msecs a pointer to a variable to store
 *              the returned TSC value (in ms).
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates that the operation was unsuccessful
 *         (no open session exists)
 */
te_error_t ote_tsc_get_msecs(uint64_t *tsc_msecs);

/*! Gets the current value of the TSC in microseconds.
 *
 * Function to access the TSC hardware.
 * The value returned represents the time, in microseconds, since boot.
 * Any number of calls to _get_usecs is allowed between
 * initialization and deinitialization.
 *
 * \param [out] tsc_usecs a pointer to a variable to store
 *              the returned TSC value (in us).
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE Indicates that the operation was unsuccessful
 *         (no open session exists)
 */
te_error_t ote_tsc_get_usecs(uint64_t *tsc_usecs);


inline te_error_t ote_tsc_init(void) {
	return ote_rtc_init();
}

inline te_error_t ote_tsc_deinit(void) {
	return ote_rtc_deinit();
}

inline te_error_t ote_tsc_get_msecs(uint64_t *tsc_msecs) {
	return ote_rtc_get_time_msecs(tsc_msecs);
}

inline te_error_t ote_tsc_get_usecs(uint64_t *tsc_usecs) {
	return ote_rtc_get_time_usecs(tsc_usecs);
}

#endif
/** @} */
