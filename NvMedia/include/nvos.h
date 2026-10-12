/*
 * SPDX-FileCopyrightText: Copyright (c) 2006-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited
 */


/**
 * @file
 * <b> NVIDIA Operating System Abstraction</b>
 *
 * @b Description: Provides interfaces that enable unification of code
 * across all supported operating systems.
 */

#ifndef INCLUDED_NVOS_H
#define INCLUDED_NVOS_H

#if defined(ANDROID)
#include "nvos_s1_tegra.h"
#include "nvos_s3_tegra_safety.h"
#elif defined(__QNX__) || defined(__linux__)
#if (NV_IS_SAFETY == 0) || (NV_DEBUG == 1)
#include "nvos_s1_tegra.h"
#endif
#include "nvos_s3_tegra_safety.h"
#else
#include "nvos_tegra_nonsafety.h"
#endif /* defined(ANDROID) */

#endif // INCLUDED_NVOS_H

