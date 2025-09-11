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

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "CUtils.hpp"

#include "CCamFsyncUtils.hpp"
#include "NvCamFsync.h"

namespace FSYNCUtils {

/**
 * @brief Get the Current TSC Ticks counter
 *
 * @param[out] currentTscTicks      Current TSC ticks counter
 *
 * @retval true                     If the TSC ticks counter is successfully read
 * @retval false                    If the TSC ticks counter is not read
 */
bool getCurrentTSCTicks(uint64_t &currentTscTicks) {
    constexpr uint32_t TSC_BASE_ADDR = 0xc6a0000;
    constexpr uint32_t TSC_MTSCCNTCV0 = 0x10;
    constexpr uint32_t TSC_MTSCCNTCV1 = 0x14;

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        LOG_ERR("Failed to open /dev/mem\n");
        return false;
    }

    // Map the TSC memory
    const auto page_size = sysconf(_SC_PAGE_SIZE);
    const auto page_offset = TSC_BASE_ADDR & (page_size - 1);
    const auto aligned_phys = TSC_BASE_ADDR & ~(page_size - 1);
    /**
     * Length of the memory to be mapped.
     * Length  =  Number of bytes to be mapped from the starting address to the end of TSC_MTSCCNTCV1
     *            0x10 bytes from base address to TSC_MTSCCNTCV0 and 0x8 bytes
     *            from TSC_MTSCCNTCV0 to end of TSC_MTSCCNTCV1
     *            0x10 + 0x8 = 0x18
     */
    size_t const length = TSC_MTSCCNTCV0 + sizeof(uint64_t);

    void *tscMem = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, aligned_phys);
    if (tscMem == MAP_FAILED) {
        LOG_ERR("Failed to map TSC memory\n");
        close(fd);
        return false;
    }

    auto read_reg = [](void *addr) -> uint32_t {
        return *reinterpret_cast<volatile std::uint32_t*>(addr);
    };

    void *low_addr = static_cast<std::uint8_t*>(tscMem) + page_offset + TSC_MTSCCNTCV0;
    void *high_addr = static_cast<std::uint8_t*>(tscMem) + page_offset + TSC_MTSCCNTCV1;
    currentTscTicks = static_cast<uint64_t>(read_reg(low_addr)) | (static_cast<uint64_t>(read_reg(high_addr)) << 32);

    // Unmap the TSC memory
    if (munmap(tscMem, length) == -1) {
        LOG_ERR("Failed to unmap TSC memory\n");
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

/**
 * @brief Program the Fsync group for given start time
 *
 * @param[in] fsyncGroupId          Fsync group ID
 * @param[in] startTimeTSCTicks     Start time in TSC ticks
 *
 * @retval true                     If the Fsync group is successfully programmed
 * @retval false                    If the Fsync group is not programmed
 */
bool programFsync(uint32_t const fsyncGroupId, uint64_t const startTimeTSCTicks) {
    LOG_INFO("Program and start fsync signal for group: %u with start_time %llu\n", fsyncGroupId, startTimeTSCTicks);
    CAM_FSYNC_STATUS fsync_status {cam_fsync_program_abs_start_value(fsyncGroupId, startTimeTSCTicks)};
    if (fsync_status != CAM_FSYNC_OK) {
        LOG_ERR("Failed to program and start fsync signal for group: %u with status: %d\n", fsyncGroupId, fsync_status);
        return false;
    }
    return true;
}

} // namespace
