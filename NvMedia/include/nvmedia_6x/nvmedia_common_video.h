/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */


/**
 * @file
 * @brief <b> NVIDIA Media Interface: MM Engines Information</b>
 *
 * @b Description: This file contains common structures which hold information about nvenc, nvdec, ofa, nvjpg devices.
 *
 */

#ifndef NVMEDIA_COMMON_VIDEO
#define NVMEDIA_COMMON_VIDEO

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Max number of HW device connected
 */
#define NVMEDIA_HW_DEVICE_MAX_COUNT         (16U)
/**
 * @brief Size of device Id
 */
#define NVMEDIA_HW_DEVICE_ID_LEN            (16U)

/**
 * @brief The 128-bit GPU device id
 */
typedef struct {
    /** array of device id */
    uint8_t  id[NVMEDIA_HW_DEVICE_ID_LEN];
} NvMediaDeviceGID;

/**
 * \brief Hold information about a MM device
 */
typedef struct {
    /**
     * @brief The GPU UUID – the primary device identifier
     * \n The GPU UUID is the primary identifier of the GPU device. The main
     * purpose of the UUID is to reliably identify a device in various
     * inter-operability scenarios.
     */
    NvMediaDeviceGID  uuid;
    /**
     * @brief The GPU UUID for the hardware device
     * \n This is the identifier for the hardware device. The hardware device
     * identifier may be different to the primary device identifier in case the
     * GPU is a multi-instance GPU (MIG) device.
     * \n The primary use case for the hardware identifier is cross-driver
     * interoperability, where hardware identifier is needed rather than the
     * logical identifier.
     */
    NvMediaDeviceGID  hwGid;
    /**
     * @brief The NUMA domain id in which the GPU device is present.
     * \n A Non-Uniform Memory Access (NUMA) architecture partitions a system
     * into multiple domains, typically each associated with its own local CPU
     * and physical memory. Performance of memory accesses to a remote domain is
     * significantly lower than that of accesses to the local domain's physical
     * memory.
     * \n Applications need to be NUMA-aware for good performance
     * under a NUMA architecture. Application threads should use domain-local
     * hardware resources for higher throughput and lower latency.
     */
    uint32_t            numaDomainId;
} NvMediaDeviceInfo;

/**
 * \brief Hold information about list of a specific MM device
 */
typedef struct {
    // @brief The size of the Device list.
    uint32_t            deviceListSize;

    /* @brief MM Engine Information.
     * This struct is the main source for device info uuid, hwid, numaDomainId
     */
    NvMediaDeviceInfo deviceList[NVMEDIA_HW_DEVICE_MAX_COUNT];
} NvMediaDeviceList;

#ifdef __cplusplus
}     /* extern "C" */
#endif

#endif /* NVMEDIA_COMMON_VIDEO */
