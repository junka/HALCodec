##############################################################################
#
# Copyright (c) 2022, NVIDIA CORPORATION.  All Rights Reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#
#
###############################################################################

include ../../../../make/nvdefs.mk
TARGETS = nvlogtest_stdin
NV_NVLOG_INCLUDE := -I ../include
CFLAGS   := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_NVLOG_INCLUDE)
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_BSP_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)
LDLIBS   += -lnvlog

OBJS     := nvlogtest_stdin.o


$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)


clobber clean :
	rm -rf $(OBJS) $(TARGETS)
