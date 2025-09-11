/*
 * Copyright (c) 2013-2020, NVIDIA CORPORATION. All rights reserved.
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
 * <b> NVIDIA Trusted Little Kernel Interface: Common UUID</b>
 *
 * @b Description: Declares the common UUID in the TLK services.
 */

/**
 * @defgroup tlk_common_nv_uuid NV_UUID
 *
 * Defines unique UUID for each Trusted Application for
 * Trusted Little Kernel (TLK).
 *
 * @ingroup ote_common
 *
 * @{
 */

#ifndef __OTE_NV_UUID_H
#define __OTE_NV_UUID_H

/*! Defines unique 16-byte UUID for TLK Trusted Applications. */

// All zero UUID is not allowed for any task
#define NULL_UUID \
	{ 0x00000000U, 0x0000U, 0x0000U, \
		{ 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U } }

// {6B42300B-8881-416d-9ABC-38B83E9CF49F}
#define SERVICE_RTC_UUID \
	{ 0x6b42300b, 0x8881, 0x416d, \
		{ 0x9a, 0xbc, 0x38, 0xb8, 0x3e, 0x9c, 0xf4, 0x9f } }

//{2f68f22a-6fa9-461f-813d-b12d09401114}
#define SERVICE_VUDU_UUID \
	{ 0x2f68f22a, 0x6fa9, 0x461f, \
		{ 0x81, 0x3d, 0xb1, 0x2d, 0x09, 0x40, 0x11, 0x14 } }

// {74629D40-1378-4d17-94D0-E0AF5D861D88}
#define SERVICE_OTF_UUID \
	{ 0x74629d40, 0x1378, 0x4d17, \
		{ 0x94, 0xd0, 0xe0, 0xaf, 0x5d, 0x86, 0x1d, 0x88 } }

// {5286F1BE-1CB4-40be-A77A-B6BBE41E768F}
#define SERVICE_STORAGE_UUID \
	{ 0x5286f1be, 0x1cb4, 0x40be, \
		{ 0xa7, 0x7a, 0xb6, 0xbb, 0xe4, 0x1e, 0x76, 0x8f } }

// {38ba0cdc-df0e-11e4-9869-233fb6ae4795}
#define SERVICE_GATEKEEPER_UUID \
	{ 0x38ba0cdc, 0xdf0e, 0x11e4, \
		{ 0x98, 0x69, 0x23, 0x3f, 0xb6, 0xae, 0x47, 0x95 } }

// {7E02FE41-E41B-405a-BC8B-44A8B89D5812}
#define SERVICE_KEYMASTER_UUID \
	{ 0x7e02fe41, 0xe41b, 0x405a, \
		{ 0xbc, 0x8b, 0x44, 0xa8, 0xb8, 0x9d, 0x58, 0x12 } }

// {13f616f9-8572-4a6f-a1f1-03aa9b05f9ff}
#define SERVICE_OEMCRYPTO_UUID \
	{ 0x13F616F9, 0x8572, 0x4A6F, \
		{ 0xA1, 0xF1, 0x03, 0xAA, 0x9B, 0x05, 0xF9, 0xff } }

// {130616f9-8572-4a6f-a1f1-03aa9b01c9ee}
#define SERVICE_PLAYREADY_UUID \
	{ 0x130616F9, 0x8572, 0x4A6F, \
		{ 0xA1, 0xF1, 0x03, 0xAA, 0x9B, 0x01, 0xC9, 0xee } }

// {64661962-40B5-46b8-B955-D010C461F62F}
#define SERVICE_NVCRYPTO_UUID \
	{ 0x64661962, 0x40b5, 0x46b8, \
		{ 0xb9, 0x55, 0xd0, 0x10, 0xc4, 0x61, 0xf6, 0x2f } }

/* {13f616f9-8572-4a6f-a1f1-04aa9b05f9ff} */
#define SERVICE_SECURE_HDCP_UUID \
	{ 0x13F616F9, 0x8572, 0x4A6F, \
		{ 0xA1, 0xF1, 0x04, 0xAA, 0x9B, 0x05, 0xF9, 0xff } }

// {403A0B40-B50D-40a4-A87C-62D253FDA594}
#define SERVICE_TRUSTEDAPP_UUID \
	{ 0x403a0b40, 0xb50d, 0x40a4, \
		{ 0xa8, 0x7c, 0x62, 0xd2, 0x53, 0xfd, 0xa5, 0x94 } }

// {214151DE-9F90-4c9c-896C-DE90FD87C4DC}
#define SERVICE_TRUSTEDAPP2_UUID \
	{ 0x214151de, 0x9f90, 0x4c9c, \
		{ 0x89, 0x6c, 0xde, 0x90, 0xfd, 0x87, 0xc4, 0xdc } }

// {3180D513-60DD-4179-B710-5791AA3EF642}
#define SERVICE_STORAGE_DEMO_UUID \
	{ 0x3180d513, 0x60dd, 0x4179, \
		{ 0xb7, 0x10, 0x57, 0x91, 0xaa, 0x3e, 0xf6, 0x42 } }

// {2FF24307-2623-48f5-A48D-77E0D2ABBA46}
#define SERVICE_CRYPTO_DEMO_UUID \
	{ 0x2ff24307, 0x2623, 0x48f5, \
		{ 0xa4, 0x8d, 0x77, 0xe0, 0xd2, 0xab, 0xba, 0x46 } }

