# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = sample_ffu_update

CFLAGS   := -fPIC $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_DU_INCLUDE)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

LDLIBS  += -lnvmnand_private

OBJS    := sample_ffu_update.o

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clobber clean :
	rm -rf $(OBJS) $(TARGETS)