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

#ifndef CAMERA_FSYNC_UTILS_HPP
#define CAMERA_FSYNC_UTILS_HPP

#include <cstdint>

namespace FSYNCUtils {

    /**
     * @brief Get the Current TSC Ticks counter
     *
     * @param[out] currentTscTicks      Current TSC ticks counter
     *
     * @retval true                     If the TSC ticks counter is successfully read
     * @retval false                    If the TSC ticks counter is not read
     */
    bool getCurrentTSCTicks(uint64_t &currentTscTicks);

    /**
     * @brief Program the Fsync group for given start time
     *
     * @param[in] fsyncGroupId          Fsync group ID
     * @param[in] startTimeTSCTicks     Start time in TSC ticks
     *
     * @retval true                     If the Fsync group is successfully programmed
     * @retval false                    If the Fsync group is not programmed
     */
    bool programFsync(uint32_t const fsyncGroupId, uint64_t const startTimeTSCTicks);

    /**
     * @brief Convert microseconds to TSC Ticks
     *
     * @param microseconds            Time in microseconds
     *
     * @retval (uint64_t)               Ticks in TSC counter
     */
    constexpr uint64_t UsToTicks(uint64_t const microseconds) {
        uint32_t NS_PER_TICK{ 32U };
        return (microseconds * 1000) / NS_PER_TICK;
    }

} // namespace

#endif // CAMERA_FSYNC_UTILS_HPP