// {cb25682e-3d21-4e98-8653-3bb46cabf89f}
#define SERVICE_TSEC_UUID \
	{ 0xcb25682e, 0x3d21, 0x4e98, \
        	{ 0x86, 0x53, 0x3b, 0xb4, 0x6c, 0xab, 0xf8, 0x9f } }

// {141b16b8-1f7e-42aa-a5d7-7b747738321e}
#define TESTING_SERVICE_UUID \
    { 0x141b16b8, 0x1f7e, 0x42aa, \
        { 0xa5, 0xd7, 0x7b, 0x74, 0x77, 0x38, 0x32, 0x1e } }

// {b93536ea-e56f-42b8-96e7-ca965fa90a47}
#define SERVICE_NVTML_UUID \
	{ 0xb93536ea, 0xe56f, 0x42b8, \
		{ 0x96, 0xe7, 0xca, 0x96, 0x5f, 0xa9, 0x0a, 0x47 } }

// RFC-4122 compliant UUID (version-5, sha-1) of
// "installer" in "TLK" namespace.
//
// {c622e081-3de8-5c15-b378-35369cd3ecc6}
#define SERVICE_INSTALLER_UUID \
	{ 0xc622e081, 0x3de8, 0x5c15, \
		{ 0xb3, 0x78, 0x35, 0x36, 0x9c, 0xd3, 0xec, 0xc6 } }

// {0179ed96-1adb-45a8-8dc69d08790252bb}
#define SERVICE_VRRAUTH_UUID \
	{ 0x0179ed96, 0x1adb, 0x45a8, \
		{ 0x8d, 0xc6, 0x9d, 0x08, 0x79, 0x02, 0x52, 0xbb } }

// {f7930cf5-c2fb-4d06-aa7b-57a33ea4224c}
#define EFS_CRYPTO_UUID \
	{ 0xf7930cf5, 0xc2fb, 0x4d06, \
		{ 0xaa, 0x7b, 0x57, 0xa3, 0x3e, 0xa4, 0x22, 0x4c } }

// {b7364f14-9835-4ba9-92db-8f94129cad3f}
#define SAMPLE_SERVICE1_UUID \
	{ 0xb7364f14, 0x9835, 0x4ba9, \
		{ 0x92, 0xdb, 0x8f, 0x94, 0x12, 0x9c, 0xad, 0x3f } }

// {d5c3bb40-5b2d-11e5-885d-feff819cdc9f}
#define SAMPLE_SERVICE2_UUID \
	{ 0xd5c3bb40, 0x5b2d, 0x11e5, \
		{ 0x88, 0x5d, 0xfe, 0xff, 0x81, 0x9c, 0xdc, 0x9f } }

// {13928130-5a75-11e6-8b77-86f30ca893d3}
#define SERVICE_EKS2_UUID \
        { 0x13928130, 0x5a75, 0x11e6, \
                { 0x8b, 0x77, 0x86, 0xf3, 0x0c, 0xa8, 0x93, 0xd3 } }

// {ed28f376-93ab-4a27-bcfe-c7bf22c36ee5}
#define SERVICE_FINGERPRINT_UUID \
	{ 0xed28f376, 0x93ab, 0x4a27, \
		{ 0xbc, 0xfe, 0xc7, 0xbf, 0x22, 0xc3, 0x6e, 0xe5 } }

// {33546eba-c918-4165-8680-a2e1e69a49c9}
#define SERVICE_MODS_UUID \
	{ 0x33546eba, 0xc918, 0x4165, \
		{ 0x86, 0x80, 0xa2, 0xe1, 0xe6, 0x9a, 0x49, 0xc9 } }

// {f618262c-5a29-40bc-b61c-716e4d015356}
#define SERVICE_DOLBY_UUID \
	{ 0xf618262c, 0x5a29, 0x40bc, \
		{ 0xb6, 0x1c, 0x71, 0x6e, 0x4d, 0x01, 0x53, 0x56} }

/** @} */

#if !defined(CONFIG_TRUSTY)
/* list of TAs which may be used by TLK */
#define TLK_SERVICE_UUID_LIST \
	{ \
		SERVICE_RTC_UUID, \
		SERVICE_VUDU_UUID, \
		SERVICE_OTF_UUID, \
		SERVICE_STORAGE_UUID, \
		SERVICE_GATEKEEPER_UUID, \
		SERVICE_KEYMASTER_UUID, \
		SERVICE_OEMCRYPTO_UUID, \
		SERVICE_PLAYREADY_UUID, \
		SERVICE_NVCRYPTO_UUID, \
		SERVICE_SECURE_HDCP_UUID, \
		SERVICE_TRUSTEDAPP_UUID, \
		SERVICE_TRUSTEDAPP2_UUID, \
		SERVICE_STORAGE_DEMO_UUID, \
		SERVICE_CRYPTO_DEMO_UUID, \
		SERVICE_TSEC_UUID, \
		TESTING_SERVICE_UUID, \
		SERVICE_NVTML_UUID, \
		SERVICE_VRRAUTH_UUID, \
		SERVICE_EKS2_UUID, \
		SERVICE_DOLBY_UUID, \
	}
#endif /* !defined(CONFIG_TRUSTY) */

#endif
