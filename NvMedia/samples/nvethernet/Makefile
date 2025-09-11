# SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

include ../../make/nvdefs.mk

INC_DIR := ${NV_SOURCE}/nvethernetrm/include
CFLAGS = $(NV_PLATFORM_SDK_INC)

all:
	$(CC) $(CFLAGS) -I$(INC_DIR) -O2 ether_api.c -o nvether_sample_app

clean:
	rm -rf nvether_sample_app
